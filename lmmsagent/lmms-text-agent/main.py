#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import sys
from typing import Any, Dict
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


def build_orchestrator(sample_roots: list[str]) -> Orchestrator:
    client = ToolClient()
    discovery = DiscoveryIndex(client, sample_roots=sample_roots)
    planner = Planner()
    memory = ProjectMemory()
    return Orchestrator(client, discovery, planner, memory)


def build_daemon_client(host: str, port: int, timeout_s: float) -> AgentDaemonClient:
    return AgentDaemonClient(host=host, port=port, timeout_s=timeout_s)


def prompt_step_confirmation(step: Dict[str, Any]) -> bool:
    print(
        f"\n[Step] {step.get('subgoal_title', step.get('subgoal'))}: "
        f"{step['action']} args={json.dumps(step.get('args', {}), ensure_ascii=True)} "
        f"(confidence={step.get('confidence', 0):.2f}, risk={step.get('risk', 'safe')})"
    )
    while True:
        choice = input("Run this step? [y/n] ").strip().lower()
        if choice in {"y", "yes"}:
            return True
        if choice in {"n", "no"}:
            return False
        print("Please answer y or n.")


def run_once(
    *,
    goal: str,
    project_path: str | None,
    guided: bool,
    orchestrator: Orchestrator,
    daemon_client: AgentDaemonClient | None,
    timeout_class: str,
    retries: int,
    no_direct_fallback: bool,
) -> int:
    result: Dict[str, Any]
    if guided:
        try:
            result = orchestrator.run(
                goal,
                project_path=project_path,
                confirm_step=prompt_step_confirmation,
            )
        except ToolClientError as exc:
            print(json.dumps({"ok": False, "error": str(exc)}, indent=2), file=sys.stderr)
            return 2
        print(json.dumps(result, indent=2, ensure_ascii=True))
        return 0

    if daemon_client is not None:
        try:
            result = daemon_client.run_goal(
                goal,
                project_path=project_path,
                timeout_class=timeout_class,
                retries=retries,
            )
            print(json.dumps(result, indent=2, ensure_ascii=True))
            return 0
        except AgentDaemonError as exc:
            if no_direct_fallback:
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

    try:
        result = orchestrator.run(
            goal,
            project_path=project_path,
        )
    except ToolClientError as exc:
        print(json.dumps({"ok": False, "error": str(exc)}, indent=2), file=sys.stderr)
        return 2

    print(json.dumps(result, indent=2, ensure_ascii=True))
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="LMMS text agent over typed AgentControl tools")
    parser.add_argument("goal", nargs="*", help="request text")
    parser.add_argument("--project-path", default=None, help="project file path for memory scoping")
    parser.add_argument("--sample-root", action="append", default=[], help="additional sample library root")
    parser.add_argument("--interactive", action="store_true", help="read goals in a loop")
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
    parser.add_argument("--retries", type=int, default=1, help="daemon retry attempts for run_goal")
    parser.add_argument(
        "--no-direct-fallback",
        action="store_true",
        help="fail when daemon is unavailable instead of falling back to direct mode",
    )
    parser.add_argument(
        "--guided",
        action="store_true",
        help="confirm each planned step before execution",
    )
    args = parser.parse_args()

    orchestrator = build_orchestrator(args.sample_root)
    daemon_client = None if args.direct or args.guided else build_daemon_client(
        args.daemon_host, args.daemon_port, args.daemon_timeout_s
    )

    if args.interactive:
        print("LMMS text agent interactive mode. Type 'exit' to quit.")
        while True:
            try:
                goal = input("lmms> ").strip()
            except EOFError:
                print()
                return 0
            if not goal:
                continue
            if goal.lower() in {"exit", "quit"}:
                return 0
            rc = run_once(
                goal=goal,
                project_path=args.project_path,
                guided=args.guided,
                orchestrator=orchestrator,
                daemon_client=daemon_client,
                timeout_class=args.timeout_class,
                retries=args.retries,
                no_direct_fallback=args.no_direct_fallback,
            )
            if rc != 0:
                return rc
        
    goal = " ".join(args.goal).strip()
    if not goal:
        parser.error("goal is required unless --interactive is used")
    return run_once(
        goal=goal,
        project_path=args.project_path,
        guided=args.guided,
        orchestrator=orchestrator,
        daemon_client=daemon_client,
        timeout_class=args.timeout_class,
        retries=args.retries,
        no_direct_fallback=args.no_direct_fallback,
    )


if __name__ == "__main__":
    raise SystemExit(main())
