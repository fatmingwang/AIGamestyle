"""
Auto-group slot machine symbols by visual content and generate ComfyUI prompts.
Supports multiple vision backends.

Backends:
  ollama     (default) -- free, local.  Install: https://ollama.com
                          Pull a vision model first: ollama pull llava
  anthropic             -- requires ANTHROPIC_API_KEY env var
  openai                -- requires OPENAI_API_KEY env var

Usage:
    python auto_group_symbols.py --folder <path>
                                 [--reference  <style_ref_image>]
                                 [--output     <output.json>]
                                 [--backend    ollama|anthropic|openai]
                                 [--model      <model_name>]

stdout protocol (read by the Qt app):
    [PROGRESS] <message>
    [DONE] <absolute_path_to_output_json>
    [ERROR] <message>
"""

import argparse
import base64
import json
import re
import sys
from pathlib import Path

IMAGE_EXTS = {".png", ".jpg", ".jpeg", ".webp"}
MEDIA_TYPES = {
    ".png":  "image/png",
    ".jpg":  "image/jpeg",
    ".jpeg": "image/jpeg",
    ".webp": "image/webp",
}

DEFAULT_MODELS = {
    "ollama":    "llava",
    "anthropic": "claude-sonnet-4-6",
    "openai":    "gpt-4o",
    "llama":     "default",      # llama.cpp ignores model name; uses whatever is loaded
}

IP_DEFAULTS = {
    "WILD":         (0.75, 0.80),
    "High Value":   (0.55, 0.85),
    "Card Letters": (0.30, 0.75),
    "Bonus":        (0.60, 0.80),
}


# ── Helpers ───────────────────────────────────────────────────────────────────

def encode_b64(path: Path) -> tuple[str, str]:
    with open(path, "rb") as f:
        data = base64.standard_b64encode(f.read()).decode()
    return data, MEDIA_TYPES.get(path.suffix.lower(), "image/png")


def _fix_json(s: str) -> str:
    s = re.sub(r",(\s*[}\]])", r"\1", s)   # trailing commas
    s = re.sub(r"\}(\s*)\{",   r"},\1{", s) # missing commas between objects
    return s


def extract_json(text: str) -> dict:
    text = text.strip()
    text = re.sub(r"^```(?:json)?\s*", "", text)
    text = re.sub(r"\s*```\s*$",       "", text)
    text = text.strip()

    for candidate in (text, _fix_json(text)):
        try:
            return json.loads(candidate)
        except json.JSONDecodeError:
            pass

    # Try to find the first { ... } block
    m = re.search(r"\{.*\}", text, re.DOTALL)
    if m:
        for candidate in (m.group(), _fix_json(m.group())):
            try:
                return json.loads(candidate)
            except json.JSONDecodeError:
                pass

    print(f"[PROGRESS] ERROR: raw model output (first 600 chars):\n{text[:600]}", flush=True)
    raise json.JSONDecodeError("Cannot parse model response as JSON", text, 0)


def per_image_instruction(style_desc: str, filename: str, style_directive: str = "") -> str:
    parts = []
    if style_desc:
        parts.append(f"The TARGET ART STYLE is described as: {style_desc}")
    else:
        parts.append(
            "Use vibrant 3D cartoon slot game art style: "
            "glossy rounded shapes, saturated colours, soft ambient lighting."
        )
    if style_directive:
        parts.append(f"Creative direction to apply: {style_directive}")
    style_note = "  ".join(parts) + "  Describe recreating the SAME CONTENT accordingly."
    return (
        f'Analyse slot machine symbol image "{filename}". {style_note}\n\n'
        "Return ONLY a raw JSON object (no markdown fences):\n"
        '{\n'
        '  "group": "WILD" or "High Value" or "Card Letters" or "Bonus" or "Other",\n'
        '  "prompt": "detailed positive prompt ending with \', transparent background, slot game art\'",\n'
        '  "negative_prompt": "blurry, low quality, distorted, realistic photo, watermark, signature, deformed, extra limbs"\n'
        '}'
    )


def finalize(results: list, reference_path, folder: Path) -> dict:
    """Group per-image results into the output JSON structure."""
    group_map: dict[str, list] = {}
    for r in results:
        key = r.get("group", "Other")
        group_map.setdefault(key, []).append(r)

    ref_str = str(reference_path).replace("\\", "/") if reference_path else ""
    groups = []
    for name, items in group_map.items():
        ip_w, den = IP_DEFAULTS.get(name, (0.55, 0.80))
        groups.append({
            "name":      name,
            "reference": ref_str,
            "ip_weight": ip_w,
            "denoise":   den,
            "items": [
                {
                    "file":            str(folder / r["filename"]).replace("\\", "/"),
                    "prompt":          r.get("prompt", ""),
                    "negative_prompt": r.get("negative_prompt", ""),
                }
                for r in items
            ],
        })
    return {"groups": groups}


# ── Ollama backend ────────────────────────────────────────────────────────────

