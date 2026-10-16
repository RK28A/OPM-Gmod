"""Tests du moteur sans Windows : WinDivert est remplacé par un faux handle.

    cd LagControl && python -m unittest discover tests
"""

import os
import queue
import sys
import threading
import time
import unittest
from types import SimpleNamespace
from unittest import mock

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from lagcontrol import engine as engine_mod  # noqa: E402
from lagcontrol.config import Settings  # noqa: E402
from lagcontrol.engine import LagEngine  # noqa: E402


class FakeHandle:
    def __init__(self):
        self.incoming: queue.Queue = queue.Queue()
        self.sent = []
        self.closed = False
        self.lock = threading.Lock()

    def open(self):
        return self

    def recv(self, timeout=None):
        if self.closed:
            raise OSError("closed")
        try:
            return self.incoming.get(timeout=timeout)
        except queue.Empty:
            raise TimeoutError() from None

    def send(self, packet):
        if self.closed:
            raise OSError("closed")
        with self.lock:
            self.sent.append((time.perf_counter(), packet))

    def close(self):
        self.closed = True


def pkt(port, outbound=True, tcp=True, tag=None):
    return SimpleNamespace(
        is_outbound=outbound,
        src_port=port if outbound else 443,
        dst_port=443 if outbound else port,
        tcp=object() if tcp else None,
        tag=tag,
    )


class EngineTest(unittest.TestCase):
    def setUp(self):
        self.handle = FakeHandle()
        self.tracker = SimpleNamespace(ports=frozenset({(True, 5000), (False, 6000)}), target="game.exe")
        fake_pydivert = SimpleNamespace(WinDivert=lambda _f: self.handle)
        patches = [
            mock.patch.object(engine_mod, "pydivert", fake_pydivert),
            mock.patch.object(LagEngine, "available", staticmethod(lambda: True)),
        ]
        for p in patches:
            p.start()
            self.addCleanup(p.stop)
        self.engine = LagEngine(self.tracker)
        self.addCleanup(self.engine.emergency_stop)

    def wait_sent(self, n, timeout=2.0):
        deadline = time.time() + timeout
        while time.time() < deadline:
            if len(self.handle.sent) >= n:
                return
            time.sleep(0.005)
        self.fail(f"{len(self.handle.sent)} paquets envoyés sur {n} attendus")

    def test_other_apps_are_not_delayed(self):
        self.engine.configure(300, 0, 0, "both")
        self.engine.start()
        t0 = time.perf_counter()
        self.handle.incoming.put(pkt(1234, tag="other"))
        self.wait_sent(1)
        self.assertLess(self.handle.sent[0][0] - t0, 0.1)
        self.assertEqual(self.engine.passed, 1)
        self.assertEqual(self.engine.delayed, 0)

    def test_target_is_delayed(self):
        self.engine.configure(200, 0, 0, "both")
        self.engine.start()
        t0 = time.perf_counter()
        self.handle.incoming.put(pkt(5000, tag="game"))
        self.handle.incoming.put(pkt(6000, outbound=False, tcp=False, tag="game-udp-in"))
        self.wait_sent(2)
        for sent_at, _ in self.handle.sent:
            self.assertGreaterEqual(sent_at - t0, 0.19)
        self.assertEqual(self.engine.delayed, 2)

    def test_protocol_must_match(self):
        self.engine.configure(300, 0, 0, "both")
        self.engine.start()
        self.handle.incoming.put(pkt(5000, tcp=False))  # UDP 5000 n'appartient pas à la cible
        self.wait_sent(1, timeout=0.2)
        self.assertEqual(self.engine.delayed, 0)

    def test_direction_filter(self):
        self.engine.configure(300, 0, 0, "out")
        self.engine.start()
        self.handle.incoming.put(pkt(5000, outbound=False))
        self.wait_sent(1, timeout=0.2)
        self.assertEqual(self.engine.delayed, 0)

    def test_order_is_preserved_with_jitter(self):
        self.engine.configure(60, 50, 0, "both")
        self.engine.start()
        for i in range(50):
            self.handle.incoming.put(pkt(5000, tag=i))
        self.wait_sent(50)
        self.assertEqual([p.tag for _, p in self.handle.sent], list(range(50)))

    def test_loss(self):
        self.engine.configure(0, 0, 100, "both")
        self.engine.start()
        for _ in range(10):
            self.handle.incoming.put(pkt(5000))
        deadline = time.time() + 1
        while self.engine.dropped < 10 and time.time() < deadline:
            time.sleep(0.01)
        self.assertEqual(self.engine.dropped, 10)
        self.assertEqual(self.handle.sent, [])

    def test_stop_releases_everything_and_closes(self):
        self.engine.configure(5000, 0, 0, "both")
        self.engine.start()
        for i in range(5):
            self.handle.incoming.put(pkt(5000, tag=i))
        deadline = time.time() + 1
        while self.engine.delayed < 5 and time.time() < deadline:
            time.sleep(0.005)
        self.assertEqual(self.handle.sent, [])
        t0 = time.perf_counter()
        self.engine.stop()
        self.assertLess(time.perf_counter() - t0, 1.0)
        self.assertEqual([p.tag for _, p in self.handle.sent], list(range(5)))
        self.assertTrue(self.handle.closed)
        self.assertFalse(self.engine.active)
        self.assertEqual(self.engine.queued, 0)

    def test_emergency_stop_never_raises(self):
        self.engine.start()
        self.handle.close = mock.Mock(side_effect=OSError("boom"))
        self.engine.emergency_stop()
        self.assertFalse(self.engine.active)
        self.engine.emergency_stop()  # idempotent


class SettingsTest(unittest.TestCase):
    def test_round_trip_and_sanitize(self):
        import tempfile

        with tempfile.TemporaryDirectory() as tmp, mock.patch.dict(os.environ, {"APPDATA": tmp}):
            s = Settings(target=" game.exe ", delay_ms=99999, loss_pct=-3, direction="nope")
            s.save()
            loaded = Settings.load()
            self.assertEqual(loaded.target, "game.exe")
            self.assertEqual(loaded.delay_ms, 10_000)
            self.assertEqual(loaded.loss_pct, 0.0)
            self.assertEqual(loaded.direction, "both")


if __name__ == "__main__":
    unittest.main()
