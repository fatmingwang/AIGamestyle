import argparse
import copy
import json
import time
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path
from typing import Any, Dict, Iterable, List


MODEL_EXTENSIONS = {".safetensors", ".ckpt", ".pt", ".pth", ".bin", ".onnx"}
PLACEHOLDER_CHECKPOINT_NAMES = {
    "put_your_model_name_here.safetensors",
    "model.safetensors",
    "__CHECKPOINT_NAME__",
}


class ComfyApiError(RuntimeError):
    pass


def load_json(path: Path) -> Any:
    with path.open("r", encoding="utf-8") as file:
        return json.load(file)


def save_json(path: Path, data: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as file:
        json.dump(data, file, indent=2, ensure_ascii=False)
        file.write("\n")


def list_model_files(path: Path) -> List[str]:
    if not path.exists() or not path.is_dir():
        return []
    return sorted(
        child.name
        for child in path.iterdir()
        if child.is_file() and child.suffix.lower() in MODEL_EXTENSIONS
    )


def is_placeholder_checkpoint_name(name: str) -> bool:
    return name.strip() in PLACEHOLDER_CHECKPOINT_NAMES


def deep_replace(value: Any, mapping: Dict[str, str]) -> Any:
    if isinstance(value, str):
        output = value
        for key, replace_value in mapping.items():
            output = output.replace(key, replace_value)
        return output
    if isinstance(value, list):
        return [deep_replace(item, mapping) for item in value]
    if isinstance(value, dict):
        return {k: deep_replace(v, mapping) for k, v in value.items()}
    return value


def http_json(url: str, method: str = "GET", payload: Dict[str, Any] | None = None) -> Any:
    data = None
    headers = {"Content-Type": "application/json"}
    if payload is not None:
        data = json.dumps(payload).encode("utf-8")
    request = urllib.request.Request(url, data=data, headers=headers, method=method)
    try:
        with urllib.request.urlopen(request, timeout=30) as response:
            return json.loads(response.read().decode("utf-8"))
    except urllib.error.HTTPError as exc:
        error_body = ""
        try:
            raw = exc.read()
            if raw:
                error_body = raw.decode("utf-8", errors="replace")
        except Exception:
            error_body = ""

        details = error_body.strip() or "(no response body)"
        raise ComfyApiError(
            f"ComfyUI API request failed: {method} {url}\n"
            f"HTTP {exc.code} {exc.reason}\n"
            f"Details: {details}\n"
            "Hint: for HTTP 400, check workflow JSON validity and LoadImage paths."
        ) from exc


def submit_prompt(base_url: str, prompt: Dict[str, Any], client_id: str) -> str:
    payload = {"prompt": prompt, "client_id": client_id}
    response = http_json(f"{base_url}/prompt", method="POST", payload=payload)
    if not isinstance(response, dict) or "prompt_id" not in response:
        raise ComfyApiError(f"ComfyUI /prompt response missing prompt_id: {response}")
    return response["prompt_id"]


def wait_for_finish(base_url: str, prompt_id: str, poll_seconds: float = 2.0) -> Dict[str, Any]:
    while True:
        try:
            history = http_json(f"{base_url}/history/{prompt_id}")
        except urllib.error.URLError:
            print("[WARN] Cannot reach ComfyUI history endpoint; retrying...")
            time.sleep(poll_seconds)
            continue

        if prompt_id in history:
            item = history[prompt_id]
            status = item.get("status", {})
            if status.get("completed") is True:
                return item

        print(f"[INFO] Waiting for prompt {prompt_id}...")
        time.sleep(poll_seconds)


def find_output_files(history_item: Dict[str, Any]) -> List[str]:
    outputs = history_item.get("outputs", {})
    files: List[str] = []
    for _, node_output in outputs.items():
        images = node_output.get("images", [])
        for image in images:
            filename = image.get("filename")
            subfolder = image.get("subfolder", "")
            if filename:
                if subfolder:
                    files.append(f"{subfolder}/{filename}")
                else:
                    files.append(filename)
    return files


def build_cases_from_visual_array(visual_array_file: Path) -> List[List[str]]:
    data = load_json(visual_array_file)
    groups = data.get("SameVisualImageArrayArray", [])
    return [group for group in groups if group]


def run_single(
    base_url: str,
    template: Dict[str, Any],
    client_id: str,
    input_image: str,
    style_reference: str,
    output_prefix: str,
    checkpoint_name: str = "",
) -> Dict[str, Any]:
    mapping = {
        "__INPUT_IMAGE__": input_image,
        "__STYLE_REFERENCE__": style_reference,
        "__OUTPUT_PREFIX__": output_prefix,
    }
    if checkpoint_name:
        mapping["__CHECKPOINT_NAME__"] = checkpoint_name
    prompt = deep_replace(copy.deepcopy(template), mapping)
    prompt_id = submit_prompt(base_url, prompt, client_id)
    print(f"[OK] Submitted prompt: {prompt_id}")
    history_item = wait_for_finish(base_url, prompt_id)
    files = find_output_files(history_item)
    print(f"[OK] Finished prompt {prompt_id}, outputs={len(files)}")
    return {
        "prompt_id": prompt_id,
        "input_image": input_image,
        "style_reference": style_reference,
        "output_prefix": output_prefix,
        "output_files": files,
    }


def run_batch(
    base_url: str,
    template: Dict[str, Any],
    client_id: str,
    visual_array_file: Path,
    mode: str,
    project_root: Path,
    output_prefix: str,
    checkpoint_name: str = "",
) -> Dict[str, Any]:
    groups = build_cases_from_visual_array(visual_array_file)
    print(f"[INFO] Loaded {len(groups)} visual groups from {visual_array_file}")

    results = []
    for index, group in enumerate(groups, start=1):
        if mode == "first-only":
            targets = [group[0]]
        else:
            targets = group

        # Prefer static image as style reference if found
        style_ref = next((p for p in group if p.startswith("textures/")), group[0])

        print(f"[INFO] Group {index}/{len(groups)} ref={style_ref} targets={len(targets)}")
        for target in targets:
            result = run_single(
                base_url=base_url,
                template=template,
                client_id=client_id,
                input_image=target,
                style_reference=style_ref,
                output_prefix=f"{output_prefix}_g{index:03d}",
                checkpoint_name=checkpoint_name,
            )
            results.append(result)

    report = {
        "generated_at": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "mode": mode,
        "groups": len(groups),
        "jobs": len(results),
        "results": results,
        "project_root": project_root.as_posix(),
    }
    return report


def main() -> None:
    parser = argparse.ArgumentParser(description="Run single/batch style generation jobs through ComfyUI API.")
    parser.add_argument("--base-url", default="http://127.0.0.1:8188", help="ComfyUI base URL.")
    parser.add_argument("--template", default=None, help="Path to ComfyUI API workflow template JSON.")
    parser.add_argument("--client-id", default="skinbuilder-style-runner", help="ComfyUI client id.")
    parser.add_argument("--project-root", default=None, help="Path to game project root.")
    parser.add_argument("--checkpoint", default=None, help="Override checkpoint filename, e.g. dreamshaper_8.safetensors")

    subparsers = parser.add_subparsers(dest="command", required=True)

    single = subparsers.add_parser("single", help="Run a single test generation.")
    single.add_argument("--input", required=True, help="Relative image path (e.g. textures/symbols/H2.png)")
    single.add_argument("--style-ref", required=True, help="Relative style reference image path.")
    single.add_argument("--output-prefix", default="style_test_single", help="Output prefix token for SaveImage node.")

    batch = subparsers.add_parser("batch", help="Run batch generation from visual match arrays.")
    batch.add_argument("--visual-array-file", default=None, help="Path to visual_match_arrays.json")
    batch.add_argument("--mode", choices=["all", "first-only"], default="first-only")
    batch.add_argument("--output-prefix", default="style_test_batch", help="Output prefix token for SaveImage node.")
    batch.add_argument("--report", default=None, help="Where to write batch report JSON.")

    args = parser.parse_args()

    project_root = Path(args.project_root).resolve() if args.project_root else Path(__file__).resolve().parents[2]
    template_path = Path(args.template).resolve() if args.template else (Path(__file__).resolve().parent / "workflow_api_template.json")

    if not template_path.exists():
        raise SystemExit(
            "[FAIL] workflow_api_template.json missing. Copy workflow_api_template.example.json "
            "to workflow_api_template.json and replace placeholders with your exported ComfyUI API workflow."
        )

    try:
        _ = http_json(f"{args.base_url}/system_stats")
    except Exception as exc:
        raise SystemExit(
            f"[FAIL] Cannot reach ComfyUI at {args.base_url}. Start ComfyUI first. Details: {exc}"
        )

    template = load_json(template_path)

    # Resolve checkpoint name: --checkpoint flag > style_profile JSON > auto-detect/validation
    style_profile_path = project_root / "_pipeline" / "style_profiles" / "default_parameters.json"
    checkpoints_dir = project_root / "_comfyui" / "ComfyUI" / "models" / "checkpoints"
    style_profile: Dict[str, Any] = load_json(style_profile_path) if style_profile_path.exists() else {}
    checkpoint_name: str = getattr(args, "checkpoint", None) or style_profile.get("checkpoint_model", "")
    available_checkpoints = list_model_files(checkpoints_dir)
    workflow_uses_checkpoint_placeholder = "__CHECKPOINT_NAME__" in json.dumps(template)

    if workflow_uses_checkpoint_placeholder:
        if not available_checkpoints:
            raise SystemExit(
                f"[FAIL] No checkpoint model file found in {checkpoints_dir}.\n"
                "  Put a real `.safetensors` or `.ckpt` file there first, then set `checkpoint_model` in\n"
                "  `_pipeline/style_profiles/default_parameters.json` or pass `--checkpoint <name.safetensors>`."
            )

        if not checkpoint_name or is_placeholder_checkpoint_name(checkpoint_name):
            if len(available_checkpoints) == 1:
                checkpoint_name = available_checkpoints[0]
                print(f"[INFO] Auto-selected checkpoint: {checkpoint_name}")
            else:
                available_text = "\n".join(f"  - {name}" for name in available_checkpoints)
                raise SystemExit(
                    "[FAIL] Checkpoint name is still a placeholder or not set.\n"
                    "  Fix one of these:\n"
                    "  1. Set `checkpoint_model` in `_pipeline/style_profiles/default_parameters.json`\n"
                    "  2. Or pass `--checkpoint <name.safetensors>` to `comfy_style_runner.py`\n"
                    f"  Available checkpoint files:\n{available_text}"
                )

        if checkpoint_name not in available_checkpoints:
            available_text = "\n".join(f"  - {name}" for name in available_checkpoints)
            raise SystemExit(
                f"[FAIL] Configured checkpoint model not found: {checkpoint_name}\n"
                f"  Folder: {checkpoints_dir}\n"
                f"  Available checkpoint files:\n{available_text}"
            )

    if checkpoint_name:
        print(f"[INFO] Using checkpoint: {checkpoint_name}")

    if args.command == "single":
        try:
            result = run_single(
                base_url=args.base_url,
                template=template,
                client_id=args.client_id,
                input_image=args.input,
                style_reference=args.style_ref,
                output_prefix=args.output_prefix,
                checkpoint_name=checkpoint_name,
            )
        except ComfyApiError as exc:
            raise SystemExit(f"[FAIL] {exc}")
        print(json.dumps(result, indent=2))
        return

    visual_array_file = Path(args.visual_array_file).resolve() if args.visual_array_file else (
        project_root / "_pipeline" / "reports" / "visual_match_arrays.json"
    )
    if not visual_array_file.exists():
        raise SystemExit(f"[FAIL] visual array file not found: {visual_array_file}")

    try:
        report = run_batch(
            base_url=args.base_url,
            template=template,
            client_id=args.client_id,
            visual_array_file=visual_array_file,
            mode=args.mode,
            project_root=project_root,
            output_prefix=args.output_prefix,
            checkpoint_name=checkpoint_name,
        )
    except ComfyApiError as exc:
        raise SystemExit(f"[FAIL] {exc}")

    report_path = Path(args.report).resolve() if args.report else (
        project_root / "_pipeline" / "reports" / "style_generation_report.json"
    )
    save_json(report_path, report)
    print(f"[OK] Batch report written: {report_path}")


if __name__ == "__main__":
    main()