def analyse_ollama(image_paths: list, reference_path, model: str, api_url: str,
                   style_directive: str = "") -> dict:
    try:
        import ollama
    except ImportError:
        print("[ERROR] 'ollama' package not found.  Run: pip install ollama", flush=True)
        sys.exit(1)

    client_kwargs = {}
    if api_url:
        # Ensure no /v1 suffix — ollama native API doesn't use it
        host = api_url.rstrip("/")
        if host.endswith("/v1"):
            host = host[:-3]
        client_kwargs["host"] = host
        print(f"[PROGRESS] Ollama host: {host}", flush=True)

    client = ollama.Client(**client_kwargs)

    # Step 1: describe reference style (if provided)
    style_desc = ""
    if reference_path and Path(reference_path).exists():
        print("[PROGRESS] Analysing style reference...", flush=True)
        resp = client.chat(
            model=model,
            messages=[{
                "role":    "user",
                "content": (
                    "Describe the ART STYLE of this image in 2-3 sentences for use in "
                    "AI image generation prompts. Cover: render style (2D/3D/cartoon), "
                    "materials, lighting, colour palette, outline style. Be concise."
                ),
                "images": [str(reference_path)],
            }],
        )
        style_desc = resp["message"]["content"].strip()
        print(f"[PROGRESS] Style: {style_desc[:80]}...", flush=True)

    # Step 2: classify + prompt each image individually
    results = []
    for i, path in enumerate(image_paths):
        print(f"[PROGRESS] Analysing {i + 1}/{len(image_paths)}: {path.name}", flush=True)
        instruction = per_image_instruction(style_desc, path.name, style_directive)
        resp = client.chat(
            model=model,
            messages=[{
                "role":    "user",
                "content": instruction,
                "images":  [str(path)],
            }],
        )
        try:
            data = extract_json(resp["message"]["content"])
        except Exception as e:
            print(f"[PROGRESS] WARNING: could not parse JSON for {path.name}: {e}", flush=True)
            data = {
                "group":           "Other",
                "prompt":          f"{path.stem} slot machine symbol, transparent background, slot game art",
                "negative_prompt": "blurry, low quality, distorted, realistic photo, watermark",
            }
        data["filename"] = path.name
        results.append(data)

    return finalize(results, reference_path, image_paths[0].parent)


# ── Anthropic backend ─────────────────────────────────────────────────────────

def analyse_anthropic(image_paths: list, reference_path, model: str, api_url: str,
                      style_directive: str = "") -> dict:
    try:
        import anthropic
    except ImportError:
        print("[ERROR] 'anthropic' package not found.  Run: pip install anthropic", flush=True)
        sys.exit(1)

    client = anthropic.Anthropic()
    content = []

    has_ref = reference_path and Path(reference_path).exists()
    if has_ref:
        b64, mt = encode_b64(Path(reference_path))
        content.append({"type": "text",
                         "text": "TARGET STYLE REFERENCE — defines the art style to recreate:"})
        content.append({"type": "image",
                         "source": {"type": "base64", "media_type": mt, "data": b64}})

    content.append({"type": "text",
                     "text": f"The following {len(image_paths)} images are the ORIGINAL symbols:"})
    for i, p in enumerate(image_paths):
        b64, mt = encode_b64(p)
        content.append({"type": "text", "text": f"[{i + 1}] {p.name}"})
        content.append({"type": "image",
                         "source": {"type": "base64", "media_type": mt, "data": b64}})

    style_hint_parts = []
    style_hint_parts.append(
        "Describe every element in the TARGET ART STYLE shown by the reference."
        if has_ref else
        "Use vibrant 3D cartoon slot game art style."
    )
    if style_directive:
        style_hint_parts.append(f"Creative direction to apply: {style_directive}")
    style_hint = "  ".join(style_hint_parts)

    content.append({"type": "text", "text": f"""Group all symbols semantically and generate prompts.
{style_hint}
Return ONLY raw JSON:
{{
  "groups": [
    {{
      "name": "...", "ip_weight": 0.75, "denoise": 0.80,
      "items": [{{"filename": "x.png", "prompt": "...", "negative_prompt": "..."}}]
    }}
  ]
}}
ip_weight/denoise: WILD 0.75/0.80, High Value 0.55/0.85, Card Letters 0.30/0.75"""})

    print(f"[PROGRESS] Sending {len(image_paths)} images to Anthropic ({model})...", flush=True)
    response = client.messages.create(
        model=model, max_tokens=4096,
        messages=[{"role": "user", "content": content}],
    )
    raw = extract_json(response.content[0].text)

    # Normalise to per-image results then re-finalize for consistent path handling
    results = []
    for g in raw.get("groups", []):
        for item in g.get("items", []):
            results.append({
                "group":           g["name"],
                "filename":        item.get("filename", ""),
                "prompt":          item.get("prompt", ""),
                "negative_prompt": item.get("negative_prompt", ""),
                "_ip":             g.get("ip_weight", 0.55),
                "_den":            g.get("denoise", 0.80),
            })
    # Re-use the same ip/denoise that Claude chose
    group_map: dict[str, list] = {}
    for r in results:
        group_map.setdefault(r["group"], []).append(r)
    ref_str = str(reference_path).replace("\\", "/") if reference_path else ""
    groups = []
    for name, items in group_map.items():
        ip_w = items[0].get("_ip", IP_DEFAULTS.get(name, (0.55, 0.80))[0])
        den  = items[0].get("_den", IP_DEFAULTS.get(name, (0.55, 0.80))[1])
        groups.append({
            "name": name, "reference": ref_str,
            "ip_weight": ip_w, "denoise": den,
            "items": [
                {"file": str(image_paths[0].parent / r["filename"]).replace("\\", "/"),
                 "prompt": r["prompt"], "negative_prompt": r["negative_prompt"]}
                for r in items
            ],
        })
    return {"groups": groups}


