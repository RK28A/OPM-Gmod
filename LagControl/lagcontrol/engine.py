"""Moteur de latence : intercepte les paquets avec WinDivert et retarde
uniquement ceux qui appartiennent à l'application ciblée.

Fonctionnement :
  * un thread de capture reçoit chaque paquet TCP/UDP de la machine ;
  * si son port local appartient à la cible (voir ``PortTracker``) il est mis
    dans une file triée par heure de sortie, sinon il est réinjecté aussitôt ;
  * un thread d'envoi réinjecte les paquets retardés quand leur heure arrive.

Fermer le handle WinDivert rend immédiatement le réseau normal : c'est ce que
fait l'arrêt (et l'arrêt d'urgence), après avoir relâché les paquets en attente.
Si l'application plante, Windows ferme le handle tout seul.
"""

from __future__ import annotations

import heapq
import itertools
import logging
import random
import sys
import threading
import time

from .processes import PortTracker

log = logging.getLogger(__name__)

try:
    import pydivert
except Exception as exc:  # pas Windows, ou paquet absent
    pydivert = None
    _IMPORT_ERROR: Exception | None = exc
else:
    _IMPORT_ERROR = None


class EngineError(RuntimeError):
    pass


def _set_timer_resolution(enable: bool) -> None:
    """Passe l'horloge Windows à 1 ms, sinon les attentes sont arrondies à ~15 ms."""
    if sys.platform != "win32":
        return
    try:
        import ctypes

        winmm = ctypes.windll.winmm
        (winmm.timeBeginPeriod if enable else winmm.timeEndPeriod)(1)
    except Exception:
        pass


