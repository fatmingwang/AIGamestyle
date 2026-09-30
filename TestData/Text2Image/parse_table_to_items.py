"""
parse_table_to_items.py
=======================
Parses a tab-separated asset description table and uses an AI backend to
generate English text-to-image prompts for each row, producing a JSON file
compatible with the Text2Image page.

Table format (tab-separated, first row is header, then data rows):
    filename    format    size    est_size    description

Usage:
    python parse_table_to_items.py --table <path_to_tsv>
                                   [--output <out.json>]
                                   [--backend ollama|anthropic|openai]
                                   [--model <model_name>]
                                   [--api-url <url>]

stdout protocol (read by the Qt app):
    [PROGRESS] <message>
    [DONE] <absolute_path_to_output_json>
    [ERROR] <message>
"""

import argparse
import json
import re
import sys
from pathlib import Path

DEFAULT_MODELS = {
    "ollama":    "llama3.2",     # text-only is fine for table parsing
    "anthropic": "claude-haiku-4-5-20251001",
    "openai":    "gpt-4o-mini",
    "llama":     "default",      # llama.cpp ignores model name; uses whatever is loaded
}


# ── Table parsing ─────────────────────────────────────────────────────────────

def parse_size(size_str: str) -> tuple[int, int]:
    """Extract (width, height) from strings like '1500 × 400' or '750x200'."""
    m = re.search(r"(\d+)\s*[×xX]\s*(\d+)", size_str)
    if m:
        return int(m.group(1)), int(m.group(2))
    return 0, 0


def parse_table(text: str) -> list[dict]:
    """
    Parse a TSV (tab-separated) asset table.
    Returns list of {name, save_filename, width, height, description} dicts.
    """
    rows = []
    lines = [l for l in text.splitlines() if l.strip()]
    # Skip header row (contains 檔案名稱 or similar)
    data_start = 0
    for i, line in enumerate(lines):
        if "檔案名稱" in line or "filename" in line.lower():
            data_start = i + 1
            break

    for line in lines[data_start:]:
        cols = line.split("\t")
        if len(cols) < 2:
            # Try splitting on 2+ spaces as fallback
            cols = re.split(r"  +", line)
        if not cols:
            continue
        filename = cols[0].strip()
        if not filename:
            continue
        # Column layout: filename, format, size, est_size, description
        size_str = cols[2].strip() if len(cols) > 2 else ""
        description = cols[-1].strip() if len(cols) >= 2 else ""
        width, height = parse_size(size_str)
        stem = Path(filename).stem  # remove extension
        save_filename = re.sub(r"\s+", "", stem)  # strip spaces
        rows.append({
            "name":          stem,
            "save_filename": save_filename,
            "width":         width,
            "height":        height,
            "description":   description,
        })
    return rows


# ── Prompt builder ────────────────────────────────────────────────────────────

SYSTEM_PROMPT = (
    "You are a game asset prompt engineer. "
    "Your task is to convert Chinese asset descriptions into concise English "
    "text-to-image prompts suitable for Stable Diffusion / SDXL generation. "
    "Focus on visual content: composition, colours, style, mood. "
    "Do NOT add unrelated content. Keep each prompt under 120 words."
)

def build_user_message(rows: list[dict], style_hint: str) -> str:
    if style_hint:
        style_section = (
            f"MANDATORY ART STYLE — every prompt MUST reflect this style throughout:\n"
            f"{style_hint}"
        )
    else:
        style_section = (
            "MANDATORY ART STYLE — every prompt MUST reflect this style throughout:\n"
            "2D mobile game asset, clean flat illustration, vibrant saturated colours, "
            "simple soft lighting."
        )

    items_text = "\n".join(
        f'[{i+1}] name={r["name"]}  description={r["description"]}'
        for i, r in enumerate(rows)
    )

    return (
        f"{style_section}\n\n"
        "For each asset below write an English text-to-image prompt.\n"
        "Requirements:\n"
        "  • 25-50 words per prompt.\n"
        "  • Weave the art style above into the prompt naturally — do NOT omit it.\n"
        "  • Describe: subject content, composition, colours/lighting matching the style.\n"
        "  • Write in the style of a diffusion-model prompt (comma-separated descriptors).\n"
        "Return ONLY a raw JSON array (no markdown fences, no explanation):\n"
        '[\n'
        '  {"name": "filename_stem", "prompt": "..."},\n'
        '  ...\n'
        ']\n\n'
        f"Assets:\n{items_text}"
    )


