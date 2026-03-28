#!/usr/bin/env python3
import argparse
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from shared.intent_router import normalize_command
from shared.lmms_client import LmmsClient


def run_once(client: LmmsClient, text: str) -> None:
    command = normalize_command(text)
    if not command:
        return
    print(client.send_text(command))


def repl(client: LmmsClient) -> None:
    while True:
        try:
            text = input("lmms> ")
        except EOFError:
            break
        if text.strip().lower() in {"exit", "quit"}:
            break
        run_once(client, text)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("command", nargs="*")
    parser.add_argument("--interactive", action="store_true")
    args = parser.parse_args()

    client = LmmsClient()
    if args.interactive or not args.command:
        repl(client)
    else:
        run_once(client, " ".join(args.command))
