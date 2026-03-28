#!/usr/bin/env python3
import json
import socket
from typing import Any, Dict


class LmmsClient:
    def __init__(self, host: str = "127.0.0.1", port: int = 7777, timeout: float = 5.0):
        self.host = host
        self.port = port
        self.timeout = timeout

    def send_text(self, command: str) -> str:
        return self._send(command.strip())

    def send_json(self, payload: Dict[str, Any]) -> str:
        return self._send(json.dumps(payload))

    def _send(self, body: str) -> str:
        with socket.create_connection((self.host, self.port), timeout=self.timeout) as sock:
            sock.sendall((body + "\n").encode("utf-8"))
            sock.shutdown(socket.SHUT_WR)
            return sock.recv(65536).decode("utf-8", errors="replace").strip()
