"""
Text2Image Runner
=================
Generates an image from a text prompt using ComfyUI.

Usage:
  python run_text2image.py --prompt "golden dragon icon" --output-name "GoldenDragon" --output-dir ./out
  python run_text2image.py --prompt "..." --output-name "MyFile" --seed 42
"""

import argparse
import json
import random
import sys
import uuid
from copy import deepcopy
from pathlib import Path

ROOT = Path(__file__).parent.parent.parent
sys.path.insert(0, str(ROOT))

from TestData.ComgyUIPythonCommonFunction import (
    COMFYUI_SERVER,
    find_node_by_title,
    queue_prompt,
    wait_for_completion,
    download_outputs,
)

WORKFLOW_FILE       = Path(__file__).parent / "gsl_starter_1.json"
PROMPT_NODE_TITLE   = "MyPrompt"
KSAMPLER_NODE_TITLE = "KSampler"
SAVE_NODE_TITLE     = "Save Image"
LATENT_NODE_TITLE   = "EmptySD3LatentImage"


def run_generation(prompt: str, output_name: str, output_dir: str,
                   seed: int = -1, width: int = 0, height: int = 0) -> str:
    """
    Generate one image and return the saved output path.

    - prompt:      text prompt sent to the CLIP node
    - output_name: filename prefix used by ComfyUI SaveImage
    - output_dir:  local directory to download the result into
    - seed:        sampler seed; -1 = pick a random seed
    - width/height: canvas size; 0 means keep the workflow default
    """
    with open(WORKFLOW_FILE, "r", encoding="utf-8") as f:
        workflow = deepcopy(json.load(f))

    prompt_node_id   = find_node_by_title(workflow, PROMPT_NODE_TITLE)
    ksampler_node_id = find_node_by_title(workflow, KSAMPLER_NODE_TITLE)
    save_node_id     = find_node_by_title(workflow, SAVE_NODE_TITLE)
    latent_node_id   = find_node_by_title(workflow, LATENT_NODE_TITLE)

    workflow[prompt_node_id]["inputs"]["text"]          = prompt
    workflow[ksampler_node_id]["inputs"]["seed"]        = (
        seed if seed >= 0 else random.randint(1, 999_999_999_999_999)
    )
    workflow[save_node_id]["inputs"]["filename_prefix"] = output_name

    if latent_node_id and width > 0 and height > 0:
        workflow[latent_node_id]["inputs"]["width"]  = width
        workflow[latent_node_id]["inputs"]["height"] = height
        print(f"Canvas size set to {width}×{height}")
    else:
        default_w = workflow[latent_node_id]["inputs"].get("width", "?") if latent_node_id else "?"
        default_h = workflow[latent_node_id]["inputs"].get("height", "?") if latent_node_id else "?"
        print(f"Canvas size: {default_w}×{default_h} (workflow default)")

    client_id = str(uuid.uuid4())
    print(f"Connecting to {COMFYUI_SERVER} ...")
    result    = queue_prompt(workflow, client_id)
    prompt_id = result["prompt_id"]
    print(f"Queued: {prompt_id}")
    wait_for_completion(prompt_id, client_id)

    Path(output_dir).mkdir(parents=True, exist_ok=True)
    save_name = output_name + ".png"
    download_outputs(prompt_id, output_dir=output_dir, save_filename=save_name)
    out_path = str(Path(output_dir) / save_name)
    print(f"Saved → {out_path}")
    return out_path


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Generate an image from a text prompt via ComfyUI"
    )
    parser.add_argument("--prompt",      required=True,
                        help="Text prompt for generation")
    parser.add_argument("--output-name", required=True,
                        help="Output filename stem (no extension)")
    parser.add_argument("--output-dir",  default="./output",
                        help="Directory to save the result (default: ./output)")
    parser.add_argument("--seed",        type=int, default=-1,
                        help="Sampler seed (-1 = random)")
    parser.add_argument("--width",       type=int, default=0,
                        help="Canvas width in pixels (0 = use workflow default)")
    parser.add_argument("--height",      type=int, default=0,
                        help="Canvas height in pixels (0 = use workflow default)")
    args = parser.parse_args()

    run_generation(
        prompt=args.prompt,
        output_name=args.output_name,
        output_dir=args.output_dir,
        seed=args.seed,
        width=args.width,
        height=args.height,
    )
