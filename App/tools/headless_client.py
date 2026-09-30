#!/usr/bin/env python3
"""TCP client for GameStyleAI's `--headless` server (see Doc/Commands_Reference.md §12).

The server never exits after one command: it listens on a TCP port, and a connected client
sends one JSON object per line ({"command": "...", "args": {...}}) and gets back one or more
NDJSON events per command, ending in a "job_end". This client exists to exercise that protocol
by hand -- connect, send commands one at a time, see exactly what comes back -- so you can
confirm the server is reachable and behaving before wiring up a real automation client.

Usage:
    # interactive session (default 127.0.0.1:8765)
    python headless_client.py

    # custom address
    python headless_client.py --host 127.0.0.1 --port 8765

    # one-shot: send a single command and exit (exit code mirrors job_end.success)
    python headless_client.py comfyui-status
    python headless_client.py scan asset-root=D:/Work/GamestyleAI/TestData/Reskin/props overwrite=true

Interactive mode: type "<command> [key=value ...]" per line, "help" for the command list, and
"exit" (or Ctrl+D/Ctrl+C) to disconnect. Values are parsed as JSON when possible (true/false/
numbers/"quoted strings"), otherwise sent as plain strings -- so plain paths don't need quoting.
Use forward slashes in paths (D:/Work/GamestyleAI), not backslashes: a bare backslash is not
valid JSON and this client does no path-specific escaping.
"""
from __future__ import annotations

import argparse
import json
import shlex
import socket
import sys

# Mirrors the command table in Doc/Commands_Reference.md §12 -- kept here so `help` works
# without the server's own --help (which only prints startup-flag usage, not this catalog).
COMMANDS = {
    "set-working-dir": "args: path=<dir>                         chdir + set Project Root for this session",
    "set-config":      "args: any of project-root, output-folder, asset-root, hero-reference,\n"
                        "                        positive-prompt, negative-prompt, style-profile-id, batch-id,\n"
                        "                        comfyui-server, python-exe, category, ip-weight, denoise, steps,\n"
                        "                        include-spine",
    "get-config":      "no args -- returns this session's current config",
    "check":           "no args -- readiness check (git/python/ComfyUI/venv/models/port)",
    "comfyui-status":  "no args -- job_end.success is true/false for reachable/unreachable",
    "comfyui-launch":  "args: wait-timeout=<seconds> (default 60)",
    "scan":            "args: asset-root=<dir>, overwrite=true | clean=true",
    "classify":        "no args (uses the last scan)",
    "link":            "no args (uses the last classify)",
    "plan-style":      "args: style-profile-id=<id>, batch-id=<id>",
    "comfyui-batch":   "args: overwrite=true | clean=true, regenerate=true | continue-stale=true,\n"
                        "                        ip-weight=<0..1>, denoise=<0..1>, steps=<n>",
    "simple-batch":    "args: asset-root=<dir>, overwrite=true | clean=true",
    "all":             "args: same gates as scan + comfyui-batch combined",
    "text2image":      "args: prompt=<text> (required), output-name=, output-dir=, width=, height=\n"
                        "                        generate a new image straight from a prompt, no source asset",
    "imagewithprompt": "args: reference=<img> (required), positive-prompt= or category=, negative-prompt=,\n"
                        "                        output-dir=, output-name=, seed=, steps=, cfg=, denoise=, ip-weight=\n"
                        "                        prompt-guided variant of one reference image",
    "pose-from-prompt": "args: name=<n>, description=<text> (both required), subject-type=, loop=,\n"
                        "                        frames=, backend=, api-url=, output=\n"
                        "                        generate pose/skeleton JSON from a text description",
    "atlas-extract":   "args: atlas-path=<file.atlas> (required), scale=1.0\n"
                        "                        unpack a Spine atlas into individual sprite PNGs",
    "atlas-merge":     "args: atlas-path=<file.atlas> (required), scale=1.0,\n"
                        "                        merge-mode=separate|single, conflict=fail|replace|rename\n"
                        "                        pack sprite PNGs (from atlas-extract) back into the atlas",
    "nas-upload":      "args: source=<dir>, target=<dir> (both required), conflict=fail|overwrite|rename\n"
                        "                        robocopy a local output folder to a NAS target",
    "exit":            "no args -- closes the connection (no reply is sent)",
}


def print_help() -> None:
    print("Commands:")
    for name, desc in COMMANDS.items():
        print(f"  {name:<16} {desc}")
    print(
        "\nType a line as: <command> [key=value ...]\n"
        "Examples:\n"
        "  set-working-dir path=D:/Work/GamestyleAI\n"
        "  comfyui-launch wait-timeout=120\n"
        "  scan asset-root=D:/Work/GamestyleAI/TestData/Reskin/props overwrite=true\n"
        "  plan-style style-profile-id=default_parameters batch-id=nightly\n"
    )