def _fix_json(s: str) -> str:
    """Apply common LLM JSON fixes in order of least-destructive first."""
    # Remove trailing commas before ] or }
    s = re.sub(r",(\s*[}\]])", r"\1", s)
    # Insert missing commas between } and { (LLMs sometimes forget them)
    s = re.sub(r"\}(\s*)\{", r"},\1{", s)
    return s


def extract_json_array(text: str) -> list:
    # Strip Qwen3-style <think>…</think> blocks before any other processing
    text = re.sub(r"<think>.*?</think>", "", text, flags=re.DOTALL)
    # Strip markdown fences
    text = text.strip()
    text = re.sub(r"^```(?:json)?\s*", "", text)
    text = re.sub(r"\s*```\s*$",       "", text)
    text = text.strip()

    # Isolate the outermost [...] block
    m = re.search(r"\[.*\]", text, re.DOTALL)
    candidate = m.group() if m else text

    # 1) Direct parse
    try:
        return json.loads(candidate)
    except json.JSONDecodeError:
        pass

    # 2) After common fixes
    try:
        return json.loads(_fix_json(candidate))
    except json.JSONDecodeError:
        pass

    # 3) Extract individual {...} objects — most resilient to mid-array errors
    print("[PROGRESS] WARNING: full JSON parse failed; extracting objects individually.",
          flush=True)
    results = []
    # Match top-level objects (no nested objects expected in our schema)
    for obj_str in re.findall(r"\{[^{}]+\}", candidate, re.DOTALL):
        for attempt in (obj_str, _fix_json(obj_str)):
            try:
                results.append(json.loads(attempt))
                break
            except json.JSONDecodeError:
                pass

    if results:
        return results

    # 4) Nothing worked — print raw excerpt so the user can diagnose
    print(f"[PROGRESS] ERROR: raw model output (first 600 chars):\n{candidate[:600]}",
          flush=True)
    raise json.JSONDecodeError("Cannot parse model response as JSON array", candidate, 0)


def merge_results(rows: list[dict], ai_results: list) -> list[dict]:
    """Merge AI-generated prompts back onto the parsed rows by name."""
    ai_map = {r.get("name", ""): r.get("prompt", "") for r in ai_results}
    items = []
    for row in rows:
        prompt = ai_map.get(row["name"], "")
        if not prompt:
            prompt = f"{row['name'].replace('_', ' ')}, 2D game asset"
        items.append({
            "name":          row["name"],
            "prompt":        prompt,
            "save_filename": row["save_filename"],
            "width":         row["width"],
            "height":        row["height"],
        })
    return items


# ── Ollama backend ────────────────────────────────────────────────────────────

def call_ollama(rows: list[dict], model: str, api_url: str,
                style_hint: str) -> list[dict]:
    try:
        import ollama
    except ImportError:
        print("[ERROR] 'ollama' package not found.  Run: pip install ollama", flush=True)
        sys.exit(1)

    client_kwargs = {}
    if api_url:
        host = api_url.rstrip("/")
        if host.endswith("/v1"):
            host = host[:-3]
        client_kwargs["host"] = host
        print(f"[PROGRESS] Ollama host: {host}", flush=True)

    client = ollama.Client(**client_kwargs)
    user_msg = build_user_message(rows, style_hint)

    print(f"[PROGRESS] Sending {len(rows)} rows to Ollama ({model})...", flush=True)
    resp = client.chat(model=model, messages=[
        {"role": "system", "content": SYSTEM_PROMPT},
        {"role": "user",   "content": user_msg},
    ])
    raw = extract_json_array(resp["message"]["content"])
    return merge_results(rows, raw)


# ── Anthropic backend ─────────────────────────────────────────────────────────

def call_anthropic(rows: list[dict], model: str, api_url: str,
                   style_hint: str) -> list[dict]:
    try:
        import anthropic
    except ImportError:
        print("[ERROR] 'anthropic' package not found.  Run: pip install anthropic", flush=True)
        sys.exit(1)

    client = anthropic.Anthropic()
    user_msg = build_user_message(rows, style_hint)

    print(f"[PROGRESS] Sending {len(rows)} rows to Anthropic ({model})...", flush=True)
    response = client.messages.create(
        model=model,
        max_tokens=4096,
        system=SYSTEM_PROMPT,
        messages=[{"role": "user", "content": user_msg}],
    )
    raw = extract_json_array(response.content[0].text)
    return merge_results(rows, raw)


