import json
import subprocess
import sys
from pathlib import Path


def run() -> None:
    tools_dir = Path(__file__).resolve().parent
    cli = tools_dir / "asset_pipeline.py"
    target_folder = "textures"
    if len(sys.argv) > 1:
        target_folder = sys.argv[1]

    commands = [
        [sys.executable, str(cli), "scan", target_folder],
        [sys.executable, str(cli), "classify"],
        [sys.executable, str(cli), "link"],
        [sys.executable, str(cli), "plan-style", "--batch-id", "smoke_batch"],
    ]

    outputs = {}
    for command in commands:
        result = subprocess.run(command, check=True, capture_output=True, text=True)
        outputs[command[2]] = json.loads(result.stdout)

    print(json.dumps({"smoke_test": "passed", "outputs": outputs}, indent=2))


if __name__ == "__main__":
    run()
