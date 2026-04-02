from __future__ import annotations

import json
import socket
import time
from dataclasses import dataclass
from typing import Any, Dict, Tuple


class ToolClientError(RuntimeError):
    pass


@dataclass
class ToolClient:
    host: str = "127.0.0.1"
    port: int = 7777
    timeout_s: float = 5.0

    def _exchange(self, payload: Dict[str, Any]) -> Tuple[Dict[str, Any], int]:
        raw = (json.dumps(payload, ensure_ascii=True) + "\n").encode("utf-8")
        started = time.monotonic()
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
            raise ToolClientError(
                f"Could not connect to AgentControl server at {self.host}:{self.port}: {exc}"
            ) from exc
        if not buffer:
            raise ToolClientError("No response from AgentControl tool server")
        line = buffer.split(b"\n", 1)[0]
        try:
            response = json.loads(line.decode("utf-8"))
        except json.JSONDecodeError as exc:
            raise ToolClientError(f"Invalid JSON response: {line!r}") from exc
        if not isinstance(response, dict):
            raise ToolClientError("Tool server returned a non-object response")
        latency_ms = int((time.monotonic() - started) * 1000)
        return response, latency_ms

    def call_tool(self, tool: str, args: Dict[str, Any] | None = None) -> Dict[str, Any]:
        response, latency_ms = self._exchange({"tool": tool, "args": args or {}})
        response["_transport"] = {
            "host": self.host,
            "port": self.port,
            "latency_ms": latency_ms,
        }
        if not response.get("ok", False):
            code = response.get("error_code") or "tool_failed"
            message = response.get("error_message") or "unknown tool failure"
            raise ToolClientError(f"{code}: {message}")
        return response

    def get_project_state(self) -> Dict[str, Any]:
        return self.call_tool("get_project_state")["result"]