# ── OpenAI backend ────────────────────────────────────────────────────────────

def analyse_openai(image_paths: list, reference_path, model: str, api_url: str,
                   style_directive: str = "") -> dict:
    try:
        from openai import OpenAI
    except ImportError:
        print("[ERROR] 'openai' package not found.  Run: pip install openai", flush=True)
        sys.exit(1)

    client_kwargs = {}
    if api_url:
        client_kwargs["base_url"] = api_url
        # Local/compatible servers don't need a real key
        import os
        if not os.environ.get("OPENAI_API_KEY"):
            client_kwargs["api_key"] = "ollama"
        print(f"[PROGRESS] OpenAI-compat URL: {api_url}", flush=True)

    client = OpenAI(**client_kwargs)
    style_desc = ""

    if reference_path and Path(reference_path).exists():
        print("[PROGRESS] Analysing style reference...", flush=True)
        b64, mt = encode_b64(Path(reference_path))
        resp = client.chat.completions.create(
            model=model,
            messages=[{"role": "user", "content": [
                {"type": "text", "text": "Describe the art style of this image in 2-3 concise sentences for AI prompt use."},
                {"type": "image_url", "image_url": {"url": f"data:{mt};base64,{b64}"}},
            ]}],
            max_tokens=256,
        )
        style_desc = resp.choices[0].message.content.strip()
        print(f"[PROGRESS] Style: {style_desc[:80]}...", flush=True)

    results = []
    for i, path in enumerate(image_paths):
        print(f"[PROGRESS] Analysing {i + 1}/{len(image_paths)}: {path.name}", flush=True)
        b64, mt = encode_b64(path)
        instruction = per_image_instruction(style_desc, path.name, style_directive)
        resp = client.chat.completions.create(
            model=model,
            messages=[{"role": "user", "content": [
                {"type": "text", "text": instruction},
                {"type": "image_url", "image_url": {"url": f"data:{mt};base64,{b64}"}},
            ]}],
            max_tokens=512,
        )
        try:
            data = extract_json(resp.choices[0].message.content)
        except Exception as e:
            print(f"[PROGRESS] WARNING: parse error for {path.name}: {e}", flush=True)
            data = {"group": "Other", "prompt": f"{path.stem}, transparent background, slot game art",
                    "negative_prompt": "blurry, low quality, distorted, realistic photo, watermark"}
        data["filename"] = path.name
        results.append(data)

    return finalize(results, reference_path, image_paths[0].parent)


# ── Main ──────────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(description="Auto-group symbols via Vision AI")
    parser.add_argument("--folder",    required=True)
    parser.add_argument("--reference", default=None)
    parser.add_argument("--output",    default=None)
    parser.add_argument("--backend",   default="ollama",
                        choices=["ollama", "anthropic", "openai", "llama"])
    parser.add_argument("--model",   default=None,
                        help="Override the default model for the chosen backend")
    parser.add_argument("--api-url", default=None,
                        help="API server URL (e.g. http://127.0.0.1:11434 for Ollama)")
    parser.add_argument("--style-directive", default=None,
                        help="High-level creative direction, e.g. 'make humans become animals'")
    args = parser.parse_args()

    folder          = Path(args.folder).resolve()
    ref             = Path(args.reference).resolve() if args.reference else None
    model           = args.model or DEFAULT_MODELS[args.backend]
    api_url         = args.api_url or ""
    style_directive = args.style_directive or ""

    image_paths = sorted(p for p in folder.iterdir() if p.suffix.lower() in IMAGE_EXTS)
    if not image_paths:
        print("[ERROR] No images found in folder.", flush=True)
        sys.exit(1)

    print(f"[PROGRESS] Backend: {args.backend}  Model: {model}", flush=True)
    print(f"[PROGRESS] Found {len(image_paths)} images.", flush=True)
    if style_directive:
        print(f"[PROGRESS] Style directive: {style_directive}", flush=True)

    dispatch = {
        "ollama":    analyse_ollama,
        "anthropic": analyse_anthropic,
        "openai":    analyse_openai,
        "llama":     analyse_openai,
    }
    result = dispatch[args.backend](image_paths, ref, model, api_url, style_directive)

    output_path = Path(args.output) if args.output else folder / "symbol_groups_auto.json"
    with open(output_path, "w", encoding="utf-8") as f:
        json.dump(result, f, indent=4, ensure_ascii=False)

    print(f"[DONE] {output_path}", flush=True)


if __name__ == "__main__":
    main()
