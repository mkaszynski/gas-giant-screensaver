"""Monitor queries stay read-only, bounded and promptly close the IPC socket."""

import importlib.util
import json
from pathlib import Path
import unittest
from unittest.mock import MagicMock, patch

SOURCE = Path(__file__).resolve().parents[1] / "hyprland_ipc.py"
spec = importlib.util.spec_from_file_location("hyprland_ipc", SOURCE)
ipc = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ipc)

ENV = {
    "XDG_RUNTIME_DIR": "/private-runtime",
    "HYPRLAND_INSTANCE_SIGNATURE": "test-instance",
}


class MonitorQueryTests(unittest.TestCase):
    def fake(self, chunks):
        connection = MagicMock()
        connection.__enter__.return_value = connection
        connection.__exit__.return_value = False
        connection.recv.side_effect = chunks
        return connection, patch.object(ipc.socket, "socket", return_value=connection)

    def test_chunked_json_uses_only_the_monitor_request(self):
        connection, factory = self.fake([b"[", b'{"name":"eDP-1"}', b"]", b""])
        with factory:
            self.assertEqual(ipc.monitors(ENV), [{"name": "eDP-1"}])
        connection.connect.assert_called_once_with(
            "/private-runtime/hypr/test-instance/.socket.sock"
        )
        connection.sendall.assert_called_once_with(b"j/monitors")
        connection.settimeout.assert_called_once_with(0.25)
        connection.__exit__.assert_called_once()

    def test_utf8_may_cross_socket_chunks(self):
        value = [{"name": "écran"}]
        raw = json.dumps(value, ensure_ascii=False).encode()
        at = raw.index(b"\xc3") + 1
        connection, factory = self.fake([raw[:at], raw[at:], b""])
        with factory:
            self.assertEqual(ipc.monitors(ENV), value)

    def test_empty_monitor_list_is_returned_for_the_host_guard_to_reject(self):
        connection, factory = self.fake([b"[]", b""])
        with factory:
            self.assertEqual(ipc.monitors(ENV), [])

    def test_truncated_and_invalid_json_fail_after_closing_the_socket(self):
        for raw in [b"", b"[{", b"error"]:
            with self.subTest(raw=raw):
                connection, factory = self.fake([raw, b""])
                with factory, self.assertRaises(ValueError):
                    ipc.monitors(ENV)
                connection.__exit__.assert_called_once()

    def test_wrong_json_shapes_fail(self):
        for raw in [b"{}", b"1", b"[1]", b"[null]"]:
            with self.subTest(raw=raw):
                connection, factory = self.fake([raw, b""])
                with factory, self.assertRaises(ValueError):
                    ipc.monitors(ENV)
                connection.__exit__.assert_called_once()

    def test_timeout_closes_connection_without_retrying(self):
        connection, factory = self.fake([TimeoutError("late compositor")])
        with factory, self.assertRaises(TimeoutError):
            ipc.monitors(ENV, timeout=0.1)
        connection.settimeout.assert_called_once_with(0.1)
        self.assertEqual(connection.recv.call_count, 1)
        connection.__exit__.assert_called_once()

    def test_response_size_is_bounded(self):
        connection, factory = self.fake([b" " * 65536] * 17)
        with factory, self.assertRaisesRegex(ValueError, "1 MiB"):
            ipc.monitors(ENV)
        self.assertEqual(connection.recv.call_count, 17)
        connection.__exit__.assert_called_once()

    def test_missing_environment_does_not_open_a_socket(self):
        with patch.object(ipc.socket, "socket") as factory, self.assertRaises(KeyError):
            ipc.monitors({})
        factory.assert_not_called()


if __name__ == "__main__":
    unittest.main()
