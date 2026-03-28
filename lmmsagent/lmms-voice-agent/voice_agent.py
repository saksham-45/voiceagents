#!/usr/bin/env python3
import argparse
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from shared.intent_router import normalize_command
from shared.lmms_client import LmmsClient


def transcribe_with_whisper(whisper_cli: str, audio_file: str) -> str:
    proc = subprocess.run(
        [whisper_cli, "-f", audio_file, "-otxt", "-nt"],
        check=True,
        capture_output=True,
        text=True,
    )
    output = proc.stdout.strip()
    if output:
        return output
    transcript_file = Path(audio_file).with_suffix(Path(audio_file).suffix + ".txt")
    if transcript_file.exists():
        return transcript_file.read_text(encoding="utf-8").strip()
    raise RuntimeError("whisper.cpp did not return a transcript")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--text")
    parser.add_argument("--audio-file")
    parser.add_argument("--whisper-cli")
    args = parser.parse_args()

    if not args.text and not args.audio_file:
        raise SystemExit("Provide --text or --audio-file")
    if args.audio_file and not args.whisper_cli:
        raise SystemExit("--audio-file requires --whisper-cli")

    transcript = args.text or transcribe_with_whisper(args.whisper_cli, args.audio_file)
    command = normalize_command(transcript)
    print(LmmsClient().send_text(command))
