from __future__ import annotations
import subprocess
import tkinter as tk
from pathlib import Path
from tkinter import filedialog, messagebox
from .refresh import (
    PIL_AVAILABLE,
    PIL_MESSAGE,
    Button,
    GlassGauge,
    MenuList,
    Panel,
    Worker,
    load_settings,
    pick_theme,
    save_settings,
)
from .wetworks import (PROJECT_ROOT, containers_in, diagnose, filenames_dir, log,
                       taildata_dir, unpack_root)
from .worker import Backend, BackendError

WINDOW_WIDTH = 1120
WINDOW_HEIGHT = 896
MIN_WIDTH = 940
MIN_HEIGHT = 776

PAD = 20
HEADER_HEIGHT = 76
BUTTON_HEIGHT = 38
BUTTON_GAP = 10
GAUGE_HEIGHT = 300
MENU_COLUMNS = 2


class CoreTools:

    def __init__(self, root: tk.Tk):
        self.root = root
        self.project_root = PROJECT_ROOT
        self.settings = load_settings(self.project_root)
        self.theme = pick_theme(self.settings.get("last_theme", ""))
        self.settings["last_theme"] = self.theme.key
        save_settings(self.project_root, self.settings)

        self.game_id = self.settings.get("last_game", "")
        self.games: list[dict] = []
        self.by_id: dict[str, dict] = {}
        self.mod_window = None
        self.creator_window = None
        self.size = (0, 0)
        self.pour_target = 0.0
        self.pour_level = 0.0
        self.pour_job = None

        root.title("GokonSoftworks")
        root.geometry(f"{WINDOW_WIDTH}x{WINDOW_HEIGHT}")
        root.minsize(MIN_WIDTH, MIN_HEIGHT)
        root.resizable(False, False)
        root.configure(bg=self.theme.bg)

        if not PIL_AVAILABLE:
            messagebox.showerror("GokonSoftworks", PIL_MESSAGE, parent=self.root)
            raise SystemExit(1)

        self.canvas = tk.Canvas(root, bg=self.theme.bg, highlightthickness=0)
        self.canvas.pack(fill="both", expand=True)
        self.worker = Worker(self.canvas)
        self.backend = Backend()

        self.build()
        self.canvas.bind("<Configure>", self.on_configure)
        self.root.protocol("WM_DELETE_WINDOW", self.on_close)
        self.load_games()

    def build(self):
        theme = self.theme
        self.title_item = self.canvas.create_text(
            PAD + 4, 16, text="The Menu", anchor="nw", fill=theme.text,
            font=("Segoe UI", 21, "bold"),
        )
        self.rule_item = self.canvas.create_line(
            0, HEADER_HEIGHT, 10, HEADER_HEIGHT, fill=theme.panel_soft
        )

        self.menu_panel = Panel(self.canvas, theme, 0, 0, 10, 10, title="")
        self.pour_panel = Panel(self.canvas, theme, 0, 0, 10, 10,
                                title=f"Fuji's Pour today is {theme.name}.")

        self.menu = MenuList(self.canvas, theme, self.select_game)
        self.drink_note = self.canvas.create_text(
            0, 0, text="", anchor="n", fill=theme.text_muted,
            font=("Segoe UI", 9), width=10,
        )

        self.buttons = {
            "unpack": Button(self.canvas, theme, 0, 0, 10, BUTTON_HEIGHT,
                             "Unpack Containers", self.start_unpack, tone="accent"),
            "manager": Button(self.canvas, theme, 0, 0, 10, BUTTON_HEIGHT,
                              "Mod Manager", self.open_mod_manager),
            "creator": Button(self.canvas, theme, 0, 0, 10, BUTTON_HEIGHT,
                              "Mod Creator", self.open_mod_creator),
            "folder": Button(self.canvas, theme, 0, 0, 10, BUTTON_HEIGHT,
                             "Set Game Folder", self.choose_game_folder),
            "taildata": Button(self.canvas, theme, 0, 0, 10, BUTTON_HEIGHT,
                               "Generate Taildata", self.start_taildata),
            "rebuild": Button(self.canvas, theme, 0, 0, 10, BUTTON_HEIGHT,
                              "Rebuild Containers", self.start_rebuild),
            "repack": Button(self.canvas, theme, 0, 0, 10, BUTTON_HEIGHT,
                             "Repack Subcontainer", self.start_repack),
            "diagnostics": Button(self.canvas, theme, 0, 0, 10, BUTTON_HEIGHT,
                                  "Diagnostics", self.open_diagnostics),
            "reveal": Button(self.canvas, theme, 0, 0, 10, BUTTON_HEIGHT,
                             "Open Unpack Folder", self.open_unpack_folder),
        }
        self.button_order = ["unpack", "manager", "creator", "folder",
                             "taildata", "diagnostics", "rebuild", "repack",
                             "reveal"]

        self.gauge = GlassGauge(self.canvas, theme, 0, 0, height=GAUGE_HEIGHT)
        self.gauge.set_caption("")
        self.set_buttons_enabled(False)

    def load_games(self):
        try:
            result = self.backend.call("games")
        except BackendError as exc:
            messagebox.showerror("GokonSoftworks", str(exc), parent=self.root)
            return

        self.games = result.get("games", [])
        self.by_id = {game["game_id"]: game for game in self.games}
        DYNASTY = ("DW7XL", "DW8XL", "DW8E", "DW9", "DW4H", "DW6", "DW6E")
        entries = {game["game_id"]: (game["game_id"], game["display_name"],
                                     game["game_id"])
                   for game in self.games}
        first = [entries[k] for k in DYNASTY if k in entries]
        rest = [entries[game["game_id"]] for game in self.games
                if game["game_id"] not in DYNASTY]
        self.menu.set_pages([("Dynasty Warriors", first),
                             ("Miscellaneous", rest)])

        if self.game_id not in self.by_id:
            self.game_id = self.games[0]["game_id"] if self.games else ""
        if self.game_id:
            self.select_game(self.game_id)
        self.relayout()

    @property
    def game(self) -> dict | None:
        return self.by_id.get(self.game_id)

    def game_dirs(self) -> dict:
        return self.settings.setdefault("game_dirs", {})

    def game_dir(self) -> Path | None:
        raw = self.game_dirs().get(self.game_id)
        return Path(raw) if raw else None

    def unpack_root(self) -> Path:
        return unpack_root()

    def select_game(self, game_id: str):
        if game_id not in self.by_id:
            return
        self.game_id = game_id
        self.settings["last_game"] = game_id
        save_settings(self.project_root, self.settings)
        self.menu.select(game_id)
        self.refresh_pour()

    def refresh_pour(self):
        game = self.game
        folder = self.game_dir()
        ready = folder is not None and folder.is_dir()

        self.set_buttons_enabled(ready)
        self.buttons["folder"].set_enabled(True)

        if not ready:
            self.pour_to(0.0, snap=True)
            self.canvas.itemconfig(self.drink_note, text="waiting on a game folder")
            return

        root = self.unpack_root()
        done = game is not None and (root / game["unpack_folder"]).is_dir()
        self.pour_to(1.0 if done else 0.0, snap=True)
        self.canvas.itemconfig(self.drink_note, text="poured" if done else "not poured yet")

    def has_rebuildable(self) -> bool:
        game = self.game
        return bool(game) and any(not part.get("patchable", True)
                                  for part in game.get("parts") or [])

    def set_buttons_enabled(self, enabled: bool):
        for key in ("unpack", "manager", "creator", "reveal", "taildata"):
            self.buttons[key].set_enabled(enabled)
        self.buttons["diagnostics"].set_enabled(True)
        self.buttons["repack"].set_enabled(True)
        self.buttons["rebuild"].set_enabled(enabled and self.has_rebuildable())

    def pour_to(self, fraction: float, snap: bool = False):
        self.pour_target = max(0.0, min(1.0, fraction))
        if snap:
            self.pour_level = self.pour_target
            self.gauge.set_fraction(self.pour_level)
            return
        if self.pour_job is None:
            self.pour_job = self.canvas.after(30, self.pour_step)

    def pour_step(self):
        self.pour_job = None
        gap = self.pour_target - self.pour_level
        if abs(gap) < 0.002:
            self.pour_level = self.pour_target
            self.gauge.set_fraction(self.pour_level)
            return
        self.pour_level += gap * 0.18
        self.gauge.set_fraction(self.pour_level)
        self.pour_job = self.canvas.after(30, self.pour_step)

    def on_configure(self, event):
        if (event.width, event.height) != self.size:
            self.size = (event.width, event.height)
            self.relayout()

    def relayout(self):
        width, height = self.size
        if width <= 1 or height <= 1:
            width, height = WINDOW_WIDTH, WINDOW_HEIGHT
        self.layout(width, height)

    def layout(self, width: int, height: int):
        self.canvas.coords(self.rule_item, 0, HEADER_HEIGHT, width, HEADER_HEIGHT)
        rows_per_column = (self.menu.rows_per_page() + MENU_COLUMNS - 1) // max(1, MENU_COLUMNS)
        menu_height = rows_per_column * MenuList.ROW_HEIGHT + 104
        menu_top = HEADER_HEIGHT + PAD
        self.menu_panel.place(PAD, menu_top, width - PAD * 2, menu_height)
        self.menu.place(PAD + 26, menu_top + 74, width - PAD * 2 - 52, MENU_COLUMNS)
        pour_top = menu_top + menu_height + PAD
        pour_height = max(200, height - pour_top - PAD)
        self.pour_panel.place(PAD, pour_top, width - PAD * 2, pour_height)
        glass_x = PAD + 40
        glass_y = pour_top + pour_height - self.gauge.height - PAD
        self.gauge.place(int(glass_x), int(glass_y))
        centre = glass_x + self.gauge.width / 2
        self.canvas.coords(self.drink_note, centre, glass_y - 26)
        self.canvas.itemconfig(self.drink_note, width=self.gauge.width + 120)
        button_x = width - PAD - 240
        button_y = pour_top + 34
        for key in self.button_order:
            self.buttons[key].place(button_x, button_y, 220, BUTTON_HEIGHT)
            button_y += BUTTON_HEIGHT + BUTTON_GAP

    def start_unpack(self):
        game = self.game
        folder = self.game_dir()
        if game is None or folder is None or self.worker.busy:
            return

        out_root = self.unpack_root()
        state = taildata_dir()
        self.set_buttons_enabled(False)
        self.buttons["folder"].set_enabled(False)
        self.pour_to(0.0, snap=True)
        self.canvas.itemconfig(self.drink_note, text="pouring")

        game_id = game["game_id"]
        backend = self.backend

        def job(report):
            def progress(done, total, _message):
                if total > 0:
                    report("progress", done / total)
            return backend.call(
                "unpack", progress=progress, game=game_id, dir=str(folder),
                out=str(out_root), state=str(state), ref=str(filenames_dir()),
            )

        self.worker.start(job, {
            "progress": self.on_unpack_progress,
            "done": self.on_unpack_done,
            "error": self.on_unpack_error,
        }, name="gokon-unpack")

    def on_unpack_progress(self, fraction: float):
        self.pour_to(fraction)

    def on_unpack_done(self, result):
        self.pour_to(1.0)
        self.canvas.itemconfig(self.drink_note, text="poured")
        self.set_buttons_enabled(True)
        self.buttons["folder"].set_enabled(True)

    def on_unpack_error(self, exc):
        self.pour_to(0.0, snap=True)
        self.canvas.itemconfig(self.drink_note, text="spilled")
        self.set_buttons_enabled(True)
        self.buttons["folder"].set_enabled(True)
        log.error("Unpack failed", exc_info=exc)
        messagebox.showerror("Unpack", str(exc), parent=self.root)

    def choose_game_folder(self):
        game = self.game
        if game is None:
            return
        current = self.game_dir()
        chosen = filedialog.askdirectory(
            title=f"Where {game['containers'][0]} lives for {game['display_name']}",
            initialdir=str(current) if current else str(self.project_root),
            parent=self.root,
        )
        if not chosen:
            return

        folder = Path(chosen)
        found = containers_in(game, folder)
        if not found:
            messagebox.showwarning(
                "Game Folder",
                f"No containers for {game['display_name']} in that folder.\n\n"
                f"Looking for: {', '.join(game['containers'])}",
                parent=self.root,
            )
            return

        self.game_dirs()[self.game_id] = str(folder)
        save_settings(self.project_root, self.settings)
        self.refresh_pour()

    def start_taildata(self):
        game = self.game
        folder = self.game_dir()
        if game is None or folder is None or self.worker.busy:
            return

        state = taildata_dir()
        self.set_buttons_enabled(False)
        self.buttons["folder"].set_enabled(False)
        self.pour_to(0.0, snap=True)
        self.canvas.itemconfig(self.drink_note, text="scanning containers")

        game_id = game["game_id"]
        backend = self.backend

        def job(report):
            def progress(done, total, _message):
                if total > 0:
                    report("progress", done / total)
            return backend.call(
                "unpack", progress=progress, game=game_id, dir=str(folder),
                out=str(self.unpack_root()), state=str(state),
                ref=str(filenames_dir()), write=False,
            )

        def done(result):
            self.pour_to(1.0)
            self.canvas.itemconfig(self.drink_note, text="taildata written")
            self.set_buttons_enabled(True)
            self.buttons["folder"].set_enabled(True)
            messagebox.showinfo(
                "Generate Taildata",
                f"Taildata written for {game['display_name']}.\n\n"
                f"{result.get('entries_seen', 0):,} container entries scanned, "
                "nothing unpacked.\n\n"
                f"{result['manifest']}",
                parent=self.root,
            )

        self.worker.start(job, {
            "progress": self.on_unpack_progress,
            "done": done,
            "error": self.on_unpack_error,
        }, name="gokon-taildata")

    def start_rebuild(self):
        game = self.game
        folder = self.game_dir()
        if game is None or folder is None or self.worker.busy:
            return
        if not self.has_rebuildable():
            return

        source = self.unpack_root()
        if not (source / game["unpack_folder"]).is_dir():
            messagebox.showwarning(
                "Rebuild Containers",
                "Unpack the containers first, there's nothing to rebuild from.",
                parent=self.root,
            )
            return

        out_dir = source / f"{game['game_id']}_Rebuilt"
        self.set_buttons_enabled(False)
        self.buttons["folder"].set_enabled(False)
        self.pour_to(0.0, snap=True)
        self.canvas.itemconfig(self.drink_note, text="rebuilding")

        backend = self.backend

        def job(report):
            def progress(done, total, _message):
                if total > 0:
                    report("progress", done / total)
            return backend.call(
                "rebuild", progress=progress, game=game["game_id"],
                dir=str(folder), src=str(source), out=str(out_dir),
                ref=str(filenames_dir()),
            )

        def done(result):
            self.pour_to(1.0)
            self.canvas.itemconfig(self.drink_note, text="rebuilt")
            self.set_buttons_enabled(True)
            self.buttons["folder"].set_enabled(True)
            lines = [
                f"{row['container']}: {row['entries']:,} entries, "
                f"{row['bytes'] / 1_000_000:,.1f} MB"
                for row in result.get("rebuilt", [])
            ]
            messagebox.showinfo(
                "Rebuild Containers",
                "Rebuilt:\n  " + "\n  ".join(lines) +
                "\n\nThey keep the same folder layout as the game, so copy the "
                "contents over your install."
                f"\n\n{out_dir}",
                parent=self.root,
            )

        self.worker.start(job, {
            "progress": self.on_unpack_progress,
            "done": done,
            "error": self.on_unpack_error,
        }, name="gokon-rebuild")

    def start_repack(self):
        if self.worker.busy:
            return

        folder = filedialog.askdirectory(
            title="Select the unpacked subcontainer folder",
            parent=self.root,
        )
        if not folder:
            return
        folder_path = Path(folder)
        expected = folder_path.name
        beside = sorted(p.name for p in folder_path.parent.iterdir()
                        if p.stem == expected and p.is_file())
        named = beside[0] if len(beside) == 1 else ""

        original = filedialog.askopenfilename(
            title=(f"Select the original file {named}" if named else
                   f"Select the original file named {expected}"),
            parent=self.root,
            initialdir=str(folder_path.parent),
            initialfile=named or expected,
        )
        if not original:
            return

        source = Path(original)
        if source.stem != expected:
            messagebox.showerror(
                "Repack Subcontainer",
                f"That folder and that file dont belong together, so nothing "
                f"was written.\n\n"
                f"The folder is named {expected}, which means it was unpacked "
                f"from {named or expected + ' with an extension after it'}.\n\n"
                f"You picked {source.name}.\n\n"
                "Rebuilding a subcontainer against another one's layout makes a "
                "file the game cant read.",
                parent=self.root,
            )
            return

        out_path = source.with_name(f"{source.stem}_repacked{source.suffix}")

        self.set_buttons_enabled(False)
        self.buttons["folder"].set_enabled(False)
        self.pour_to(0.0, snap=True)
        self.canvas.itemconfig(self.drink_note, text="repacking")

        backend = self.backend

        def job(report):
            def progress(done, total, _message):
                if total > 0:
                    report("progress", done / total)
            return backend.call(
                "repack", progress=progress, folder=folder,
                original=str(source), out=str(out_path),
            )

        def done(result):
            self.pour_to(1.0)
            self.canvas.itemconfig(self.drink_note, text="repacked")
            self.set_buttons_enabled(True)
            self.buttons["folder"].set_enabled(True)
            before = result.get("original_size", 0)
            after = result.get("rebuilt_size", 0)
            verdict = ("It came back identical to the original, "
                       "so it's good."
                       if result.get("unchanged") else
                       f"It differs from the original ({before:,} -> {after:,} "
                       f"bytes), expected if you edited anything "
                       f"inside the folder.")
            messagebox.showinfo(
                "Repack Subcontainer",
                f"Rebuilt {source.name} from its folder.\n\n{verdict}\n\n"
                f"Written to:\n{out_path}\n\n",
                parent=self.root,
            )

        self.worker.start(job, {
            "progress": self.on_unpack_progress,
            "done": done,
            "error": self.on_unpack_error,
        }, name="gokon-repack")

    def open_diagnostics(self):
        report = diagnose()
        text = report.report_text()
        if report.has_errors:
            messagebox.showerror("Diagnostics", text, parent=self.root)
        elif report.has_warnings:
            messagebox.showwarning("Diagnostics", text, parent=self.root)
        else:
            messagebox.showinfo("Diagnostics", text, parent=self.root)

    def open_unpack_folder(self):
        game = self.game
        if game is None:
            return
        target = self.unpack_root() / game["unpack_folder"]
        if not target.is_dir():
            messagebox.showinfo("Unpack Folder", "Nothing unpacked yet.", parent=self.root)
            return
        subprocess.Popen(["explorer", str(target)])

    def open_tool(self, attribute: str, factory, *args):
        window = getattr(self, attribute)
        if window is not None:
            try:
                if window.winfo_exists():
                    window.lift()
                    window.focus_force()
                    return
            except tk.TclError:
                pass
        try:
            setattr(self, attribute, factory(*args))
        except Exception as exc:
            log.exception("couldnt open %s", attribute)
            messagebox.showerror("GokonSoftworks", str(exc), parent=self.root)

    def open_mod_manager(self):
        from .bar import ModManagerWindow

        self.open_tool("mod_window", ModManagerWindow,
                       self.root, self.theme, self.game, self.game_dir(), self.backend)

    def open_mod_creator(self):
        from .mixer import ModCreatorWindow

        self.open_tool("creator_window", ModCreatorWindow,
                       self.root, self.theme, self.game, self.unpack_root(), self.backend)

    def on_close(self):
        try:
            self.backend.close()
        except Exception:
            pass
        self.root.destroy()
