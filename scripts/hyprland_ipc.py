"""Bounded read-only monitor query without spawning hyprctl per sample.

Protocol: https://wiki.hypr.land/0.52.0/IPC/
The compositor handles each request synchronously. Send immediately and always
close the socket, including on timeout, parse failure or an oversized response.
"""

import json
import os
from pathlib import Path
import socket


def monitors(environ=None, timeout=0.25):
    env = os.environ if environ is None else environ
    path = (
        Path(env["XDG_RUNTIME_DIR"])
        / "hypr"
        / env["HYPRLAND_INSTANCE_SIGNATURE"]
        / ".socket.sock"
    )
    chunks = []
    total = 0
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
        connection.settimeout(timeout)
        connection.connect(str(path))
        connection.sendall(b"j/monitors")
        while True:
            chunk = connection.recv(65536)
            if not chunk:
                break
            total += len(chunk)
            if total > 1024 * 1024:
                raise ValueError("Monitor IPC response exceeds 1 MiB")
            chunks.append(chunk)
    result = json.loads(b"".join(chunks))
    if not isinstance(result, list) or any(
        not isinstance(item, dict) for item in result
    ):
        raise ValueError("Unexpected monitor IPC response")
    return result
