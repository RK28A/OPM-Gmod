"""Point d'entrée de LagControl.

Sous Windows, l'application se relance automatiquement en administrateur :
le pilote WinDivert et les raccourcis globaux en ont besoin.
"""

from __future__ import annotations

import logging
import os
import sys


def _setup_logging() -> str:
    from lagcontrol.config import config_dir

    path = os.path.join(config_dir(), "lagcontrol.log")
    logging.basicConfig(
        level=logging.INFO,
        format="%(asctime)s %(levelname)s %(name)s: %(message)s",
        handlers=[logging.FileHandler(path, encoding="utf-8")],
    )
    return path


def _is_admin() -> bool:
    try:
        import ctypes

        return bool(ctypes.windll.shell32.IsUserAnAdmin())
    except Exception:
        return False


def _relaunch_as_admin() -> bool:
    """Relance le programme avec l'invite UAC. Renvoie True si la relance est partie."""
    import ctypes
    import subprocess

    if getattr(sys, "frozen", False):  # exécutable PyInstaller
        exe, args = sys.executable, sys.argv[1:]
    else:
        exe = sys.executable
        pythonw = os.path.join(os.path.dirname(exe), "pythonw.exe")
        if os.path.exists(pythonw):
            exe = pythonw  # pas de fenêtre console
        args = [os.path.abspath(sys.argv[0]), *sys.argv[1:]]
    params = subprocess.list2cmdline([*args, "--elevated"])
    workdir = os.path.dirname(os.path.abspath(sys.argv[0]))
    ret = ctypes.windll.shell32.ShellExecuteW(None, "runas", exe, params, workdir, 1)
    return ret > 32


def main() -> None:
    log_path = _setup_logging()
    log = logging.getLogger("lagcontrol")

    if sys.platform == "win32" and not _is_admin() and "--elevated" not in sys.argv and "--no-elevate" not in sys.argv:
        if _relaunch_as_admin():
            return
        log.warning("Élévation refusée, démarrage sans droits administrateur")

    def excepthook(exc_type, exc, tb):
        log.critical("Erreur non gérée", exc_info=(exc_type, exc, tb))
        try:
            from tkinter import messagebox

            messagebox.showerror("LagControl", f"Erreur inattendue : {exc}\n\nDétails dans :\n{log_path}")
        except Exception:
            pass

    sys.excepthook = excepthook

    from lagcontrol.gui import run

    run()


if __name__ == "__main__":
    main()
