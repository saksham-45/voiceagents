#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import subprocess
import sys
import uuid
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from shared import (
    AgentDaemonClient,
    AgentDaemonError,
    DiscoveryIndex,
    Orchestrator,
    Planner,
    ProjectMemory,
    ToolClient,
    ToolClientError,
)


def transcribe_with_whisper(audio_path: str, whisper_bin: str, model_path: str) -> str:
    cmd = [whisper_bin, audio_path, "-m", model_path, "-nt", "-l", "en"]
    proc = subprocess.run(cmd, capture_output=True, text=True, check=False)
    if proc.returncode != 0:
        raise RuntimeError(f"whisper.cpp failed: {proc.stderr.strip() or proc.stdout.strip()}")

    lines = [line.strip() for line in proc.stdout.splitlines() if line.strip()]
    # whisper.cpp commonly prints the final transcript as the last non-empty line.
    if not lines:
        raise RuntimeError("whisper.cpp returned no transcript")
    return lines[-1]


def build_orchestrator(sample_roots: list[str]) -> Orchestrator:
    client = ToolClient()
    discovery = DiscoveryIndex(client, sample_roots=sample_roots)
    planner = Planner()
    memory = ProjectMemory()
    return Orchestrator(client, discovery, planner, memory)


def main() -> int:
    parser = argparse.ArgumentParser(description="LMMS voice agent using whisper.cpp + shared planner")
    parser.add_argument("--audio", default=None, help="audio file to transcribe")
    parser.add_argument("--transcript", default=None, help="bypass ASR and use this transcript")
    parser.add_argument("--whisper-bin", default="whisper-cli", help="path to whisper.cpp executable")
    parser.add_argument("--whisper-model", default=None, help="path to whisper model file")
    parser.add_argument("--project-path", default=None, help="project file path for memory scoping")
    parser.add_argument("--sample-root", action="append", default=[], help="additional sample library root")
    parser.add_argument("--direct", action="store_true", help="bypass lmms-agentd and run in-process")
    parser.add_argument("--daemon-host", default="127.0.0.1", help="lmms-agentd host")
    parser.add_argument("--daemon-port", type=int, default=7781, help="lmms-agentd port")
    parser.add_argument("--daemon-timeout-s", type=float, default=30.0, help="lmms-agentd request timeout")
    parser.add_argument(
        "--timeout-class",
        default="interactive",
        choices=["interactive", "standard", "background"],
        help="daemon timeout class",
    )
    parser.add_argument("--retries", type=int, default=1, help="daemon retry attempts")
    parser.add_argument(
        "--no-direct-fallback",
        action="store_true",
        help="fail when daemon is unavailable instead of falling back to direct mode",
    )
    args = parser.parse_args()

    if not args.transcript and not args.audio:
        parser.error("either --transcript or --audio is required")

    if args.transcript:
        transcript = args.transcript.strip()
    else:
        if not args.whisper_model:
            parser.error("--whisper-model is required when using --audio")
        transcript = transcribe_with_whisper(args.audio, args.whisper_bin, args.whisper_model)

    result = None
    if not args.direct:
        daemon = AgentDaemonClient(
            host=args.daemon_host,
            port=args.daemon_port,
            timeout_s=args.daemon_timeout_s,
        )
        try:
            result = daemon.run_goal(
                transcript,
                project_path=args.project_path,
                timeout_class=args.timeout_class,
                retries=args.retries,
                idempotency_key=f"voice_{uuid.uuid4().hex}",
            )
        except AgentDaemonError as exc:
            if args.no_direct_fallback:
                print(json.dumps({"ok": False, "error": str(exc)}, indent=2), file=sys.stderr)
                return 2
            print(
                json.dumps(
                    {
                        "ok": False,
                        "warning": "daemon_unavailable_falling_back_to_direct",
                        "detail": str(exc),
                    },
                    ensure_ascii=True,
                ),
                file=sys.stderr,
            )

    if result is None:
        orchestrator = build_orchestrator(args.sample_root)
        try:
            result = orchestrator.run(transcript, project_path=args.project_path)
        except (ToolClientError, RuntimeError) as exc:
            print(json.dumps({"ok": False, "error": str(exc)}, indent=2), file=sys.stderr)
            return 2

    print(
        json.dumps(
            {
                "transcript": transcript,
                "result": result,
            },
            indent=2,
            ensure_ascii=True,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
