import json
import random
import sys
import uuid
from copy import deepcopy
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent.parent))

from ComgyUIPythonCommonFunction import (
    COMFYUI_SERVER,
    find_node_by_title,
    queue_prompt,
    wait_for_completion,
    download_outputs,
)

WORKFLOW_FILE = Path(__file__).parent / "ReSkinTestWorkFlow.json"
SOURCE_IMAGE_NODE_TITLE = "2.Load Image A(Source)"
REFERENCE_IMAGE_NODE_TITLE = "5.Load Image B(Reference)"
POSITIVE_PROMPT_NODE_TITLE = "4.CLIP Text Encode (Prompt) Positive"
NEGATIVE_PROMPT_NODE_TITLE = "4.CLIP Text Encode (Prompt) Negative"
IPADAPTER_NODE_TITLE = "8.IPAdapter Advanced"
KSAMPLER_NODE_TITLE = "9.KSampler"
SAVE_NODE_TITLE = "12.Save Image"


def run_reskin(source_image, reference_image, positive_prompt=None, negative_prompt=None,
               ipadapter_weight=0.6, denoise=0.65):
    try:
        with open(WORKFLOW_FILE, "r", encoding="utf-8") as f:
            base_workflow = json.load(f)
    except FileNotFoundError:
        print(f"[run_reskin] Workflow file not found: {WORKFLOW_FILE}")
        raise SystemExit(1)
    except json.JSONDecodeError as e:
        print(f"[run_reskin] Failed to parse workflow JSON: {e}")
        raise SystemExit(1)

    workflow = deepcopy(base_workflow)

    try:
        source_node_id = find_node_by_title(workflow, SOURCE_IMAGE_NODE_TITLE)
        reference_node_id = find_node_by_title(workflow, REFERENCE_IMAGE_NODE_TITLE)
        positive_node_id = find_node_by_title(workflow, POSITIVE_PROMPT_NODE_TITLE)
        negative_node_id = find_node_by_title(workflow, NEGATIVE_PROMPT_NODE_TITLE)
        ipadapter_node_id = find_node_by_title(workflow, IPADAPTER_NODE_TITLE)
        ksampler_node_id = find_node_by_title(workflow, KSAMPLER_NODE_TITLE)
        save_node_id = find_node_by_title(workflow, SAVE_NODE_TITLE)
    except ValueError as e:
        print(f"[run_reskin] {e}")
        raise SystemExit(1)

    workflow[source_node_id]["inputs"]["image"] = source_image
    workflow[reference_node_id]["inputs"]["image"] = reference_image

    if positive_prompt is not None:
        workflow[positive_node_id]["inputs"]["text"] = positive_prompt
    if negative_prompt is not None:
        workflow[negative_node_id]["inputs"]["text"] = negative_prompt

    workflow[ipadapter_node_id]["inputs"]["weight"] = ipadapter_weight
    workflow[ksampler_node_id]["inputs"]["seed"] = random.randint(1, 999999999999999)
    workflow[ksampler_node_id]["inputs"]["denoise"] = denoise
    workflow[save_node_id]["inputs"]["filename_prefix"] = "reskin_output"

    client_id = str(uuid.uuid4())

    print(f"[run_reskin] Connecting to {COMFYUI_SERVER} ...")
    print(f"[run_reskin] Source: {source_image}  Reference: {reference_image}")
    print(f"[run_reskin] IPAdapter weight={ipadapter_weight}  denoise={denoise}")

    result = queue_prompt(workflow, client_id)
    prompt_id = result["prompt_id"]

    print("Queued prompt_id:", prompt_id)
    wait_for_completion(prompt_id, client_id)
    download_outputs(prompt_id, output_dir="output")


if __name__ == "__main__":
    src = input("Enter source image filename (e.g. P1.png): ").strip()
    ref = input("Enter reference image filename (e.g. W1.png): ").strip()
    run_reskin(src, ref)
