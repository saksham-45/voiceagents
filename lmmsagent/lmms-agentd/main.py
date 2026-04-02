#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import socketserver
import threading
import time
import uuid
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Dict, Optional

import sys

ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from shared import DiscoveryIndex, Orchestrator, Planner, ProjectMemory, ToolClient, ToolClientError


TIMEOUT_CLASS_RETRIES = {
    "interactive": 1,
    "standard": 2,
    "background": 3,
}

TIMEOUT_CLASS_BACKOFF_MS = {
    "interactive": 120,
    "standard": 250,
    "background": 400,
}


def now_ms() -> int:
    return int(time.time() * 1000)


@dataclass
class CachedResponse:
    payload: Dict[str, Any]
    expires_at: float


@dataclass
class AgentRuntime:
    sample_roots: list[str] = field(default_factory=list)
    cache_ttl_s: float = 300.0

    def __post_init__(self) -> None:
        self._lock = threading.RLock()
        self._started = time.monotonic()
        self._idempotency: Dict[str, CachedResponse] = {}
        self._requests_total = 0
        self._tool_client = ToolClient()
        self._discovery = DiscoveryIndex(self._tool_client, sample_roots=self.sample_roots)
        self._planner = Planner()
        self._memory = ProjectMemory()
        self._orchestrator = Orchestrator(
            tool_client=self._tool_client,
            discovery=self._discovery,
            planner=self._planner,
            memory=self._memory,
        )

    def _cleanup_cache(self) -> None:
        now = time.monotonic()
        expired = [key for key, cached in self._idempotency.items() if cached.expires_at <= now]
        for key in expired:
            del self._idempotency[key]

    def _ok(
        self,
        *,
        request_id: str,
        op: str,
        started: float,
        result: Dict[str, Any],
        timeout_class: str,
        attempts: int = 1,
        replayed: bool = False,
    ) -> Dict[str, Any]:
        return {
            "ok": True,
            "request_id": request_id,
            "op": op,
            "timeout_class": timeout_class,
            "attempts": attempts,
            "replayed": replayed,
            "duration_ms": int((time.monotonic() - started) * 1000),
            "result": result,
            "meta": {
                "server_uptime_s": int(time.monotonic() - self._started),
                "requests_total": self._requests_total,
            },
        }

    def _error(
        self,
        *,
        request_id: str,
        op: str,
        started: float,
        timeout_class: str,
        code: str,
        message: str,
        attempts: int = 1,
    ) -> Dict[str, Any]:
        return {
            "ok": False,
            "request_id": request_id,
            "op": op,
            "timeout_class": timeout_class,
            "attempts": attempts,
            "duration_ms": int((time.monotonic() - started) * 1000),
            "error": {"code": code, "message": message},
            "meta": {
                "server_uptime_s": int(time.monotonic() - self._started),
                "requests_total": self._requests_total,
            },
        }

    def _normalize_retries(self, timeout_class: str, raw_retries: Any) -> int:
        default_retries = TIMEOUT_CLASS_RETRIES.get(timeout_class, TIMEOUT_CLASS_RETRIES["standard"])
        if raw_retries is None:
            return default_retries
        try:
            parsed = int(raw_retries)
        except (TypeError, ValueError):
            return default_retries
        return max(0, min(parsed, 6))

    def _health_result(self) -> Dict[str, Any]:
        return {
            "status": "ok",
            "server_time_ms": now_ms(),
            "uptime_s": int(time.monotonic() - self._started),
            "idempotency_cache_size": len(self._idempotency),
            "requests_total": self._requests_total,
        }

    def _warmup_result(self, project_path: Optional[str]) -> Dict[str, Any]:
        with self._lock:
            discovery_stats = self._discovery.refresh(project_path=project_path)
            state = self._tool_client.get_project_state()
        return {
            "discovery": discovery_stats,
            "project_state": {
                "tempo": state.get("tempo"),
                "track_count": state.get("track_count"),
                "project_file": state.get("project_file"),
            },
        }

    def handle_request(self, request: Dict[str, Any]) -> Dict[str, Any]:
        started = time.monotonic()
        self._requests_total += 1
        self._cleanup_cache()

        op = request.get("op")
        if not isinstance(op, str) or not op.strip():
            request_id = str(request.get("request_id") or f"daemon_{uuid.uuid4().hex[:12]}")
            return self._error(
                request_id=request_id,
                op="unknown",
                started=started,
                timeout_class="standard",
                code="invalid_request",
                message="missing op",
            )

        op = op.strip()
        request_id = str(request.get("request_id") or f"daemon_{uuid.uuid4().hex[:12]}")
        timeout_class = str(request.get("timeout_class") or "standard").strip().lower()
        if timeout_class not in TIMEOUT_CLASS_RETRIES:
            timeout_class = "standard"
        retries = self._normalize_retries(timeout_class, request.get("retries"))

        if op == "health":
            return self._ok(
                request_id=request_id,
                op=op,
                started=started,
                result=self._health_result(),
                timeout_class=timeout_class,
            )

        if op == "warmup":
            try:
                result = self._warmup_result(request.get("project_path"))
                return self._ok(
                    request_id=request_id,
                    op=op,
                    started=started,
                    result=result,
                    timeout_class=timeout_class,
                )
            except ToolClientError as exc:
                return self._error(
                    request_id=request_id,
                    op=op,
                    started=started,
                    timeout_class=timeout_class,
                    code="tool_client_error",
                    message=str(exc),
                )

        if op != "run_goal":
            return self._error(
                request_id=request_id,
                op=op,
                started=started,
                timeout_class=timeout_class,
                code="unknown_op",
                message=f"Unsupported op: {op}",
            )

        goal = request.get("goal")
        if not isinstance(goal, str) or not goal.strip():
            return self._error(
                request_id=request_id,
                op=op,
                started=started,
                timeout_class=timeout_class,
                code="invalid_request",
                message="run_goal requires non-empty goal",
            )

        idempotency_key = request.get("idempotency_key")
        if isinstance(idempotency_key, str) and idempotency_key:
            cached = self._idempotency.get(idempotency_key)
            if cached and cached.expires_at > time.monotonic():
                return self._ok(
                    request_id=request_id,
                    op=op,
                    started=started,
                    result=cached.payload,
                    timeout_class=timeout_class,
                    replayed=True,
                )

        project_path = request.get("project_path")
        if project_path is not None and not isinstance(project_path, str):
            project_path = None

        attempts = 0
        backoff_ms = TIMEOUT_CLASS_BACKOFF_MS.get(timeout_class, 250)
        last_error: Optional[Exception] = None

        while attempts <= retries:
            attempts += 1
            try:
                with self._lock:
                    result = self._orchestrator.run(
                        goal.strip(),
                        project_path=project_path,
                        request_id=request_id,
                    )
                response = self._ok(
                    request_id=request_id,
                    op=op,
                    started=started,
                    result=result,
                    timeout_class=timeout_class,
                    attempts=attempts,
                )
                if isinstance(idempotency_key, str) and idempotency_key:
                    self._idempotency[idempotency_key] = CachedResponse(
                        payload=result,
                        expires_at=time.monotonic() + self.cache_ttl_s,
                    )
                return response
            except ToolClientError as exc:
                last_error = exc
                if attempts > retries:
                    break
                time.sleep(backoff_ms / 1000.0)
                backoff_ms = min(backoff_ms * 2, 1500)

        return self._error(
            request_id=request_id,
            op=op,
            started=started,
            timeout_class=timeout_class,
            code="tool_client_error",
            message=str(last_error) if last_error else "unknown run_goal failure",
            attempts=attempts,
        )