def parse_value(raw: str):
    """A bare token is sent as a string; anything that also happens to be valid JSON (true,
    123, 0.75, "quoted") is sent as that JSON type instead, matching what the server expects
    for flags like overwrite/include-spine and numeric fields like steps/denoise."""
    try:
        return json.loads(raw)
    except (json.JSONDecodeError, ValueError):
        return raw


def kv_pairs_to_args(tokens: list[str]) -> dict:
    args: dict = {}
    for tok in tokens:
        if "=" not in tok:
            print(f"  (ignoring malformed arg '{tok}', expected key=value)", file=sys.stderr)
            continue
        key, _, value = tok.partition("=")
        args[key] = parse_value(value)
    return args


def print_event(obj: dict) -> None:
    t = obj.get("type")
    if t == "job_start":
        print(f"  -> job_start  {obj.get('command')}")
    elif t == "job_end":
        ok = "OK  " if obj.get("success") else "FAIL"
        print(f"  <- job_end    [{ok}] code={obj.get('code')}  {obj.get('message')}")
    elif t == "step_start":
        print(f"  -- step_start {obj.get('step')} ({obj.get('index')}/{obj.get('total')})")
    elif t == "step_end":
        ok = "OK  " if obj.get("success") else "FAIL"
        print(f"  -- step_end   [{ok}] {obj.get('step')} exit={obj.get('exitCode')}")
    elif t == "marker":
        extra = obj.get("message") or obj.get("detail") or ""
        print(f"  .. marker     {obj.get('marker')} step={obj.get('scriptStep')} {extra}")
    elif t == "log":
        print(f"  .. log[{obj.get('stream')}] {obj.get('line')}")
    elif t == "result":
        rest = {k: v for k, v in obj.items() if k not in ("type", "command")}
        print(f"  == result     {obj.get('command')}: {json.dumps(rest, ensure_ascii=False)}")
    elif t == "error":
        print(f"  !! error      {obj.get('message')}")
    else:
        print(f"  ?? {obj}")


class HeadlessClient:
    def __init__(self, host: str, port: int, connect_timeout: float = 10.0):
        self.sock = socket.create_connection((host, port), timeout=connect_timeout)
        self.sock.settimeout(None)  # commands like comfyui-launch/all can take minutes
        self._reader = self.sock.makefile("rb")

    def send(self, command: str, args: dict | None = None) -> list[dict]:
        payload: dict = {"command": command}
        if args:
            payload["args"] = args
        self.sock.sendall((json.dumps(payload) + "\n").encode("utf-8"))

        if command in ("exit", "quit"):
            return []

        events: list[dict] = []
        while True:
            raw = self._reader.readline()
            if not raw:
                print("  [connection closed by server]")
                break
            raw = raw.strip()
            if not raw:
                continue
            try:
                obj = json.loads(raw)
            except json.JSONDecodeError:
                print(f"  [unparseable line from server] {raw!r}")
                continue
            events.append(obj)
            print_event(obj)
            if obj.get("type") == "job_end":
                break
        return events

    def close(self) -> None:
        try:
            self.sock.close()
        except OSError:
            pass


def run_interactive(client: HeadlessClient) -> None:
    print("Connected. Type 'help' for the command list, 'exit' to disconnect and quit.")
    while True:
        try:
            line = input("headless> ")
        except (EOFError, KeyboardInterrupt):
            print()
            break

        line = line.strip()
        if not line:
            continue
        if line in ("help", "?"):
            print_help()
            continue

        try:
            tokens = shlex.split(line)
        except ValueError as e:
            print(f"  (parse error: {e})")
            continue
        if not tokens:
            continue

        command, arg_tokens = tokens[0], tokens[1:]
        client.send(command, kv_pairs_to_args(arg_tokens))
        if command in ("exit", "quit"):
            break


def run_one_shot(client: HeadlessClient, command: str, arg_tokens: list[str]) -> int:
    events = client.send(command, kv_pairs_to_args(arg_tokens))
    if command in ("exit", "quit"):
        return 0
    job_end = next((e for e in events if e.get("type") == "job_end"), None)
    if job_end is None:
        return 1
    return 0 if job_end.get("success") else int(job_end.get("code", 1) or 1)


def main() -> None:
    parser = argparse.ArgumentParser(
        description="TCP client for GameStyleAI's --headless server.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    parser.add_argument("--host", default="127.0.0.1", help="Server host (default 127.0.0.1)")
    parser.add_argument("--port", type=int, default=8765, help="Server port (default 8765)")
    parser.add_argument("command", nargs="?", help="One-shot mode: command name (omit for interactive mode)")
    parser.add_argument("args", nargs="*", help="One-shot mode: key=value pairs")
    ns = parser.parse_args()

    try:
        client = HeadlessClient(ns.host, ns.port)
    except OSError as e:
        print(f"Could not connect to {ns.host}:{ns.port}: {e}", file=sys.stderr)
        sys.exit(1)

    print(f"Connected to {ns.host}:{ns.port}")
    try:
        if ns.command:
            sys.exit(run_one_shot(client, ns.command, ns.args))
        else:
            run_interactive(client)
    finally:
        client.close()


if __name__ == "__main__":
    main()
