import json
import subprocess
import sys
from pathlib import Path


def main() -> None:
    tools_dir = Path(__file__).resolve().parent
    cli_file = tools_dir / "asset_pipeline.py"

    target_folder = "textures"
    if len(sys.argv) > 1:
        target_folder = sys.argv[1]

    command = [sys.executable, str(cli_file), "all", target_folder]
    result = subprocess.run(command, check=True, capture_output=True, text=True)

    payload = json.loads(result.stdout)
    print(json.dumps({"status": "ok", "outputs": payload}, indent=2))


if __name__ == "__main__":
    main()