# ── OpenAI backend ────────────────────────────────────────────────────────────

def call_openai(rows: list[dict], model: str, api_url: str,
                style_hint: str) -> list[dict]:
    try:
        from openai import OpenAI
    except ImportError:
        print("[ERROR] 'openai' package not found.  Run: pip install openai", flush=True)
        sys.exit(1)

    client_kwargs = {}
    if api_url:
        client_kwargs["base_url"] = api_url
        import os
        if not os.environ.get("OPENAI_API_KEY"):
            client_kwargs["api_key"] = "ollama"

    client = OpenAI(**client_kwargs)
    user_msg = build_user_message(rows, style_hint)

    print(f"[PROGRESS] Sending {len(rows)} rows to OpenAI ({model})...", flush=True)
    resp = client.chat.completions.create(
        model=model,
        max_tokens=4096,
        messages=[
            {"role": "system", "content": SYSTEM_PROMPT},
            {"role": "user",   "content": user_msg},
        ],
    )
    raw = extract_json_array(resp.choices[0].message.content)
    return merge_results(rows, raw)


# ── LLaMA.cpp backend ─────────────────────────────────────────────────────────

def call_llama(rows: list[dict], model: str, api_url: str,
               style_hint: str) -> list[dict]:
    """Like call_openai but tuned for llama.cpp + Qwen3 thinking models.

    Appends /no_think to suppress the <think> block so the model answers
    directly.  extract_json_array also strips any residual thinking tags.
    """
    try:
        from openai import OpenAI
    except ImportError:
        print("[ERROR] 'openai' package not found.  Run: pip install openai", flush=True)
        sys.exit(1)

    client_kwargs = {"base_url": api_url or "http://127.0.0.1:8666/v1", "api_key": "llama"}
    client = OpenAI(**client_kwargs)
    user_msg = build_user_message(rows, style_hint) + " /no_think"

    print(f"[PROGRESS] Sending {len(rows)} rows to LLaMA.cpp ({model})...", flush=True)
    resp = client.chat.completions.create(
        model=model,
        max_tokens=8192,
        messages=[
            {"role": "system", "content": SYSTEM_PROMPT},
            {"role": "user",   "content": user_msg},
        ],
    )
    content = resp.choices[0].message.content or ""
    raw = extract_json_array(content)
    return merge_results(rows, raw)


# ── Main ──────────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(
        description="Parse asset table and generate Text2Image item prompts via AI"
    )
    parser.add_argument("--table",   required=True,
                        help="Path to TSV table file")
    parser.add_argument("--output",  default=None,
                        help="Output JSON path (default: <table>.items.json)")
    parser.add_argument("--backend", default="anthropic",
                        choices=["ollama", "anthropic", "openai", "llama"])
    parser.add_argument("--model",   default=None)
    parser.add_argument("--api-url", default=None)
    parser.add_argument("--style",   default="",
                        help="Art style hint appended to every prompt request")
    args = parser.parse_args()

    table_path = Path(args.table).resolve()
    if not table_path.exists():
        print(f"[ERROR] Table file not found: {table_path}", flush=True)
        sys.exit(1)

    table_text = table_path.read_text(encoding="utf-8")
    rows = parse_table(table_text)
    if not rows:
        print("[ERROR] No data rows found in table.", flush=True)
        sys.exit(1)

    print(f"[PROGRESS] Parsed {len(rows)} rows from table.", flush=True)

    model   = args.model or DEFAULT_MODELS[args.backend]
    api_url = args.api_url or ""

    print(f"[PROGRESS] Backend: {args.backend}  Model: {model}", flush=True)

    dispatch = {
        "ollama":    call_ollama,
        "anthropic": call_anthropic,
        "openai":    call_openai,
        "llama":     call_llama,
    }
    items = dispatch[args.backend](rows, model, api_url, args.style)

    output_path = Path(args.output) if args.output else table_path.with_suffix(".items.json")
    output_path.parent.mkdir(parents=True, exist_ok=True)
    with open(output_path, "w", encoding="utf-8") as f:
        json.dump({"items": items}, f, indent=4, ensure_ascii=False)

    print(f"[DONE] {output_path}", flush=True)


if __name__ == "__main__":
    main()
