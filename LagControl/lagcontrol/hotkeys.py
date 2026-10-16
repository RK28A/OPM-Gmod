"""Raccourcis clavier globaux (fonctionnent même quand le jeu a le focus)."""

from __future__ import annotations

import logging
import queue
import threading

log = logging.getLogger(__name__)

try:
    import keyboard
except Exception as exc:  # pas Windows / pas de droits
    keyboard = None
    log.warning("Module keyboard indisponible : %s", exc)


def pretty(hotkey: str) -> str:
    return "+".join(part.strip().upper() for part in hotkey.split("+")) if hotkey else "—"


class HotkeyManager:
    """Pousse des événements dans une file lue par la boucle Tk (thread-safe)."""

    def __init__(self, events: queue.Queue) -> None:
        self.events = events
        self._handles: list = []
        self._recording = False

    @property
    def available(self) -> bool:
        return keyboard is not None

    def bind(self, toggle: str, panic: str, hold_mode: bool) -> list[str]:
        """(Re)lie les raccourcis. Renvoie la liste des erreurs éventuelles."""
        self.unbind()
        if keyboard is None:
            return ["Raccourcis globaux indisponibles (module keyboard absent)."]
        errors = []
        if not hold_mode:
            errors += self._add(toggle, "toggle")
        errors += self._add(panic, "panic")
        return errors

    def _add(self, hotkey: str, event: str) -> list[str]:
        if not hotkey:
            return []
        try:
            handle = keyboard.add_hotkey(hotkey, self.events.put, args=(event,), suppress=False)
        except (ValueError, ImportError, OSError) as exc:
            return [f"Raccourci « {pretty(hotkey)} » invalide : {exc}"]
        self._handles.append(handle)
        return []

    def unbind(self) -> None:
        if keyboard is None:
            return
        for handle in self._handles:
            try:
                keyboard.remove_hotkey(handle)
            except (KeyError, ValueError):
                pass
        self._handles.clear()

    def is_pressed(self, hotkey: str) -> bool:
        if keyboard is None or not hotkey:
            return False
        try:
            return keyboard.is_pressed(hotkey)
        except (ValueError, ImportError, OSError):
            return False

    def record(self, slot: str) -> bool:
        """Enregistre la prochaine combinaison pressée ; résultat via ('recorded', slot, hk)."""
        if keyboard is None or self._recording:
            return False
        self._recording = True

        def worker():
            try:
                hk = keyboard.read_hotkey(suppress=False)
            except Exception as exc:
                log.warning("Enregistrement du raccourci impossible : %s", exc)
                hk = ""
            finally:
                self._recording = False
            self.events.put(("recorded", slot, hk))

        threading.Thread(target=worker, name="HotkeyRecord", daemon=True).start()
        return True

    def shutdown(self) -> None:
        if keyboard is None:
            return
        try:
            keyboard.unhook_all()
        except Exception:
            pass