class AgentRequestHandler(socketserver.StreamRequestHandler):
    def handle(self) -> None:
        while True:
            line = self.rfile.readline()
            if not line:
                return
            raw = line.strip()
            if not raw:
                continue
            try:
                request = json.loads(raw.decode("utf-8"))
            except json.JSONDecodeError as exc:
                response = {
                    "ok": False,
                    "request_id": f"daemon_{uuid.uuid4().hex[:12]}",
                    "op": "unknown",
                    "timeout_class": "standard",
                    "attempts": 1,
                    "duration_ms": 0,
                    "error": {
                        "code": "invalid_json",
                        "message": str(exc),
                    },
                }
            else:
                if not isinstance(request, dict):
                    response = {
                        "ok": False,
                        "request_id": f"daemon_{uuid.uuid4().hex[:12]}",
                        "op": "unknown",
                        "timeout_class": "standard",
                        "attempts": 1,
                        "duration_ms": 0,
                        "error": {
                            "code": "invalid_request",
                            "message": "request payload must be a JSON object",
                        },
                    }
                else:
                    response = self.server.runtime.handle_request(request)  # type: ignore[attr-defined]

            self.wfile.write((json.dumps(response, ensure_ascii=True) + "\n").encode("utf-8"))
            self.wfile.flush()


class ThreadedAgentServer(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True

    def __init__(self, server_address: tuple[str, int], runtime: AgentRuntime) -> None:
        self.runtime = runtime
        super().__init__(server_address, AgentRequestHandler)


def main() -> int:
    parser = argparse.ArgumentParser(description="Persistent LMMS agent daemon (phase 1 runtime split)")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=7781)
    parser.add_argument("--sample-root", action="append", default=[])
    parser.add_argument("--cache-ttl-s", type=float, default=300.0)
    args = parser.parse_args()

    runtime = AgentRuntime(sample_roots=args.sample_root, cache_ttl_s=max(1.0, args.cache_ttl_s))
    with ThreadedAgentServer((args.host, args.port), runtime) as server:
        print(
            json.dumps(
                {
                    "ok": True,
                    "message": "lmms-agentd started",
                    "host": args.host,
                    "port": args.port,
                },
                ensure_ascii=True,
            ),
            flush=True,
        )
        try:
            server.serve_forever(poll_interval=0.25)
        except KeyboardInterrupt:
            pass
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
