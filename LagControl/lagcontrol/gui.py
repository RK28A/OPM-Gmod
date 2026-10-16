"""Interface graphique (customtkinter)."""

from __future__ import annotations

import logging
import os
import queue
import sys
import threading
import time
import zlib
from tkinter import filedialog, messagebox

import customtkinter as ctk

from . import APP_NAME, __version__, sound
from .config import Settings
from .engine import EngineError, LagEngine
from .hotkeys import HotkeyManager, pretty
from .processes import AppEntry, PortTracker, list_apps

log = logging.getLogger(__name__)

# --------------------------------------------------------------------- thème
C = {
    "bg": "#0c0e13",
    "card": "#141720",
    "card2": "#1a1e29",
    "row_hover": "#202533",
    "border": "#252a38",
    "accent": "#7c5cff",
    "accent_h": "#6a48f5",
    "accent_dim": "#241f45",
    "green": "#22c55e",
    "green_h": "#16a34a",
    "red": "#f04343",
    "red_h": "#d63131",
    "red_dim": "#2a1316",
    "amber": "#f5a524",
    "text": "#e8eaf1",
    "muted": "#8b91a3",
    "faint": "#5a6072",
}
AVATAR_COLORS = ["#7c5cff", "#3b82f6", "#06b6d4", "#10b981", "#f59e0b", "#ef4444", "#ec4899", "#8b5cf6"]
FAMILY = "Segoe UI" if sys.platform == "win32" else "DejaVu Sans"

DIRECTION_LABELS = {"in": "Entrant", "out": "Sortant", "both": "Les deux"}
AUTO_OFF_CHOICES = {"Jamais": 0, "5 s": 5, "10 s": 10, "30 s": 30, "1 min": 60, "2 min": 120, "5 min": 300}
DELAY_PRESETS = (50, 100, 250, 500, 1000, 2000)


def font(size: int = 13, weight: str = "normal") -> ctk.CTkFont:
    return ctk.CTkFont(family=FAMILY, size=size, weight=weight)


def is_admin() -> bool:
    if sys.platform != "win32":
        return True
    try:
        import ctypes

        return bool(ctypes.windll.shell32.IsUserAnAdmin())
    except Exception:
        return False


# ----------------------------------------------------------------- widgets
class Card(ctk.CTkFrame):
    def __init__(self, master, title: str, step: str | None = None, **kw):
        super().__init__(master, fg_color=C["card"], corner_radius=16, border_width=1, border_color=C["border"], **kw)
        head = ctk.CTkFrame(self, fg_color="transparent")
        head.pack(fill="x", padx=18, pady=(16, 8))
        if step:
            ctk.CTkLabel(
                head, text=step, width=24, height=24, corner_radius=8, fg_color=C["accent_dim"],
                text_color=C["accent"], font=font(12, "bold"),
            ).pack(side="left", padx=(0, 10))
        ctk.CTkLabel(head, text=title, font=font(15, "bold"), text_color=C["text"]).pack(side="left")
        self.head = head
        self.body = ctk.CTkFrame(self, fg_color="transparent")
        self.body.pack(fill="both", expand=True, padx=18, pady=(0, 16))


class SliderRow(ctk.CTkFrame):
    """Titre + champ numérique + curseur, synchronisés."""

    def __init__(self, master, title, unit, lo, hi, value, on_change, hint="", max_value=None, decimals=0):
        super().__init__(master, fg_color="transparent")
        self.lo, self.hi = lo, hi
        self.max_value = hi if max_value is None else max_value
        self.decimals = decimals
        self.on_change = on_change
        self.value = value

        top = ctk.CTkFrame(self, fg_color="transparent")
        top.pack(fill="x")
        ctk.CTkLabel(top, text=title, font=font(13, "bold"), text_color=C["text"]).pack(side="left")
        if hint:
            ctk.CTkLabel(top, text=hint, font=font(11), text_color=C["faint"]).pack(side="left", padx=8)
        ctk.CTkLabel(top, text=unit, font=font(12), text_color=C["muted"], width=24).pack(side="right")
        self.entry = ctk.CTkEntry(
            top, width=72, height=28, justify="right", font=font(13, "bold"),
            fg_color=C["card2"], border_color=C["border"], text_color=C["text"],
        )
        self.entry.pack(side="right", padx=(0, 6))
        self.entry.bind("<Return>", self._from_entry)
        self.entry.bind("<FocusOut>", self._from_entry)

        self.slider = ctk.CTkSlider(
            self, from_=lo, to=hi, command=self._from_slider, height=18,
            progress_color=C["accent"], button_color=C["accent"], button_hover_color=C["accent_h"],
            fg_color=C["card2"],
        )
        self.slider.pack(fill="x", pady=(8, 0))
        self.set(value, notify=False)

    def _fmt(self, v) -> str:
        return f"{v:.{self.decimals}f}" if self.decimals else str(int(round(v)))

    def _coerce(self, v):
        v = max(self.lo, min(self.max_value, v))
        return round(v, self.decimals) if self.decimals else int(round(v))

    def set(self, v, notify=True):
        self.value = self._coerce(v)
        self.slider.set(min(self.hi, self.value))
        self.entry.delete(0, "end")
        self.entry.insert(0, self._fmt(self.value))
        if notify:
            self.on_change(self.value)

    def _from_slider(self, v):
        self.set(v)

    def _from_entry(self, _event=None):
        text = self.entry.get().strip().replace(",", ".")
        try:
            self.set(float(text))
        except ValueError:
            self.set(self.value, notify=False)