class LagEngine:
    FILTER = "(tcp or udp) and !loopback"
    RECV_TIMEOUT = 0.25  # s — permet au thread de capture de voir l'arrêt

    def __init__(self, tracker: PortTracker) -> None:
        self.tracker = tracker

        # Réglages lus à chaque paquet : simples affectations, pas de verrou.
        self.delay_ms = 300
        self.jitter_ms = 0
        self.loss_pct = 0.0
        self.direction = "both"

        self.delayed = 0
        self.dropped = 0
        self.passed = 0

        self._handle = None
        self._running = False
        self._threads: list[threading.Thread] = []
        self._heap: list = []
        self._cv = threading.Condition()
        self._seq = itertools.count()
        self._last_release = {True: 0.0, False: 0.0}
        self._op_lock = threading.Lock()

    # ------------------------------------------------------------------ état
    @staticmethod
    def available() -> bool:
        return pydivert is not None and sys.platform == "win32"

    @staticmethod
    def unavailable_reason() -> str:
        if sys.platform != "win32":
            return "LagControl ne fonctionne que sous Windows."
        return f"Le module pydivert est introuvable ({_IMPORT_ERROR}). Lancez « pip install -r requirements.txt »."

    @property
    def active(self) -> bool:
        return self._running

    @property
    def queued(self) -> int:
        return len(self._heap)

    def configure(self, delay_ms: int, jitter_ms: int, loss_pct: float, direction: str) -> None:
        self.delay_ms = max(0, int(delay_ms))
        self.jitter_ms = max(0, int(jitter_ms))
        self.loss_pct = min(100.0, max(0.0, float(loss_pct)))
        self.direction = direction if direction in ("in", "out", "both") else "both"

    def reset_stats(self) -> None:
        self.delayed = self.dropped = self.passed = 0

    # ------------------------------------------------------- start / stop
    def start(self) -> None:
        with self._op_lock:
            if self._running:
                return
            if not self.available():
                raise EngineError(self.unavailable_reason())
            try:
                handle = pydivert.WinDivert(self.FILTER)
                handle.open()
            except OSError as exc:
                raise EngineError(
                    "Impossible d'ouvrir le pilote WinDivert.\n"
                    "Lancez LagControl en tant qu'administrateur.\n\n"
                    f"Détail : {exc}"
                ) from exc

            _set_timer_resolution(True)
            self._handle = handle
            self._last_release = {True: 0.0, False: 0.0}
            self._running = True
            self._threads = [
                threading.Thread(target=self._recv_loop, args=(handle,), name="LagRecv", daemon=True),
                threading.Thread(target=self._send_loop, args=(handle,), name="LagSend", daemon=True),
            ]
            for t in self._threads:
                t.start()
            log.info("Lag activé (%s)", self.tracker.target)

    def stop(self, timeout: float = 2.0) -> None:
        """Relâche tous les paquets en attente puis ferme le pilote."""
        if not self._op_lock.acquire(timeout=timeout):
            # Ne devrait jamais arriver ; on force quand même la fermeture.
            log.error("Verrou du moteur bloqué, fermeture forcée")
            self._force_close()
            return
        try:
            if not self._running:
                return
            self._running = False
            handle = self._handle

            released = self._flush(handle)
            for t in self._threads:
                if t is not threading.current_thread():
                    t.join(timeout=1.0)
            self._threads = []
            released += self._flush(handle)  # paquet arrivé pendant l'arrêt
            self._close_handle()
            _set_timer_resolution(False)
            log.info("Lag désactivé (%d paquets relâchés)", released)
        finally:
            self._op_lock.release()

    def _flush(self, handle) -> int:
        with self._cv:
            pending = [item[2] for item in sorted(self._heap)]
            self._heap.clear()
            self._cv.notify_all()
        for packet in pending:
            self._safe_send(handle, packet)
        return len(pending)

    def emergency_stop(self) -> None:
        """Arrêt qui ne lève jamais d'exception : le réseau doit revenir quoi qu'il arrive."""
        try:
            self.stop(timeout=1.0)
        except Exception:
            log.exception("Erreur pendant l'arrêt d'urgence")
            self._force_close()

    def _force_close(self) -> None:
        self._running = False
        with self._cv:
            self._heap.clear()
            self._cv.notify_all()
        self._close_handle()
        _set_timer_resolution(False)

    def _close_handle(self) -> None:
        handle, self._handle = self._handle, None
        if handle is not None:
            try:
                handle.close()
            except Exception:
                pass

    # ------------------------------------------------------------ threads
    def _safe_send(self, handle, packet) -> None:
        try:
            handle.send(packet)
        except Exception as exc:
            log.debug("Échec de réinjection : %s", exc)

    def _recv_loop(self, handle) -> None:
        errors = 0
        while self._running:
            try:
                packet = handle.recv(timeout=self.RECV_TIMEOUT)
            except TimeoutError:
                continue
            except Exception as exc:
                if not self._running:
                    break
                errors += 1
                log.debug("Erreur de capture : %s", exc)
                if errors > 100:
                    log.error("Trop d'erreurs de capture, arrêt du moteur")
                    threading.Thread(target=self.emergency_stop, daemon=True).start()
                    break
                time.sleep(0.005)
                continue
            errors = 0
            self._process(handle, packet)

    def _process(self, handle, packet) -> None:
        outbound = bool(packet.is_outbound)
        local_port = packet.src_port if outbound else packet.dst_port
        is_tcp = packet.tcp is not None
        direction = self.direction

        targeted = (
            self._running
            and (is_tcp, local_port) in self.tracker.ports
            and (direction == "both" or (direction == "out") == outbound)
        )
        if not targeted:
            self._safe_send(handle, packet)
            self.passed += 1
            return

        if self.loss_pct > 0 and random.random() * 100.0 < self.loss_pct:
            self.dropped += 1
            return

        delay = self.delay_ms
        if self.jitter_ms:
            delay += random.uniform(-self.jitter_ms, self.jitter_ms)
        now = time.perf_counter()
        # max() garde l'ordre des paquets malgré la variation (pas de réordonnancement TCP).
        release = max(now + max(0.0, delay) / 1000.0, self._last_release[outbound])
        self._last_release[outbound] = release

        if release <= now:
            self._safe_send(handle, packet)
            self.passed += 1
            return

        item = (release, next(self._seq), packet)
        with self._cv:
            heapq.heappush(self._heap, item)
            if self._heap[0] is item:  # nouvelle échéance la plus proche : réveiller l'envoi
                self._cv.notify()
        self.delayed += 1

    def _send_loop(self, handle) -> None:
        while True:
            with self._cv:
                while self._running:
                    if self._heap:
                        wait = self._heap[0][0] - time.perf_counter()
                        if wait <= 0:
                            break
                        self._cv.wait(wait)
                    else:
                        self._cv.wait()
                if not self._running:
                    return
                now = time.perf_counter()
                due = []
                while self._heap and self._heap[0][0] <= now:
                    due.append(heapq.heappop(self._heap)[2])
            for packet in due:
                self._safe_send(handle, packet)
