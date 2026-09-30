import json
import random
import sys
import uuid
import urllib.request
import urllib.error
import mimetypes
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

WORKFLOW_FILE = Path(__file__).parent / "UpScale.json"
LOAD_IMAGE_NODE_TITLE = "Load Image"
SAVE_NODE_TITLE = "Save Image"
KSAMPLER_NODE_TITLE = "KSampler"


def upload_image(image_path):
    image_path = Path(image_path)
    boundary = "----ComfyUIBoundary" + uuid.uuid4().hex

    mime_type = mimetypes.guess_type(image_path.name)[0] or "application/octet-stream"
    file_data = image_path.read_bytes()

    body = b""
    body += f"--{boundary}\r\n".encode()
    body += f'Content-Disposition: form-data; name="image"; filename="{image_path.name}"\r\n'.encode()
    body += f"Content-Type: {mime_type}\r\n\r\n".encode()
    body += file_data
    body += b"\r\n"
    body += f"--{boundary}\r\n".encode()
    body += b'Content-Disposition: form-data; name="type"\r\n\r\n'
    body += b"input\r\n"
    body += f"--{boundary}\r\n".encode()
    body += b'Content-Disposition: form-data; name="overwrite"\r\n\r\n'
    body += b"true\r\n"
    body += f"--{boundary}--\r\n".encode()

    req = urllib.request.Request(
        f"http://{COMFYUI_SERVER}/upload/image",
        data=body,
        headers={"Content-Type": f"multipart/form-data; boundary={boundary}"},
        method="POST",
    )

    try:
        result = json.loads(urllib.request.urlopen(req).read())
        return result["name"]
    except urllib.error.HTTPError as e:
        print(f"[upload_image] HTTP {e.code}: {e.read().decode('utf-8')}")
        raise SystemExit(1)


def run_upscale(input_image_path, output_prefix="upscale_result"):
    uploaded_name = upload_image(input_image_path)
    print("Uploaded image:", uploaded_name)

    try:
        with open(WORKFLOW_FILE, "r", encoding="utf-8") as f:
            base_workflow = json.load(f)
    except FileNotFoundError:
        print(f"[run_upscale] Workflow file not found: {WORKFLOW_FILE}")
        raise SystemExit(1)

    workflow = deepcopy(base_workflow)

    try:
        load_image_node = find_node_by_title(workflow, LOAD_IMAGE_NODE_TITLE)
        save_node = find_node_by_title(workflow, SAVE_NODE_TITLE)
        ksampler_node = find_node_by_title(workflow, KSAMPLER_NODE_TITLE)
    except ValueError as e:
        print(f"[run_upscale] {e}")
        raise SystemExit(1)

    workflow[load_image_node]["inputs"]["image"] = uploaded_name
    workflow[save_node]["inputs"]["filename_prefix"] = output_prefix
    workflow[ksampler_node]["inputs"]["seed"] = random.randint(1, 999999999999999)

    client_id = str(uuid.uuid4())

    result = queue_prompt(workflow, client_id)
    prompt_id = result["prompt_id"]
    print("Queued:", result)

    wait_for_completion(prompt_id, client_id)
    download_outputs(prompt_id)


if __name__ == "__main__":
    image_path = input("Input image path: ").strip().strip('"')
    run_upscale(image_path)
