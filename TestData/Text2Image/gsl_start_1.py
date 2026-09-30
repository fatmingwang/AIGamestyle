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

WORKFLOW_FILE = Path(__file__).parent / "gsl_starter_1.json"
PROMPT_NODE_TITLE = "MyPrompt"
KSAMPLER_NODE_TITLE = "KSampler"
SAVE_NODE_TITLE = "Save Image"


def run_with_prompt(custom_prompt):
    try:
        with open(WORKFLOW_FILE, "r", encoding="utf-8") as f:
            base_workflow = json.load(f)
    except FileNotFoundError:
        print(f"[run_with_prompt] Workflow file not found: {WORKFLOW_FILE}")
        raise SystemExit(1)
    except json.JSONDecodeError as e:
        print(f"[run_with_prompt] Failed to parse workflow JSON: {e}")
        raise SystemExit(1)

    workflow = deepcopy(base_workflow)

    try:
        prompt_node_id = find_node_by_title(workflow, PROMPT_NODE_TITLE)
        ksampler_node_id = find_node_by_title(workflow, KSAMPLER_NODE_TITLE)
        save_node_id = find_node_by_title(workflow, SAVE_NODE_TITLE)
    except ValueError as e:
        print(f"[run_with_prompt] {e}")
        raise SystemExit(1)

    workflow[prompt_node_id]["inputs"]["text"] = custom_prompt
    workflow[ksampler_node_id]["inputs"]["seed"] = random.randint(1, 999999999999999)
    workflow[save_node_id]["inputs"]["filename_prefix"] = "qoo_custom_prompt"

    client_id = str(uuid.uuid4())

    print(f"[run_with_prompt] Connecting to {COMFYUI_SERVER} ...")
    result = queue_prompt(workflow, client_id)
    prompt_id = result["prompt_id"]

    print("Queued prompt_id:", prompt_id)
    wait_for_completion(prompt_id, client_id)
    download_outputs(prompt_id, output_dir="output")


if __name__ == "__main__":
    prompt = input("Enter your prompt: ")
    run_with_prompt(prompt)
