import json
import socket
import threading
import time
from collections import deque
import os


class DataSource:
    def __init__(self,
            host: str = os.environ.get("ENGINE_HOST", "localhost"),
            port: int = int(os.environ.get("ENGINE_PORT", "8765"))):
        self.host = host
        self.port = port
        self._buffer: deque = deque(maxlen=1500)
        self._lock = threading.Lock()
        self.last_snapshot_ts: float = 0.0
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()

    def _run(self):
        backoff = 1.0
        while True:
            try:
                with socket.create_connection((self.host, self.port), timeout=5) as sock:
                    backoff = 1.0
                    with self._lock:
                        self._buffer.clear()  # reset on new connection
                    buf = ""
                    while True:
                        chunk = sock.recv(4096).decode("utf-8", errors="replace")
                        if not chunk:
                            break
                        if len(buf) > 8192:
                            break  # triggers reconnect via the outer except
                        buf += chunk
                        while "\n" in buf:
                            line, buf = buf.split("\n", 1)
                            line = line.strip()
                            if not line:
                                continue
                            try:
                                snapshot = json.loads(line)
                                with self._lock:
                                    self._buffer.append(snapshot)
                                    self.last_snapshot_ts = time.time()
                            except json.JSONDecodeError:
                                pass
            except (OSError, ConnectionError, socket.timeout):
                time.sleep(backoff)
                backoff = min(backoff * 2, 5.0)

    def get_snapshots(self) -> list:
        with self._lock:
            return list(self._buffer)

    def get_latest(self) -> dict | None:
        with self._lock:
            return self._buffer[-1] if self._buffer else None