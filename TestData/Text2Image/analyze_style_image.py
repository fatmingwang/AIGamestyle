"""
analyze_style_image.py
======================
Analyze the art style of a reference image and return a short English
description suitable for use as a text-to-image style hint.

Usage:
    python analyze_style_image.py --image path/to/ref.png
                                  [--backend ollama|anthropic|openai]
                                  [--model <model>]
                                  [--api-url <url>]

stdout protocol:
    [PROGRESS] <message>
    [DONE] <style description text>
    [ERROR] <message>
"""

import argparse
import base64
import sys
from pathlib import Path

ROOT = Path(__file__).parent.parent.parent
sys.path.insert(0, str(ROOT))

MEDIA_TYPES = {
    ".png":  "image/png",
    ".jpg":  "image/jpeg",
    ".jpeg": "image/jpeg",
    ".webp": "image/webp",
    ".bmp":  "image/bmp",
}

STYLE_PROMPT = (
    "Describe the ART STYLE of this image in 2-3 concise sentences for use as "
    "an AI image generation style hint. Cover: render style (2D/3D/cartoon/flat), "
    "materials, lighting, colour palette, outline style. "
    "Output ONLY the description — no preamble, no quotes."
)

DEFAULT_MODELS = {
    "ollama":    "gemma4:e4b",   # Gemma 4 is multimodal — supports image input
    "anthropic": "claude-haiku-4-5-20251001",
    "openai":    "gpt-4o-mini",
    "llama":     "default",      # llama.cpp ignores model name; uses whatever is loaded
}


def encode_b64(path: Path) -> tuple[str, str]:
    with open(path, "rb") as f:
        data = base64.standard_b64encode(f.read()).decode()
    return data, MEDIA_TYPES.get(path.suffix.lower(), "image/png")


# ── Backends ──────────────────────────────────────────────────────────────────

def analyze_ollama(image_path: Path, model: str, api_url: str) -> str:
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
    print(f"[PROGRESS] Analyzing style with Ollama ({model})...", flush=True)
    resp = client.chat(
        model=model,
        messages=[{
            "role":    "user",
            "content": STYLE_PROMPT,
            "images":  [str(image_path)],
        }],
    )
    return resp["message"]["content"].strip()


def analyze_anthropic(image_path: Path, model: str, api_url: str) -> str:
    try:
        import anthropic
    except ImportError:
        print("[ERROR] 'anthropic' package not found.  Run: pip install anthropic", flush=True)
        sys.exit(1)

    b64, mt = encode_b64(image_path)
    client = anthropic.Anthropic()
    print(f"[PROGRESS] Analyzing style with Anthropic ({model})...", flush=True)
    response = client.messages.create(
        model=model,
        max_tokens=512,
        messages=[{"role": "user", "content": [
            {"type": "image",
             "source": {"type": "base64", "media_type": mt, "data": b64}},
            {"type": "text", "text": STYLE_PROMPT},
        ]}],
    )
    return response.content[0].text.strip()


def analyze_openai(image_path: Path, model: str, api_url: str) -> str:
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
    b64, mt = encode_b64(image_path)
    print(f"[PROGRESS] Analyzing style with OpenAI ({model})...", flush=True)
    resp = client.chat.completions.create(
        model=model,
        max_tokens=512,
        messages=[{"role": "user", "content": [
            {"type": "text",      "text": STYLE_PROMPT},
            {"type": "image_url", "image_url": {"url": f"data:{mt};base64,{b64}"}},
        ]}],
    )
    return resp.choices[0].message.content.strip()


def analyze_llama(image_path: Path, model: str, api_url: str) -> str:
    """Like analyze_openai but tuned for llama.cpp + Qwen3 thinking models.

    Qwen3's thinking mode burns all tokens on <think> blocks and returns empty
    content when max_tokens is too small.  Append /no_think to suppress it and
    give a larger budget so the actual answer fits.
    """
    try:
        from openai import OpenAI
    except ImportError:
        print("[ERROR] 'openai' package not found.  Run: pip install openai", flush=True)
        sys.exit(1)

    client_kwargs = {"base_url": api_url or "http://127.0.0.1:8666/v1", "api_key": "llama"}
    client = OpenAI(**client_kwargs)
    b64, mt = encode_b64(image_path)
    print(f"[PROGRESS] Analyzing style with LLaMA.cpp ({model})...", flush=True)
    resp = client.chat.completions.create(
        model=model,
        max_tokens=2048,
        messages=[{"role": "user", "content": [
            {"type": "text",      "text": STYLE_PROMPT + " /no_think"},
            {"type": "image_url", "image_url": {"url": f"data:{mt};base64,{b64}"}},
        ]}],
    )
    content = resp.choices[0].message.content or ""
    # Strip any residual <think>…</think> block that slipped through
    import re
    content = re.sub(r"<think>.*?</think>", "", content, flags=re.DOTALL).strip()
    if not content:
        print("[ERROR] LLaMA.cpp returned empty content. "
              "Make sure the model is loaded with --mmproj (vision support).", flush=True)
        sys.exit(1)
    return content


# ── Main ──────────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(
        description="Analyze image art style for use as a style hint"
    )
    parser.add_argument("--image",   required=True,  help="Path to the reference image")
    parser.add_argument("--backend", default="anthropic",
                        choices=["ollama", "anthropic", "openai", "llama"])
    parser.add_argument("--model",   default=None)
    parser.add_argument("--api-url", default=None)
    args = parser.parse_args()

    image_path = Path(args.image).resolve()
    if not image_path.exists():
        print(f"[ERROR] Image not found: {image_path}", flush=True)
        sys.exit(1)

    model   = args.model or DEFAULT_MODELS[args.backend]
    api_url = args.api_url or ""

    print(f"[PROGRESS] Backend: {args.backend}  Model: {model}", flush=True)

    dispatch = {
        "ollama":    analyze_ollama,
        "anthropic": analyze_anthropic,
        "openai":    analyze_openai,
        "llama":     analyze_llama,
    }
    result = dispatch[args.backend](image_path, model, api_url)
    print(f"[DONE] {result}", flush=True)


if __name__ == "__main__":
    main()
