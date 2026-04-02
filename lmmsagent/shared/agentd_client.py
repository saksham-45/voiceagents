from __future__ import annotations

import json
import socket
import uuid
from dataclasses import dataclass
from typing import Any, Dict


class AgentDaemonError(RuntimeError):
    pass


@dataclass
class AgentDaemonClient:
    host: str = "127.0.0.1"
    port: int = 7781
    timeout_s: float = 30.0

    def _exchange(self, payload: Dict[str, Any]) -> Dict[str, Any]:
        raw = (json.dumps(payload, ensure_ascii=True) + "\n").encode("utf-8")
        try:
            with socket.create_connection((self.host, self.port), timeout=self.timeout_s) as conn:
                conn.sendall(raw)
                conn.settimeout(self.timeout_s)
                buffer = b""
                while b"\n" not in buffer:
                    chunk = conn.recv(4096)
                    if not chunk:
                        break
                    buffer += chunk
        except OSError as exc:
            raise AgentDaemonError(
                f"Could not connect to lmms-agentd at {self.host}:{self.port}: {exc}"
            ) from exc

        if not buffer:
            raise AgentDaemonError("No response from lmms-agentd")

        line = buffer.split(b"\n", 1)[0]
        try:
            response = json.loads(line.decode("utf-8"))
        except json.JSONDecodeError as exc:
            raise AgentDaemonError(f"Invalid lmms-agentd JSON response: {line!r}") from exc
        if not isinstance(response, dict):
            raise AgentDaemonError("lmms-agentd returned a non-object response")
        return response

    def call(self, op: str, payload: Dict[str, Any] | None = None) -> Dict[str, Any]:
        body = dict(payload or {})
        body.setdefault("request_id", f"client_{uuid.uuid4().hex[:12]}")
        body["op"] = op
        response = self._exchange(body)
        if not response.get("ok", False):
            error = response.get("error") or {}
            code = error.get("code") or "daemon_failed"
            message = error.get("message") or "unknown daemon error"
            raise AgentDaemonError(f"{code}: {message}")
        return response

    def health(self) -> Dict[str, Any]:
        return self.call("health").get("result", {})

    def warmup(self, project_path: str | None = None) -> Dict[str, Any]:
        return self.call("warmup", {"project_path": project_path}).get("result", {})

    def run_goal(
        self,
        goal: str,
        *,
        project_path: str | None = None,
        timeout_class: str = "interactive",
        retries: int = 1,
        idempotency_key: str | None = None,
    ) -> Dict[str, Any]:
        payload: Dict[str, Any] = {
            "goal": goal,
            "project_path": project_path,
            "timeout_class": timeout_class,
            "retries": retries,
        }
        if idempotency_key:
            payload["idempotency_key"] = idempotency_key
        response = self.call("run_goal", payload)
        result = response.get("result")
        if not isinstance(result, dict):
            raise AgentDaemonError("lmms-agentd returned invalid run_goal result")
        return result