class ProcessRow(ctk.CTkFrame):
    def __init__(self, master, app: AppEntry, on_click):
        super().__init__(master, fg_color="transparent", corner_radius=10, border_width=1, border_color=C["card"])
        self.app = app
        self.selected = False

        color = AVATAR_COLORS[zlib.crc32(app.key.encode()) % len(AVATAR_COLORS)]
        avatar = ctk.CTkLabel(
            self, text=(app.name[:1] or "?").upper(), width=34, height=34, corner_radius=10,
            fg_color=color, text_color="#ffffff", font=font(15, "bold"),
        )
        avatar.pack(side="left", padx=(10, 10), pady=7)

        texts = ctk.CTkFrame(self, fg_color="transparent")
        texts.pack(side="left", fill="x", expand=True, pady=6)
        name = ctk.CTkLabel(texts, text=app.name, font=font(13, "bold"), text_color=C["text"], anchor="w")
        name.pack(fill="x")
        n = len(app.pids)
        sub_text = f"{n} processus" + (f"  ·  {app.connections} connexion{'s' if app.connections > 1 else ''}"
                                        if app.connections else "  ·  hors ligne")
        sub = ctk.CTkLabel(texts, text=sub_text, font=font(11), text_color=C["muted"], anchor="w")
        sub.pack(fill="x")

        dot = ctk.CTkLabel(self, text="●", font=font(12), width=20,
                           text_color=C["green"] if app.connections else C["faint"])
        dot.pack(side="right", padx=(0, 12))

        for w in (self, avatar, texts, name, sub, dot):
            w.bind("<Button-1>", lambda _e: on_click(app))
            w.bind("<Enter>", self._enter)
            w.bind("<Leave>", self._leave)

    def _enter(self, _e=None):
        if not self.selected:
            self.configure(fg_color=C["row_hover"])

    def _leave(self, _e=None):
        if not self.selected:
            self.configure(fg_color="transparent")

    def set_selected(self, selected: bool):
        self.selected = selected
        self.configure(
            fg_color=C["accent_dim"] if selected else "transparent",
            border_color=C["accent"] if selected else C["card"],
        )


class KeyChip(ctk.CTkButton):
    def __init__(self, master, text, command):
        super().__init__(
            master, text=text, command=command, width=120, height=34, corner_radius=8,
            fg_color=C["card2"], hover_color=C["row_hover"], border_width=1, border_color=C["border"],
            text_color=C["text"], font=font(13, "bold"),
        )


