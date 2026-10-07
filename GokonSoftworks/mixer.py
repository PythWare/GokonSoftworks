from __future__ import annotations
import json
import tkinter as tk
from pathlib import Path
from tkinter import filedialog, messagebox
from .recipe import MAX_PREVIEW_IMAGES, PACKAGE_EXTENSION, collect_source_files
from .refresh import (Button, Panel, ProgressBar, StatusLog, Theme, Worker,
                      keep_front, own_window)
from .wetworks import GokonSoftworksError, human_size, log, mods_dir, taildata_path

WINDOW_WIDTH = 780
WINDOW_HEIGHT = 860
PAD = 18
ROW_HEIGHT = 30
LABEL_WIDTH = 104
BUTTON_HEIGHT = 30

LIQUIDS = (
    ("Merlot", "#7B1E3A"),
    ("Rose", "#C25A7C"),
    ("Amber", "#C88A3C"),
    ("Absinthe", "#7BC24A"),
    ("Curacao", "#3C8AC8"),
    ("Violette", "#8E5BC8"),
    ("Midori", "#48B08A"),
    ("Sakura", "#E26F9E"),
)
LIQUID_COLOURS = dict(LIQUIDS)

class ModCreatorWindow(tk.Toplevel):

    def __init__(self, master, theme: Theme, game: dict, unpack_root, backend, on_built=None):
        super().__init__(master)
        self.theme = theme
        self.game = game or {}
        self.game_id = self.game.get("game_id", "")
        self.unpack_root = Path(unpack_root) if unpack_root else None
        self.backend = backend
        self.on_built = on_built

        if self.unpack_root is None:
            raise GokonSoftworksError("Set the game folder before opening the mod creator.")

        self.manifest_path = taildata_path(self.game_id)
        self.records: dict = {}
        self.matched: list[tuple[str, Path]] = []
        self.source_folder: Path | None = None
        self.image_paths: list[Path] = []
        self.audio_path: Path | None = None
        self.mods_dir = mods_dir()
        self.busy = False
        self.embedded: list[int] = []

        self.title(f"Mod Creator, {self.game.get('display_name', '')}")
        self.geometry(f"{WINDOW_WIDTH}x{WINDOW_HEIGHT}")
        self.resizable(False, False)
        self.configure(bg=theme.bg)
        own_window(self, master)

        self.canvas = tk.Canvas(self, bg=theme.bg, highlightthickness=0)
        self.canvas.pack(fill="both", expand=True)
        self.worker = Worker(self.canvas)

        self.liquid_var = tk.StringVar(value=LIQUIDS[0][0])
        self.protocol("WM_DELETE_WINDOW", self.close)
        self.build()
        self.load_manifest()

    def label(self, x, y, text) -> int:
        return self.canvas.create_text(
            x, y + 5, text=text, anchor="nw", fill=self.theme.text_muted,
            font=("Segoe UI", 9),
        )

    def entry(self, x, y, width, value: str = "") -> tk.Entry:
        widget = tk.Entry(
            self.canvas, bg=self.theme.field, fg=self.theme.text,
            insertbackground=self.theme.accent, relief="flat",
            highlightthickness=1, highlightbackground=self.theme.panel_soft,
            highlightcolor=self.theme.accent, font=("Segoe UI", 10),
        )
        if value:
            widget.insert(0, value)
        self.embedded.append(
            self.canvas.create_window(x, y, window=widget, anchor="nw",
                                      width=width, height=ROW_HEIGHT - 4)
        )
        return widget

    def build(self):
        theme = self.theme
        inner = WINDOW_WIDTH - PAD * 2

        self.canvas.create_text(
            PAD + 2, 20,
            text=f"Bottle a folder of edited {self.game.get('display_name', '')} files "
                 f"into one {PACKAGE_EXTENSION} package",
            anchor="nw", fill=theme.accent, font=("Segoe UI", 9), width=inner,
        )

        top = 48
        self.form_panel = Panel(self.canvas, theme, PAD, top, inner, 476, title="Recipe Card")

        x = PAD + 16
        field_x = x + LABEL_WIDTH
        field_w = inner - LABEL_WIDTH - 150
        y = top + 44

        self.label(x, y, "Source folder")
        self.source_entry = self.entry(field_x, y, field_w)
        self.source_entry.configure(state="readonly", readonlybackground=theme.field)
        self.browse_button = Button(self.canvas, theme, field_x + field_w + 10, y, 118,
                                    BUTTON_HEIGHT - 4, "Browse", self.choose_source)
        y += ROW_HEIGHT + 8

        self.label(x, y, "Mod name")
        self.name_entry = self.entry(field_x, y, field_w)
        y += ROW_HEIGHT + 8

        self.label(x, y, "Author")
        half = int(field_w * 0.55)
        self.author_entry = self.entry(field_x, y, half)
        self.canvas.create_text(
            field_x + half + 14, y + 5, text="Version", anchor="nw",
            fill=theme.text_muted, font=("Segoe UI", 9),
        )
        self.version_entry = self.entry(field_x + half + 72, y, field_w - half - 72, "1.0")
        y += ROW_HEIGHT + 8

        self.label(x, y, "Liquid")
        self.liquid_menu = tk.OptionMenu(self.canvas, self.liquid_var,
                                         *[name for name, _c in LIQUIDS],
                                         command=self.on_liquid)
        self.liquid_menu.configure(
            bg=theme.panel_soft, fg=theme.text, activebackground=theme.accent,
            activeforeground=theme.bg, relief="flat", highlightthickness=0,
            font=("Segoe UI", 9), anchor="w",
        )
        menu = self.liquid_menu["menu"]
        menu.configure(bg=theme.panel, fg=theme.text, relief="flat")
        for index, (name, colour) in enumerate(LIQUIDS):
            menu.entryconfigure(index, foreground=colour, activeforeground=colour)
        self.embedded.append(
            self.canvas.create_window(field_x, y, window=self.liquid_menu, anchor="nw",
                                      width=200, height=26)
        )
        self.swatch = self.canvas.create_oval(
            field_x + 216, y + 3, field_x + 216 + 20, y + 23,
            fill=self.liquid_colour(), outline="",
        )
        y += ROW_HEIGHT + 8

        self.label(x, y, "Description")
        self.description_text = tk.Text(
            self.canvas, bg=theme.field, fg=theme.text, insertbackground=theme.accent,
            relief="flat", highlightthickness=1, highlightbackground=theme.panel_soft,
            highlightcolor=theme.accent, font=("Segoe UI", 9), wrap="word",
        )
        self.embedded.append(
            self.canvas.create_window(field_x, y, window=self.description_text, anchor="nw",
                                      width=field_w, height=104)
        )
        y += 118

        self.label(x, y, "Preview images")
        self.images_item = self.canvas.create_text(
            field_x, y + 5, text="None chosen", anchor="nw", fill=theme.text_muted,
            font=("Segoe UI", 9), width=field_w,
        )
        self.images_button = Button(self.canvas, theme, field_x + field_w + 10, y, 118,
                                    BUTTON_HEIGHT - 4, "Add Images", self.choose_images)
        y += ROW_HEIGHT + 8

        self.label(x, y, "Theme WAV")
        self.audio_item = self.canvas.create_text(
            field_x, y + 5, text="None chosen", anchor="nw", fill=theme.text_muted,
            font=("Segoe UI", 9), width=field_w,
        )
        self.audio_button = Button(self.canvas, theme, field_x + field_w + 10, y, 118,
                                   BUTTON_HEIGHT - 4, "Choose WAV", self.choose_audio)
        y += ROW_HEIGHT + 8

        self.label(x, y, "Matched files")
        self.matched_item = self.canvas.create_text(
            field_x, y + 5, text="Nothing picked yet", anchor="nw",
            fill=theme.text_muted, font=("Segoe UI", 9), width=field_w,
        )
        self.recheck_button = Button(self.canvas, theme, field_x + field_w + 10, y, 118,
                                     BUTTON_HEIGHT - 4, "Re-check", self.recheck_source)
        y += ROW_HEIGHT + 10

        self.canvas.create_text(
            x, y, text=f"Bottles are written into {self.mods_dir}", anchor="nw",
            fill=theme.text_muted, font=("Segoe UI", 8), width=inner - 32,
        )

        actions_y = top + 492
        action_h = BUTTON_HEIGHT + 6
        self.build_button = Button(
            self.canvas, theme, PAD, actions_y, 176, action_h,
            "Bottle It", self.build_package, tone="accent",
        )
        self.clear_button = Button(self.canvas, theme, PAD + 186, actions_y, 150, action_h,
                                   "Clear Extras", self.clear_extras)
        bar_x = PAD + 348
        self.progress = ProgressBar(self.canvas, theme, bar_x, actions_y + 14, inner - 348)

        log_top = actions_y + action_h + 14
        self.status_log = StatusLog(
            self.canvas, theme, PAD, log_top, inner,
            WINDOW_HEIGHT - log_top - PAD,
        )

        self.build_button.set_enabled(False)
        self.say("Pick the folder holding your edited files to begin.", "accent")
        self.say(
            "The folder has to keep the layout the unpack made. for example "
            f"{self.game.get('unpack_folder', 'Game_Unpacked')}/Pack_00/modded file "
            "because that is how each file is matched back to its container slot. "
            f"Modded/{self.game.get('unpack_folder', 'Game_Unpacked')}/Pack_00/modded file works too."
        )

    def say(self, message: str, tone: str = "muted"):
        try:
            self.status_log.write(message, tone)
        except Exception:
            log.info("mixer: %s", message)

    def on_liquid(self, _value=None):
        self.canvas.itemconfig(self.swatch, fill=self.liquid_colour())

    def liquid_colour(self) -> str:
        return LIQUID_COLOURS.get(self.liquid_var.get(), LIQUIDS[0][1])

    def load_manifest(self):
        try:
            data = json.loads(self.manifest_path.read_text(encoding="utf-8"))
            self.records = data.get("files", {})
        except (OSError, json.JSONDecodeError):
            self.records = {}
            self.say("No taildata manifest yet. Unpack the containers first.", "danger")

    def choose_source(self):
        chosen = filedialog.askdirectory(
            title="Select the folder holding your edited files",
            initialdir=str(self.unpack_root), parent=self,
        )
        if not chosen:
            return

        self.source_folder = Path(chosen)
        self.source_entry.configure(state="normal")
        self.source_entry.delete(0, "end")
        self.source_entry.insert(0, str(self.source_folder))
        self.source_entry.configure(state="readonly")
        if not self.name_entry.get().strip():
            self.name_entry.insert(0, self.source_folder.name)
        self.recheck_source()

    def recheck_source(self):
        if self.source_folder is None:
            self.say("Pick a source folder first.", "danger")
            return
        if not self.records:
            self.say("No manifest to match against. Unpack the containers first.", "danger")
            return

        self.matched = collect_source_files(self.source_folder, self.records)
        if self.matched:
            self.canvas.itemconfig(
                self.matched_item,
                text=f"{len(self.matched)} files match the unpack",
                fill=self.theme.ok,
            )
            self.say(f"{len(self.matched)} files in this folder match the unpack.", "ok")
            for key, _path in self.matched[:6]:
                self.say(f"   {key}")
            if len(self.matched) > 6:
                self.say(f"   also {len(self.matched) - 6} more")
        else:
            self.canvas.itemconfig(
                self.matched_item, text="Nothing matched", fill=self.theme.danger
            )
            self.say("Nothing in this folder matches the unpacked layout.", "danger")
        self.build_button.set_enabled(bool(self.matched) and not self.busy)

    def choose_images(self):
        chosen = filedialog.askopenfilenames(
            title="Select preview images",
            filetypes=[("Images", "*.png *.jpg *.jpeg *.bmp *.gif"), ("All files", "*.*")],
            parent=self,
        )
        if not chosen:
            return
        self.image_paths = [Path(p) for p in chosen][:MAX_PREVIEW_IMAGES]
        if len(chosen) > MAX_PREVIEW_IMAGES:
            self.say(f"Only the first {MAX_PREVIEW_IMAGES} previews are kept.", "danger")
        self.refresh_extras()

    def choose_audio(self):
        chosen = filedialog.askopenfilename(
            title="Select a WAV to bundle", filetypes=[("WAV audio", "*.wav")], parent=self
        )
        if not chosen:
            return
        self.audio_path = Path(chosen)
        self.refresh_extras()

    def clear_extras(self):
        self.image_paths = []
        self.audio_path = None
        self.refresh_extras()
        self.say("Cleared the previews and the theme tune.")

    def refresh_extras(self):
        if self.image_paths:
            names = ", ".join(path.name for path in self.image_paths)
            self.canvas.itemconfig(self.images_item, text=names, fill=self.theme.text)
        else:
            self.canvas.itemconfig(self.images_item, text="None chosen",
                                   fill=self.theme.text_muted)
        if self.audio_path:
            self.canvas.itemconfig(self.audio_item, text=self.audio_path.name,
                                   fill=self.theme.text)
        else:
            self.canvas.itemconfig(self.audio_item, text="None chosen",
                                   fill=self.theme.text_muted)

    def clear_card(self):
        self.source_folder = None
        self.matched = []
        self.source_entry.configure(state="normal")
        self.source_entry.delete(0, "end")
        self.source_entry.configure(state="readonly")
        self.canvas.itemconfig(self.matched_item, text="Nothing picked yet",
                               fill=self.theme.text_muted)
        self.build_button.set_enabled(False)
        self.say("Cleared the card.")

    def build_package(self):
        if self.busy or not self.matched:
            return

        name = self.name_entry.get().strip() or "New Brew"
        self.mods_dir.mkdir(parents=True, exist_ok=True)
        out_path = self.mods_dir / f"{name.replace(' ', '_')}{PACKAGE_EXTENSION}"

        entries = []
        compressed = 0
        for key, path in self.matched:
            record = self.records[key]
            entry = {
                "file": str(path),
                "idx_marker": int(record["idx_marker"]),
                "entry_off": int(record["entry_off"]),
            }
            if record.get("container"):
                entry["container"] = record["container"]
            codec = record.get("codec")
            if codec:
                entry["codec"] = codec
                entry["codec_version"] = str(record.get("codec_version", "1.01"))
                entry["codec_chunk"] = int(record.get("codec_chunk", 0))
                compressed += 1
            entries.append(entry)

        meta = {
            "name": name,
            "author": self.author_entry.get().strip() or "Unknown",
            "mod_version": self.version_entry.get().strip() or "1.0",
            "description": self.description_text.get("1.0", "end").strip(),
            "bottle": "bordeaux",
            "color": self.liquid_colour(),
        }

        self.set_busy(True)
        self.progress.set_fraction(0.0)
        note = f", {compressed} recompressed" if compressed else ""
        self.say(f"Bottling {name}, {len(entries)} files{note}", "accent")
        backend, game_id = self.backend, self.game_id
        image_paths, audio_path = list(self.image_paths), self.audio_path

        def job(report):
            def progress(done, total, _message):
                if total > 0:
                    report("progress", done / total)
            return backend.call("bottle", progress=progress, game=game_id,
                                out=str(out_path), meta=meta, entries=entries,
                                images=[str(p) for p in image_paths],
                                audio=str(audio_path) if audio_path else "")

        def on_progress(fraction):
            self.progress.set_fraction(fraction)

        def done(result):
            self.set_busy(False)
            self.progress.set_fraction(1.0)
            self.say(
                f"{out_path.name}: {result['entries']} files, "
                f"{human_size(result['payload_bytes'])}, "
                f"{result['encrypted_entries']} scrambled, "
                f"{result['images']} previews"
                + (", theme tune bundled" if result.get('audio_bytes') else "") + ".",
                "ok",
            )
            keep_front(self)
            if self.on_built:
                self.on_built(out_path)

        def failed(exc):
            self.set_busy(False)
            self.progress.set_fraction(0.0)
            log.error("Bottling failed", exc_info=exc)
            self.say(str(exc), "danger")
            messagebox.showerror("Mod Creator", str(exc), parent=self)
            keep_front(self)

        self.worker.start(
            job, {"progress": on_progress, "done": done, "error": failed}, name="gokon-mixer"
        )

    def set_busy(self, busy: bool):
        self.busy = busy
        self.browse_button.set_enabled(not busy)
        self.recheck_button.set_enabled(not busy)
        self.images_button.set_enabled(not busy)
        self.audio_button.set_enabled(not busy)
        self.clear_button.set_enabled(not busy)
        self.build_button.set_enabled(not busy and bool(self.matched))

    def close(self):
        self.destroy()
