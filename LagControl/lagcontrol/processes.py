"""Liste des applications et suivi des ports réseau de l'application ciblée."""

from __future__ import annotations

import logging
import os
import socket
import threading
from collections import Counter
from dataclasses import dataclass, field

import psutil

log = logging.getLogger(__name__)

# Processus système qu'on ne propose jamais comme cible.
_HIDDEN = {"system idle process", "system", "registry", "memory compression", "secure system"}


@dataclass
class AppEntry:
    name: str
    exe: str = ""
    pids: list[int] = field(default_factory=list)
    connections: int = 0

    @property
    def key(self) -> str:
        return self.name.lower()


def _connections_by_pid() -> Counter:
    counts: Counter = Counter()
    try:
        for conn in psutil.net_connections(kind="inet"):
            if conn.pid:
                counts[conn.pid] += 1
    except (psutil.AccessDenied, OSError) as exc:
        log.debug("net_connections indisponible : %s", exc)
    return counts


def list_apps() -> list[AppEntry]:
    """Applications en cours, regroupées par nom d'exécutable."""
    conns = _connections_by_pid()
    own_pid = os.getpid()
    apps: dict[str, AppEntry] = {}

    for proc in psutil.process_iter(["pid", "name", "exe"]):
        info = proc.info
        name = info.get("name") or ""
        pid = info.get("pid") or 0
        if not name or pid in (0, 4, own_pid) or name.lower() in _HIDDEN:
            continue
        entry = apps.get(name.lower())
        if entry is None:
            entry = apps[name.lower()] = AppEntry(name=name, exe=info.get("exe") or "")
        entry.pids.append(pid)
        entry.connections += conns.get(pid, 0)

    return sorted(apps.values(), key=lambda a: (a.connections == 0, a.name.lower()))


class PortTracker(threading.Thread):
    """Tient à jour, en arrière-plan, les ports locaux ouverts par l'application ciblée.

    ``ports`` est un frozenset de tuples ``(is_tcp, port)`` remplacé atomiquement,
    donc lisible sans verrou depuis le thread de capture.
    """

    def __init__(self, interval: float = 0.35, pid_refresh_every: int = 6) -> None:
        super().__init__(name="PortTracker", daemon=True)
        self.interval = interval
        self.pid_refresh_every = pid_refresh_every
        self.ports: frozenset[tuple[bool, int]] = frozenset()
        self.pids: frozenset[int] = frozenset()
        self._target = ""
        self._stop = threading.Event()
        self._wake = threading.Event()
        self._tick = 0

    @property
    def target(self) -> str:
        return self._target

    def set_target(self, name: str) -> None:
        self._target = (name or "").strip().lower()
        self.pids = frozenset()
        self.ports = frozenset()
        self._tick = 0
        self._wake.set()

    def stop(self) -> None:
        self._stop.set()
        self._wake.set()

    def run(self) -> None:
        while not self._stop.is_set():
            try:
                self.refresh()
            except Exception:  # le suivi ne doit jamais mourir
                log.exception("Erreur pendant le suivi des ports")
            self._wake.wait(self.interval)
            self._wake.clear()

    def refresh(self) -> None:
        target = self._target
        if not target:
            self.pids = frozenset()
            self.ports = frozenset()
            return

        pids = self.pids
        if self._tick % self.pid_refresh_every == 0 or not pids:
            pids = frozenset(
                proc.pid
                for proc in psutil.process_iter(["name"])
                if (proc.info.get("name") or "").lower() == target
            )
        self._tick += 1

        ports = set()
        if pids:
            try:
                for conn in psutil.net_connections(kind="inet"):
                    if conn.pid in pids and conn.laddr:
                        ports.add((conn.type == socket.SOCK_STREAM, conn.laddr.port))
            except (psutil.AccessDenied, OSError) as exc:
                log.debug("net_connections indisponible : %s", exc)
                return

        if target == self._target:  # la cible n'a pas changé entre-temps
            self.pids = pids
            self.ports = frozenset(ports)