# --------------------------------------------------------------------- app
class App(ctk.CTk):
    def __init__(self):
        ctk.set_appearance_mode("dark")
        super().__init__(fg_color=C["bg"])
        self.title(f"{APP_NAME} {__version__}")
        self.geometry("1140x800")
        self.minsize(980, 660)

        self.settings = Settings.load()
        self.events: queue.Queue = queue.Queue()
        self.tracker = PortTracker()
        self.tracker.set_target(self.settings.target)
        self.tracker.start()
        self.engine = LagEngine(self.tracker)
        self.hotkeys = HotkeyManager(self.events)

        self.rows: list[ProcessRow] = []
        self._save_job = None
        self._auto_job = None
        self._activated_at = 0.0
        self._hold_active = False
        self._panic_latch = False
        self._recording_slot: str | None = None
        self._tick_count = 0
        self._loading = False

        self._build()
        self._apply_engine_settings()
        self._bind_hotkeys()
        self.attributes("-topmost", self.settings.always_on_top)
        self.protocol("WM_DELETE_WINDOW", self.on_close)

        self.refresh_apps()
        self._render_state()
        self.after(40, self._tick)
        self._startup_checks()

    # ----------------------------------------------------------- layout
    def _build(self):
        self.grid_columnconfigure(0, weight=5, uniform="col")
        self.grid_columnconfigure(1, weight=6, uniform="col")
        self.grid_rowconfigure(1, weight=1)

        self._build_header()
        self._build_app_picker().grid(row=1, column=0, sticky="nsew", padx=(24, 10), pady=(0, 14))

        # Défilable : sur petit écran / zoom Windows élevé rien n'est coupé.
        right = ctk.CTkScrollableFrame(self, fg_color="transparent", scrollbar_button_color=C["bg"],
                                       scrollbar_button_hover_color=C["border"])
        right.grid(row=1, column=1, sticky="nsew", padx=(10, 14), pady=(0, 14))
        right.grid_columnconfigure(0, weight=1)
        self._build_settings(right).grid(row=0, column=0, sticky="ew", padx=(0, 6), pady=(0, 14))
        self._build_hotkeys(right).grid(row=1, column=0, sticky="ew", padx=(0, 6))

        self._build_action_bar().grid(row=2, column=0, columnspan=2, sticky="ew", padx=24, pady=(0, 22))

    def _build_header(self):
        head = ctk.CTkFrame(self, fg_color="transparent")
        head.grid(row=0, column=0, columnspan=2, sticky="ew", padx=24, pady=(20, 16))

        logo = ctk.CTkFrame(head, width=46, height=46, corner_radius=14, fg_color=C["accent"])
        logo.pack(side="left")
        logo.pack_propagate(False)
        bolt = ctk.CTkCanvas(logo, width=24, height=30, bg=C["accent"], highlightthickness=0)
        bolt.create_polygon(14, 0, 2, 17, 11, 17, 8, 30, 22, 11, 13, 11, 16, 0, fill="#ffffff", outline="")
        bolt.place(relx=0.5, rely=0.5, anchor="center")
        titles = ctk.CTkFrame(head, fg_color="transparent")
        titles.pack(side="left", padx=14)
        ctk.CTkLabel(titles, text=APP_NAME, font=font(24, "bold"), text_color=C["text"], anchor="w").pack(fill="x")
        ctk.CTkLabel(titles, text="Ajoute de la latence réseau à une seule application — les autres ne sont pas touchées",
                     font=font(12), text_color=C["muted"], anchor="w").pack(fill="x")

        self.pill = ctk.CTkFrame(head, corner_radius=22, fg_color=C["card"], border_width=1, border_color=C["border"])
        self.pill.pack(side="right")
        self.pill_dot = ctk.CTkLabel(self.pill, text="●", font=font(14), text_color=C["faint"])
        self.pill_dot.pack(side="left", padx=(16, 6), pady=8)
        self.pill_text = ctk.CTkLabel(self.pill, text="INACTIF", font=font(13, "bold"), text_color=C["muted"])
        self.pill_text.pack(side="left", padx=(0, 18), pady=8)

    def _build_app_picker(self):
        card = Card(self, "Application cible", "1")
        body = card.body

        bar = ctk.CTkFrame(body, fg_color="transparent")
        bar.pack(fill="x")
        self.search_var = ctk.StringVar()
        self.search_var.trace_add("write", lambda *_: self._filter_rows())
        ctk.CTkEntry(
            bar, textvariable=self.search_var, placeholder_text="Rechercher une application…", height=38,
            corner_radius=10, fg_color=C["card2"], border_color=C["border"], text_color=C["text"], font=font(13),
        ).pack(side="left", fill="x", expand=True)
        self.refresh_btn = ctk.CTkButton(
            bar, text="↻", width=38, height=38, corner_radius=10, font=font(18, "bold"),
            fg_color=C["card2"], hover_color=C["row_hover"], border_width=1, border_color=C["border"],
            command=self.refresh_apps,
        )
        self.refresh_btn.pack(side="left", padx=(8, 0))

        self.network_only_var = ctk.BooleanVar(value=self.settings.network_only)
        ctk.CTkCheckBox(
            body, text="Seulement les applications connectées à Internet", variable=self.network_only_var,
            command=self._on_network_only, font=font(12), text_color=C["muted"], checkbox_width=18,
            checkbox_height=18, corner_radius=5, fg_color=C["accent"], hover_color=C["accent_h"],
            border_color=C["faint"],
        ).pack(anchor="w", pady=(10, 6))

        self.list_frame = ctk.CTkScrollableFrame(
            body, fg_color=C["bg"], corner_radius=12, scrollbar_button_color=C["border"],
            scrollbar_button_hover_color=C["faint"],
        )
        self.list_frame.pack(fill="both", expand=True, pady=(4, 12))

        self.empty_box = ctk.CTkFrame(self.list_frame, fg_color="transparent")
        self.empty_label = ctk.CTkLabel(self.empty_box, text="", font=font(12), text_color=C["muted"])
        self.empty_label.pack(pady=(30, 8))
        self.use_name_btn = ctk.CTkButton(
            self.empty_box, text="", height=32, corner_radius=8, fg_color=C["accent_dim"],
            hover_color=C["row_hover"], text_color=C["accent"], font=font(12, "bold"),
            command=lambda: self.set_target(self.search_var.get().strip()),
        )

        target = ctk.CTkFrame(body, fg_color=C["card2"], corner_radius=12)
        target.pack(fill="x")
        ctk.CTkButton(
            target, text="Parcourir .exe", width=120, height=34, corner_radius=8, font=font(12, "bold"),
            fg_color="transparent", hover_color=C["row_hover"], border_width=1, border_color=C["border"],
            text_color=C["text"], command=self._browse_exe,
        ).pack(side="right", padx=14)
        left = ctk.CTkFrame(target, fg_color="transparent")
        left.pack(side="left", fill="x", expand=True, padx=14, pady=10)
        ctk.CTkLabel(left, text="CIBLE ACTUELLE", font=font(10, "bold"), text_color=C["faint"], anchor="w").pack(fill="x")
        self.target_label = ctk.CTkLabel(left, text="", font=font(15, "bold"), text_color=C["text"], anchor="w")
        self.target_label.pack(fill="x")
        self.target_info = ctk.CTkLabel(left, text="", font=font(11), text_color=C["muted"], anchor="w")
        self.target_info.pack(fill="x")
        return card

    def _build_settings(self, parent):
        card = Card(parent, "Réglages du lag", "2")
        body = card.body
        s = self.settings

        self.ping_label = ctk.CTkLabel(card.head, text="", font=font(12, "bold"), text_color=C["amber"])
        self.ping_label.pack(side="right")

        self.delay_row = SliderRow(body, "Latence", "ms", 0, 2000, s.delay_ms, self._on_delay,
                                   hint="ajoutée à chaque paquet", max_value=10_000)
        self.delay_row.pack(fill="x")

        presets = ctk.CTkFrame(body, fg_color="transparent")
        presets.pack(fill="x", pady=(8, 14))
        for ms in DELAY_PRESETS:
            ctk.CTkButton(
                presets, text=f"{ms}" if ms < 1000 else f"{ms // 1000} s", height=26, width=10, corner_radius=7,
                font=font(11, "bold"), fg_color=C["card2"], hover_color=C["accent_dim"], text_color=C["muted"],
                command=lambda v=ms: self.delay_row.set(v),
            ).pack(side="left", fill="x", expand=True, padx=(0 if ms == DELAY_PRESETS[0] else 6, 0))

        self.jitter_row = SliderRow(body, "Variation", "ms", 0, 500, s.jitter_ms, self._on_jitter,
                                    hint="lag irrégulier (±)", max_value=2000)
        self.jitter_row.pack(fill="x", pady=(0, 14))
        self.loss_row = SliderRow(body, "Perte de paquets", "%", 0, 100, s.loss_pct, self._on_loss,
                                  hint="paquets jetés", decimals=1)
        self.loss_row.pack(fill="x", pady=(0, 14))

        drow = ctk.CTkFrame(body, fg_color="transparent")
        drow.pack(fill="x")
        ctk.CTkLabel(drow, text="Sens du trafic", font=font(13, "bold"), text_color=C["text"]).pack(side="left")
        self.direction_seg = ctk.CTkSegmentedButton(
            drow, values=list(DIRECTION_LABELS.values()), command=self._on_direction, height=32, font=font(12, "bold"),
            selected_color=C["accent"], selected_hover_color=C["accent_h"], unselected_color=C["card2"],
            unselected_hover_color=C["row_hover"], fg_color=C["card2"], text_color=C["text"],
        )
        self.direction_seg.set(DIRECTION_LABELS[s.direction])
        self.direction_seg.pack(side="right")
        return card

    def _build_hotkeys(self, parent):
        card = Card(parent, "Raccourcis & sécurité", "3")
        body = card.body
        body.grid_columnconfigure(1, weight=1)
        s = self.settings

        def label(row, text, sub):
            box = ctk.CTkFrame(body, fg_color="transparent")
            box.grid(row=row, column=0, sticky="w", pady=5)
            ctk.CTkLabel(box, text=text, font=font(13, "bold"), text_color=C["text"], anchor="w").pack(fill="x")
            ctk.CTkLabel(box, text=sub, font=font(11), text_color=C["faint"], anchor="w").pack(fill="x")

        label(0, "Activer / désactiver", "cliquez puis appuyez sur la touche voulue")
        self.toggle_chip = KeyChip(body, pretty(s.toggle_hotkey), lambda: self._record("toggle"))
        self.toggle_chip.grid(row=0, column=2, sticky="e")

        label(1, "Mode du raccourci", "Maintenir = lag tant que la touche est enfoncée")
        self.mode_seg = ctk.CTkSegmentedButton(
            body, values=["Basculer", "Maintenir"], command=self._on_mode, height=32, width=180, font=font(12, "bold"),
            selected_color=C["accent"], selected_hover_color=C["accent_h"], unselected_color=C["card2"],
            unselected_hover_color=C["row_hover"], fg_color=C["card2"], text_color=C["text"],
        )
        self.mode_seg.set("Maintenir" if s.hold_mode else "Basculer")
        self.mode_seg.grid(row=1, column=2, sticky="e")

        label(2, "Arrêt d'urgence", "coupe tout et rétablit le réseau")
        self.panic_chip = KeyChip(body, pretty(s.panic_hotkey), lambda: self._record("panic"))
        self.panic_chip.configure(text_color=C["red"])
        self.panic_chip.grid(row=2, column=2, sticky="e")

        label(3, "Désactivation automatique", "sécurité si vous oubliez de couper")
        current = next((k for k, v in AUTO_OFF_CHOICES.items() if v == s.auto_off_s), None)
        if current is None:
            current = f"{s.auto_off_s} s"
            AUTO_OFF_CHOICES[current] = s.auto_off_s
        self.auto_menu = ctk.CTkOptionMenu(
            body, values=list(AUTO_OFF_CHOICES), command=self._on_auto_off, width=120, height=32, font=font(12, "bold"),
            fg_color=C["card2"], button_color=C["card2"], button_hover_color=C["row_hover"],
            dropdown_fg_color=C["card2"], dropdown_hover_color=C["row_hover"], text_color=C["text"],
        )
        self.auto_menu.set(current)
        self.auto_menu.grid(row=3, column=2, sticky="e")

        switches = ctk.CTkFrame(body, fg_color="transparent")
        switches.grid(row=4, column=0, columnspan=3, sticky="ew", pady=(10, 0))
        self.sound_var = ctk.BooleanVar(value=s.sounds)
        self.top_var = ctk.BooleanVar(value=s.always_on_top)
        for text, var, cmd in (("Sons de confirmation", self.sound_var, self._on_sound),
                               ("Toujours au premier plan", self.top_var, self._on_topmost)):
            ctk.CTkSwitch(
                switches, text=text, variable=var, command=cmd, font=font(12), text_color=C["muted"],
                progress_color=C["accent"], button_color=C["text"], button_hover_color="#ffffff",
                fg_color=C["card2"],
            ).pack(side="left", padx=(0, 24))
        return card

    def _build_action_bar(self):
        bar = ctk.CTkFrame(self, fg_color=C["card"], corner_radius=16, border_width=1, border_color=C["border"])
        bar.grid_columnconfigure(2, weight=1)

        self.toggle_btn = ctk.CTkButton(
            bar, text="", height=64, width=300, corner_radius=14, font=font(17, "bold"), command=self.toggle,
        )
        self.toggle_btn.grid(row=0, column=0, padx=(18, 10), pady=(18, 6))

        self.panic_btn = ctk.CTkButton(
            bar, text="⛔  ARRÊT D'URGENCE", height=64, width=230, corner_radius=14, font=font(15, "bold"),
            fg_color=C["red_dim"], hover_color=C["red_h"], border_width=2, border_color=C["red"],
            text_color="#ffd9d9", command=self.panic,
        )
        self.panic_btn.grid(row=0, column=1, padx=(0, 10), pady=(18, 6))

        stats = ctk.CTkFrame(bar, fg_color="transparent")
        stats.grid(row=0, column=2, sticky="e", padx=(10, 18), pady=(18, 6))
        self.stat_labels = {}
        for key, title in (("delayed", "Retardés"), ("dropped", "Perdus"), ("queued", "En attente"),
                           ("ports", "Connexions")):
            tile = ctk.CTkFrame(stats, fg_color=C["card2"], corner_radius=12, width=104, height=64)
            tile.pack(side="left", padx=(8, 0))
            tile.pack_propagate(False)
            value = ctk.CTkLabel(tile, text="0", font=font(18, "bold"), text_color=C["text"])
            value.pack(pady=(9, 0))
            ctk.CTkLabel(tile, text=title, font=font(11), text_color=C["muted"]).pack()
            self.stat_labels[key] = value

        self.message = ctk.CTkLabel(bar, text="", font=font(12), text_color=C["muted"], anchor="w")
        self.message.grid(row=1, column=0, columnspan=3, sticky="ew", padx=20, pady=(0, 12))
        return bar

    # ------------------------------------------------------------ processus
    def refresh_apps(self):
        if self._loading:
            return
        self._loading = True
        self.refresh_btn.configure(state="disabled", text="…")

        def worker():
            try:
                apps = list_apps()
            except Exception:
                log.exception("Impossible de lister les applications")
                apps = []
            self.events.put(("apps", apps))

        threading.Thread(target=worker, name="ListApps", daemon=True).start()

    def _populate(self, apps: list[AppEntry]):
        self._loading = False
        self.refresh_btn.configure(state="normal", text="↻")
        for row in self.rows:
            row.destroy()
        self.rows = [ProcessRow(self.list_frame, app, lambda a: self.set_target(a.name)) for app in apps]
        self._filter_rows()

    def _filter_rows(self):
        query = self.search_var.get().strip().lower()
        net_only = self.network_only_var.get()
        target = self.settings.target.lower()
        self.empty_box.pack_forget()
        shown = 0
        for row in self.rows:
            row.pack_forget()
        for row in self.rows:
            app = row.app
            visible = (not query or query in app.key) and (not net_only or app.connections or app.key == target)
            row.set_selected(app.key == target)
            if visible:
                row.pack(fill="x", padx=4, pady=2)
                shown += 1
        if shown == 0:
            if self._loading:
                self.empty_label.configure(text="Chargement des applications…")
            elif query:
                self.empty_label.configure(text=f"Aucune application ne correspond à « {query} »")
            else:
                self.empty_label.configure(text="Aucune application connectée trouvée")
            self.use_name_btn.pack_forget()
            if query:
                name = self.search_var.get().strip()
                if not name.lower().endswith(".exe"):
                    name += ".exe"
                self.use_name_btn.configure(text=f"Cibler quand même « {name} »",
                                            command=lambda n=name: self.set_target(n))
                self.use_name_btn.pack()
            self.empty_box.pack(fill="x")

    def set_target(self, name: str):
        name = os.path.basename(name.strip())
        if not name:
            return
        self.settings.target = name
        self.tracker.set_target(name)
        self._filter_rows()
        self._render_state()
        self._schedule_save()
        self.flash(f"Cible : {name}", C["text"])

    def _browse_exe(self):
        path = filedialog.askopenfilename(
            title="Choisir l'exécutable à ralentir",
            filetypes=[("Programmes", "*.exe"), ("Tous les fichiers", "*.*")],
        )
        if path:
            self.set_target(path)

    # ------------------------------------------------------------- réglages
    def _apply_engine_settings(self):
        s = self.settings
        self.engine.configure(s.delay_ms, s.jitter_ms, s.loss_pct, s.direction)
        mult = 2 if s.direction == "both" else 1
        self.ping_label.configure(text=f"≈ +{s.delay_ms * mult} ms de ping")

    def _on_delay(self, v):
        self.settings.delay_ms = int(v)
        self._settings_changed()

    def _on_jitter(self, v):
        self.settings.jitter_ms = int(v)
        self._settings_changed()

    def _on_loss(self, v):
        self.settings.loss_pct = float(v)
        self._settings_changed()

    def _on_direction(self, label):
        self.settings.direction = next(k for k, v in DIRECTION_LABELS.items() if v == label)
        self._settings_changed()

    def _settings_changed(self):
        self._apply_engine_settings()
        self._render_state()
        self._schedule_save()

    def _on_network_only(self):
        self.settings.network_only = self.network_only_var.get()
        self._filter_rows()
        self._schedule_save()

    def _on_mode(self, label):
        self.settings.hold_mode = label == "Maintenir"
        if self.engine.active:
            self.deactivate()
        self._bind_hotkeys()
        self._render_state()
        self._schedule_save()

    def _on_auto_off(self, label):
        self.settings.auto_off_s = AUTO_OFF_CHOICES.get(label, 0)
        self._schedule_save()

    def _on_sound(self):
        self.settings.sounds = self.sound_var.get()
        self._schedule_save()

    def _on_topmost(self):
        self.settings.always_on_top = self.top_var.get()
        self.attributes("-topmost", self.settings.always_on_top)
        self._schedule_save()

    def _schedule_save(self):
        if self._save_job:
            self.after_cancel(self._save_job)
        self._save_job = self.after(600, self._save_now)

    def _save_now(self):
        self._save_job = None
        self.settings.save()

    # ------------------------------------------------------------ raccourcis
    def _bind_hotkeys(self):
        errors = self.hotkeys.bind(self.settings.toggle_hotkey, self.settings.panic_hotkey, self.settings.hold_mode)
        if errors:
            self.flash(errors[0], C["amber"])

    def _record(self, slot: str):
        if self._recording_slot:
            return
        self.hotkeys.unbind()  # ne pas déclencher l'ancien raccourci pendant l'enregistrement
        if not self.hotkeys.record(slot):
            self._bind_hotkeys()
            self.flash("Enregistrement des raccourcis indisponible.", C["amber"])
            return
        self._recording_slot = slot
        chip = self.toggle_chip if slot == "toggle" else self.panic_chip
        chip.configure(text="Appuyez…", border_color=C["accent"])
        self.flash("Appuyez sur la touche ou la combinaison voulue (Échap pour annuler).", C["accent"])

    def _on_recorded(self, slot: str, hotkey: str):
        self._recording_slot = None
        chip = self.toggle_chip if slot == "toggle" else self.panic_chip
        other = self.settings.panic_hotkey if slot == "toggle" else self.settings.toggle_hotkey
        if hotkey and hotkey != "esc" and hotkey != other:
            if slot == "toggle":
                self.settings.toggle_hotkey = hotkey
            else:
                self.settings.panic_hotkey = hotkey
            self.flash(f"Raccourci enregistré : {pretty(hotkey)}", C["green"])
            self._schedule_save()
        elif hotkey == other:
            self.flash("Ce raccourci est déjà utilisé par l'autre action.", C["amber"])
        else:
            self.flash("Enregistrement annulé.", C["muted"])
        chip.configure(text=pretty(self.settings.toggle_hotkey if slot == "toggle" else self.settings.panic_hotkey),
                       border_color=C["border"])
        self._bind_hotkeys()
        self._render_state()

    # ------------------------------------------------------------- actions
    def toggle(self):
        if self.engine.active:
            self.deactivate()
        else:
            self.activate()

    def activate(self) -> bool:
        if self.engine.active:
            return True
        if not self.settings.target:
            self.flash("Choisissez d'abord une application dans la liste.", C["amber"])
            self._beep("error")
            return False
        try:
            self.engine.reset_stats()
            self.engine.start()
        except EngineError as exc:
            self._beep("error")
            self.flash(str(exc).splitlines()[0], C["red"])
            messagebox.showerror(APP_NAME, str(exc))
            return False
        self._panic_latch = False
        self._activated_at = time.monotonic()
        if self.settings.auto_off_s:
            self._auto_job = self.after(self.settings.auto_off_s * 1000, lambda: self.deactivate("auto"))
        self._beep("on")
        self.flash(f"Lag activé sur {self.settings.target}.", C["green"])
        self._render_state()
        return True

    def deactivate(self, reason: str | None = None):
        self._cancel_auto()
        if not self.engine.active:
            return
        self.engine.stop()
        self._beep("off")
        self.flash("Désactivation automatique : réseau rétabli." if reason == "auto" else "Lag désactivé.",
                   C["muted"])
        self._render_state()

    def panic(self):
        self._cancel_auto()
        self._panic_latch = True  # en mode Maintenir : attendre que la touche soit relâchée
        self.engine.emergency_stop()
        self._beep("panic")
        self.flash("⛔ ARRÊT D'URGENCE — tous les paquets ont été relâchés, le réseau est rétabli.", C["red"])
        self._render_state()

    def _cancel_auto(self):
        if self._auto_job:
            self.after_cancel(self._auto_job)
            self._auto_job = None

    def _beep(self, kind: str):
        if self.settings.sounds:
            sound.play(kind)

    def flash(self, text: str, color: str | None = None):
        self.message.configure(text=text, text_color=color or C["muted"])

    # --------------------------------------------------------------- boucle
    def _tick(self):
        try:
            self._drain_events()
            self._poll_hold()
            self._tick_count += 1
            if self._tick_count % 6 == 0:
                self._render_stats()
        except Exception:
            log.exception("Erreur dans la boucle de l'interface")
        self.after(40, self._tick)

    def _drain_events(self):
        while True:
            try:
                event = self.events.get_nowait()
            except queue.Empty:
                return
            if event == "toggle":
                if not self._recording_slot:
                    self.toggle()
            elif event == "panic":
                self.panic()
            elif isinstance(event, tuple) and event[0] == "apps":
                self._populate(event[1])
            elif isinstance(event, tuple) and event[0] == "recorded":
                self._on_recorded(event[1], event[2])

    def _poll_hold(self):
        if not self.settings.hold_mode or self._recording_slot:
            return
        pressed = self.hotkeys.is_pressed(self.settings.toggle_hotkey)
        if self._panic_latch:
            if not pressed:
                self._panic_latch = False
            return
        if pressed and not self.engine.active:
            self._hold_active = self.activate()
            if not self._hold_active:
                self._panic_latch = True  # évite de réessayer en boucle tant que la touche est tenue
        elif not pressed and self.engine.active and self._hold_active:
            self._hold_active = False
            self.deactivate()

    def _render_stats(self):
        e = self.engine
        self.stat_labels["delayed"].configure(text=f"{e.delayed:,}".replace(",", " "))
        self.stat_labels["dropped"].configure(text=f"{e.dropped:,}".replace(",", " "))
        self.stat_labels["queued"].configure(text=str(e.queued))
        self.stat_labels["ports"].configure(text=str(len(self.tracker.ports)))

        if not self.settings.target:
            info = "Sélectionnez une application dans la liste ci-dessus"
        elif not self.tracker.pids:
            info = "Pas lancée pour l'instant — sera ciblée dès son démarrage"
        else:
            n = len(self.tracker.ports)
            info = f"{len(self.tracker.pids)} processus  ·  {n} connexion{'s' if n > 1 else ''} suivie{'s' if n > 1 else ''}"
        self.target_info.configure(text=info)

        if e.active:
            # le moteur a pu s'arrêter tout seul (erreur pilote) : l'interface suit
            elapsed = int(time.monotonic() - self._activated_at)
            extra = ""
            if self.settings.auto_off_s:
                extra = f"  ·  arrêt dans {max(0, self.settings.auto_off_s - elapsed)} s"
            self.pill_text.configure(text=f"LAG ACTIF  ·  {self.settings.delay_ms} ms{extra}")
        elif self.toggle_btn.cget("fg_color") == C["red"]:
            self._render_state()

    def _render_state(self):
        active = self.engine.active
        hk = pretty(self.settings.toggle_hotkey)
        if active:
            self.toggle_btn.configure(text=f"■  DÉSACTIVER LE LAG   [{hk}]", fg_color=C["red"], hover_color=C["red_h"],
                                      text_color="#ffffff")
            self.pill.configure(border_color=C["red"], fg_color=C["red_dim"])
            self.pill_dot.configure(text_color=C["red"])
            self.pill_text.configure(text=f"LAG ACTIF  ·  {self.settings.delay_ms} ms", text_color="#ffd9d9")
        else:
            label = f"▶  ACTIVER LE LAG   [{hk}]" if not self.settings.hold_mode else f"▶  ACTIVER  (maintenir {hk})"
            self.toggle_btn.configure(text=label, fg_color=C["green"], hover_color=C["green_h"], text_color="#06210f")
            self.pill.configure(border_color=C["border"], fg_color=C["card"])
            self.pill_dot.configure(text_color=C["faint"])
            self.pill_text.configure(text="INACTIF", text_color=C["muted"])
        self.target_label.configure(text=self.settings.target or "Aucune",
                                    text_color=C["text"] if self.settings.target else C["faint"])
        self._render_stats()

    # ------------------------------------------------------------ démarrage
    def _startup_checks(self):
        if not LagEngine.available():
            self.flash("Mode démo : " + LagEngine.unavailable_reason(), C["amber"])
        elif not is_admin():
            self.flash("⚠ Lancez LagControl en administrateur, sinon le lag ne pourra pas s'activer.", C["amber"])
        elif not self.hotkeys.available:
            self.flash("Raccourcis globaux indisponibles — utilisez les boutons.", C["amber"])
        else:
            self.flash(f"Prêt. Choisissez une application, réglez le lag, puis appuyez sur {pretty(self.settings.toggle_hotkey)}.")

    def on_close(self):
        try:
            self.engine.emergency_stop()
            self.hotkeys.shutdown()
            self.tracker.stop()
            if self._save_job:
                self.after_cancel(self._save_job)
            self.settings.save()
        finally:
            self.destroy()


def run():
    app = App()

    def report(exc_type, exc, tb):
        log.error("Erreur dans l'interface", exc_info=(exc_type, exc, tb))
        app.flash(f"Erreur : {exc}", C["red"])

    app.report_callback_exception = report
    app.mainloop()
