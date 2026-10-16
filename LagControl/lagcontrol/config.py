"""Réglages persistants (JSON dans %APPDATA%\\LagControl)."""

from __future__ import annotations

import json
import logging
import os
from dataclasses import asdict, dataclass, fields

from . import APP_NAME

log = logging.getLogger(__name__)

DIRECTIONS = ("in", "out", "both")


def config_dir() -> str:
    base = os.environ.get("APPDATA") or os.path.join(os.path.expanduser("~"), ".config")
    path = os.path.join(base, APP_NAME)
    os.makedirs(path, exist_ok=True)
    return path


def _clamp(value, lo, hi):
    return max(lo, min(hi, value))


@dataclass
class Settings:
    target: str = ""
    delay_ms: int = 300
    jitter_ms: int = 0
    loss_pct: float = 0.0
    direction: str = "both"
    toggle_hotkey: str = "f8"
    panic_hotkey: str = "ctrl+f12"
    hold_mode: bool = False
    auto_off_s: int = 0
    sounds: bool = True
    always_on_top: bool = False
    network_only: bool = True

    @property
    def path(self) -> str:
        return os.path.join(config_dir(), "config.json")

    def sanitize(self) -> None:
        self.delay_ms = int(_clamp(self.delay_ms, 0, 10_000))
        self.jitter_ms = int(_clamp(self.jitter_ms, 0, 2_000))
        self.loss_pct = float(_clamp(self.loss_pct, 0.0, 100.0))
        self.auto_off_s = int(_clamp(self.auto_off_s, 0, 3_600))
        if self.direction not in DIRECTIONS:
            self.direction = "both"
        self.toggle_hotkey = (self.toggle_hotkey or "f8").strip().lower()
        self.panic_hotkey = (self.panic_hotkey or "ctrl+f12").strip().lower()
        self.target = (self.target or "").strip()

    @classmethod
    def load(cls) -> "Settings":
        settings = cls()
        try:
            with open(settings.path, encoding="utf-8") as fh:
                raw = json.load(fh)
        except FileNotFoundError:
            return settings
        except (OSError, ValueError) as exc:
            log.warning("Config illisible, valeurs par défaut utilisées : %s", exc)
            return settings

        if isinstance(raw, dict):
            for f in fields(cls):
                if f.name not in raw:
                    continue
                default = getattr(settings, f.name)
                try:
                    value = type(default)(raw[f.name])
                except (TypeError, ValueError):
                    continue
                setattr(settings, f.name, value)
        settings.sanitize()
        return settings

    def save(self) -> None:
        self.sanitize()
        tmp = self.path + ".tmp"
        try:
            with open(tmp, "w", encoding="utf-8") as fh:
                json.dump(asdict(self), fh, indent=2, ensure_ascii=False)
            os.replace(tmp, self.path)
        except OSError as exc:
            log.warning("Impossible d'enregistrer la config : %s", exc)
