"""Petits bips de confirmation (utile quand le jeu est en plein écran)."""

from __future__ import annotations

import threading

try:
    import winsound
except ImportError:  # pas Windows
    winsound = None

_PATTERNS = {
    "on": [(880, 70), (1320, 90)],
    "off": [(1320, 70), (660, 90)],
    "panic": [(1500, 80), (1500, 80), (1500, 80)],
    "error": [(220, 180)],
}


def play(kind: str) -> None:
    if winsound is None or kind not in _PATTERNS:
        return

    def worker():
        for freq, duration in _PATTERNS[kind]:
            try:
                winsound.Beep(freq, duration)
            except RuntimeError:
                return

    threading.Thread(target=worker, name="Beep", daemon=True).start()
