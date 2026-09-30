import argparse
import json
import os
import shutil
import socket
from pathlib import Path
from typing import Dict, List, Tuple


def command_exists(name: str) -> bool:
    return shutil.which(name) is not None


_MODEL_EXTENSIONS = {".safetensors", ".ckpt", ".pt", ".pth", ".bin", ".onnx"}


def has_any_files(path: Path) -> bool:
    if not path.exists() or not path.is_dir():
        return False
    return any(child.is_file() for child in path.rglob("*"))


def has_model_files(path: Path) -> bool:
    if not path.exists() or not path.is_dir():
        return False
    return any(child.suffix.lower() in _MODEL_EXTENSIONS for child in path.rglob("*") if child.is_file())


def can_connect(host: str, port: int, timeout: float = 1.5) -> bool:
    try:
        with socket.create_connection((host, port), timeout=timeout):
            return True
    except OSError:
        return False


def check(project_root: Path, comfy_port: int) -> Tuple[bool, Dict[str, List[str]]]:
    comfy_dir = project_root / "_comfyui" / "ComfyUI"
    venv_python = comfy_dir / ".venv" / "Scripts" / "python.exe"

    checkpoints = comfy_dir / "models" / "checkpoints"
    controlnet = comfy_dir / "models" / "controlnet"
    ipadapter = comfy_dir / "models" / "ipadapter"
    clip_vision = comfy_dir / "models" / "clip_vision"

    workflow_template = project_root / "_pipeline" / "tools" / "workflow_api_template.json"
    style_runner = project_root / "_pipeline" / "tools" / "comfy_style_runner.py"

    passed: List[str] = []
    warnings: List[str] = []
    failed: List[str] = []

    if command_exists("git"):
        passed.append("Git command found.")
    else:
        failed.append("Missing git. Install: https://git-scm.com/download/win")

    if command_exists("python"):
        passed.append("System python command found.")
    else:
        failed.append("Missing python in PATH. Install Python 3.11+ and re-open terminal.")

    if comfy_dir.joinpath("main.py").exists():
        passed.append(f"ComfyUI exists: {comfy_dir}")
    else:
        failed.append(f"ComfyUI missing at {comfy_dir}. Run setup_style_pipeline.bat")

    if venv_python.exists():
        passed.append(f"ComfyUI venv python exists: {venv_python}")
    else:
        failed.append("ComfyUI virtual env missing. Run setup_style_pipeline.bat")

    if workflow_template.exists():
        passed.append("workflow_api_template.json exists.")
    else:
        warnings.append("workflow_api_template.json missing. Copy from workflow_api_template.example.json and configure it.")

    if style_runner.exists():
        passed.append("comfy_style_runner.py exists.")
    else:
        failed.append("Missing comfy_style_runner.py in _pipeline/tools.")

    if has_model_files(checkpoints):
        passed.append("Checkpoint model(s) found.")
    else:
        failed.append(f"No checkpoint model (.safetensors/.ckpt) found in {checkpoints}")

    if has_model_files(controlnet):
        passed.append("ControlNet model(s) found.")
    else:
        warnings.append(f"No ControlNet model found in {controlnet}. Needed for structure-safe style transfer.")

    if has_model_files(ipadapter):
        passed.append("IP-Adapter model(s) found.")
    else:
        warnings.append(f"No IP-Adapter model found in {ipadapter}. Needed for style consistency.")

    if has_model_files(clip_vision):
        passed.append("CLIP Vision model(s) found.")
    else:
        warnings.append(f"No CLIP Vision model found in {clip_vision}. Needed by many IP-Adapter workflows.")

    if can_connect("127.0.0.1", comfy_port):
        passed.append(f"ComfyUI API port is reachable at http://127.0.0.1:{comfy_port}")
    else:
        warnings.append(
            f"ComfyUI API not reachable on port {comfy_port}. Start ComfyUI before generation."
        )

    ready = len(failed) == 0
    report = {"passed": passed, "warnings": warnings, "failed": failed}
    return ready, report


def main() -> None:
    parser = argparse.ArgumentParser(description="Check readiness for ComfyUI style generation pipeline.")
    parser.add_argument("--project-root", default=None, help="Path to game project root.")
    parser.add_argument("--comfy-port", type=int, default=8188, help="ComfyUI API port (default: 8188).")
    parser.add_argument("--json", action="store_true", help="Print result as JSON.")
    args = parser.parse_args()

    if args.project_root:
        project_root = Path(args.project_root).resolve()
    else:
        project_root = Path(__file__).resolve().parents[2]

    ready, report = check(project_root, args.comfy_port)

    if args.json:
        print(json.dumps({"ready": ready, **report}, indent=2))
    else:
        print("\n=== Style Pipeline Readiness Check ===")
        for line in report["passed"]:
            print(f"[ OK ] {line}")
        for line in report["warnings"]:
            print(f"[WARN] {line}")
        for line in report["failed"]:
            print(f"[FAIL] {line}")
        print("--------------------------------------")
        if ready:
            print("READY: Everything required is installed for style generation.")
        else:
            print("NOT READY: Fix [FAIL] items first.")

    raise SystemExit(0 if ready else 1)


if __name__ == "__main__":
    main()
