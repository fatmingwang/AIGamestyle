"""Version checks for the ComfyUI venv, called by start_comfyui_with_flags.bat.

Run with the venv's python, from anywhere:
  python check_comfyui_env.py torch <min_version>   exit 0 = torch is new enough, 1 = too old
  python check_comfyui_env.py reqs <requirements.txt>  exit 0 = all satisfied, 1 = something missing/too old

"torch" also tests the feature recent ComfyUI packages (comfy-kitchen) rely on: registering a
custom op whose signature uses the builtin `list[int]` annotation. Old torch builds reject that
at import time with "infer_schema(func): Parameter ... has unsupported type list[int]".

"reqs" skips torch/torchvision/torchaudio: those come from the CUDA index and are handled by
the "torch" check, never from plain PyPI (which only has CPU builds on Windows).
"""
import sys

TORCH_PACKAGES = {"torch", "torchvision", "torchaudio"}


def check_torch(min_version: str) -> bool:
    try:
        import torch
    except Exception as e:  # not installed / broken install
        print(f"[WARN] PyTorch cannot be imported: {e}")
        return False
    from pip._vendor.packaging.version import Version

    installed = torch.__version__
    if Version(installed.split("+")[0]) < Version(min_version):
        print(f"[WARN] PyTorch {installed} is older than the required {min_version}.")
        return False
    try:
        def probe(x: torch.Tensor, stride: list[int]) -> torch.Tensor:  # noqa: ARG001
            ...
        torch.library.infer_schema(probe, mutates_args=())
    except Exception as e:
        print(f"[WARN] PyTorch {installed} is too old for current ComfyUI packages ({type(e).__name__}).")
        return False
    print(f"[ OK ] PyTorch {installed} is recent enough.")
    return True


def check_requirements(path: str) -> bool:
    from importlib.metadata import PackageNotFoundError, version
    from pip._vendor.packaging.requirements import InvalidRequirement, Requirement

    problems = []
    with open(path, encoding="utf-8") as f:
        for raw in f:
            line = raw.split("#", 1)[0].strip()
            if not line or line.startswith("-"):
                continue
            try:
                req = Requirement(line)
            except InvalidRequirement:
                continue
            if req.marker and not req.marker.evaluate():
                continue
            if req.name.lower() in TORCH_PACKAGES:
                continue
            try:
                have = version(req.name)
            except PackageNotFoundError:
                problems.append(f"{req.name} (not installed)")
                continue
            if req.specifier and not req.specifier.contains(have, prereleases=True):
                problems.append(f"{req.name} {have} (needs {req.specifier})")

    if problems:
        print("[WARN] ComfyUI Python packages are missing or too old:")
        for p in problems:
            print(f"       - {p}")
        return False
    print("[ OK ] ComfyUI Python packages match requirements.txt.")
    return True


def main() -> int:
    if len(sys.argv) != 3 or sys.argv[1] not in ("torch", "reqs"):
        print(__doc__)
        return 2
    ok = check_torch(sys.argv[2]) if sys.argv[1] == "torch" else check_requirements(sys.argv[2])
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
