import json
import os
import time
import urllib.request
import urllib.parse
import urllib.error
import websocket

#COMFYUI_SERVER = "127.0.0.1:8188"
COMFYUI_SERVER = os.environ.get("COMFYUI_SERVER", "localhost:8188")


def find_node_by_title(workflow, title):
    for node_id, node_data in workflow.items():
        if node_data.get("_meta", {}).get("title") == title:
            return node_id
    raise ValueError(f"Cannot find node with title: {title}")


def queue_prompt(workflow, client_id):
    payload = {"prompt": workflow, "client_id": client_id}
    data = json.dumps(payload).encode("utf-8")
    request = urllib.request.Request(
        f"http://{COMFYUI_SERVER}/prompt",
        data=data,
        headers={"Content-Type": "application/json"},
    )
    try:
        response = urllib.request.urlopen(request)
        return json.loads(response.read())
    except urllib.error.HTTPError as e:
        body = e.read().decode("utf-8")
        print(f"[queue_prompt] HTTP {e.code} from {COMFYUI_SERVER}")
        try:
            parsed = json.loads(body)
            print("[queue_prompt] Error detail:")
            print(json.dumps(parsed, indent=2, ensure_ascii=False))
            node_errors = parsed.get("node_errors", {})
            if node_errors:
                print("[queue_prompt] Node errors summary:")
                for node_id, info in node_errors.items():
                    class_type = info.get("class_type", "?")
                    for err in info.get("errors", []):
                        print(f"  Node {node_id} ({class_type}): {err['message']} — {err['details']}")
        except Exception:
            print("[queue_prompt] Raw response:", body)
        raise SystemExit(1)
    except urllib.error.URLError as e:
        print(f"[queue_prompt] Cannot reach ComfyUI at {COMFYUI_SERVER}")
        print(f"[queue_prompt] Reason: {e.reason}")
        raise SystemExit(1)


def wait_for_completion(prompt_id, client_id):
    try:
        ws = websocket.WebSocket()
        ws.connect(f"ws://{COMFYUI_SERVER}/ws?clientId={client_id}")
    except Exception as e:
        print(f"[wait_for_completion] WebSocket connection failed: {e}")
        raise SystemExit(1)

    print("Connected to ComfyUI WebSocket.")
    print("Waiting for prompt_id:", prompt_id)

    current_node = None
    started = False
    last_queue_check = 0

    while True:
        now = time.time()
        if now - last_queue_check > 5:
            last_queue_check = now
            try:
                queue_data = json.loads(
                    urllib.request.urlopen(f"http://{COMFYUI_SERVER}/queue").read()
                )
                running = queue_data.get("queue_running", [])
                pending = queue_data.get("queue_pending", [])
                running_ids = [item[1] for item in running if len(item) > 1]
                pending_ids = [item[1] for item in pending if len(item) > 1]

                if prompt_id in running_ids:
                    print("Your job is running.")
                elif prompt_id in pending_ids:
                    position = pending_ids.index(prompt_id) + 1
                    print(f"Your job is pending. Queue position: {position}")
                elif not started:
                    print("Your job is not in queue. It may have finished or failed early.")
            except Exception as e:
                print("Queue check failed:", e)

        try:
            ws.settimeout(5)
            message = ws.recv()
        except websocket.WebSocketTimeoutException:
            continue

        if not isinstance(message, str):
            continue

        data = json.loads(message)
        msg_type = data.get("type")
        msg_data = data.get("data", {})
        msg_prompt_id = msg_data.get("prompt_id")

        if msg_prompt_id is not None and msg_prompt_id != prompt_id:
            continue

        if msg_type == "execution_start":
            started = True
            print("Your job started.")
        elif msg_type == "executing":
            node = msg_data.get("node")
            if node is None:
                print("Your workflow finished.")
                break
            current_node = node
            started = True
            print(f"Executing your node: {node}")
        elif msg_type == "progress":
            value = msg_data.get("value")
            maximum = msg_data.get("max")
            if value is not None and maximum:
                percent = round(value / maximum * 100, 2)
                node_info = f" node={current_node}" if current_node else ""
                print(f"Progress:{node_info} {value}/{maximum} ({percent}%)")
        elif msg_type == "execution_error":
            print("Your workflow failed:")
            print(json.dumps(msg_data, indent=2, ensure_ascii=False))
            break
        elif msg_type == "execution_success":
            print("Your workflow completed successfully.")
            break

    ws.close()


def get_history(prompt_id):
    with urllib.request.urlopen(
        f"http://{COMFYUI_SERVER}/history/{prompt_id}"
    ) as response:
        return json.loads(response.read())


def download_image(filename, subfolder, folder_type, save_path):
    params = urllib.parse.urlencode({
        "filename": filename,
        "subfolder": subfolder,
        "type": folder_type,
    })
    url = f"http://{COMFYUI_SERVER}/view?{params}"
    if os.path.exists(save_path):
        print(f"WARNING: file already exists, overwriting: {save_path}")
    urllib.request.urlretrieve(url, save_path)
    print("Downloaded:", save_path)


def download_outputs(prompt_id, output_dir=".", save_filename=None):
    """Download outputs for a prompt.

    save_filename: if given, save the first image under this name instead of
                   the ComfyUI-generated name (e.g. "H2_00001_.png" → "H2.png").
    """
    os.makedirs(output_dir, exist_ok=True)
    history = get_history(prompt_id)
    if prompt_id not in history:
        print(f"[download_outputs] WARNING: prompt_id {prompt_id} not found in history — no files saved.")
        return []
    prompt_data = history[prompt_id]
    outputs = prompt_data.get("outputs", {})
    saved = []
    for node_id in outputs:
        node_output = outputs[node_id]
        if "images" in node_output:
            for idx, image in enumerate(node_output["images"]):
                filename = image["filename"]
                subfolder = image["subfolder"]
                folder_type = image["type"]
                if save_filename and idx == 0:
                    out_name = save_filename
                else:
                    out_name = filename
                save_path = os.path.join(output_dir, out_name)
                download_image(filename, subfolder, folder_type, save_path)
                saved.append(save_path)
    if not saved:
        print(f"[download_outputs] WARNING: No images found in outputs for prompt_id {prompt_id}.")
        print(f"[download_outputs] Node output keys: { {nid: list(outputs[nid].keys()) for nid in outputs} }")
    return saved
