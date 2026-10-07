from __future__ import annotations
import subprocess, io
import tkinter as tk
import tkinter.font as tkfont
from dataclasses import dataclass, field
from pathlib import Path
from tkinter import messagebox
from PIL import Image, ImageChops, ImageDraw, ImageOps, ImageTk
from .ledger import load_tab
from .recipe import Recipe, read_audio, read_images, scan_folder
from .refresh import (
    Button,
    keep_front,
    Panel,
    ProgressBar,
    StatusLog,
    Theme,
    Worker,
    fit_line,
    letterbox,
    load_png,
    load_settings,
    own_window,
    save_settings,
    scale_to_height,
    sparkle_photo,
    spawn_sparkle_cluster,
    wrap_lines,
)
from .returns import (apply_recipe, capture_container_sizes, container_file, disable_all,
                      disable_recipe)
from .wetworks import (
    PROJECT_ROOT,
    GokonSoftworksError,
    WinMemoryAudioPlayer,
    human_size,
    log,
    mods_dir,
    unpack_root,
)

WINDOW_WIDTH = 1240
WINDOW_HEIGHT = 800
MIN_WIDTH = 960
MIN_HEIGHT = 760
DETAILS_HEIGHT = 560

PAD = 16
TOP_HEIGHT = 62
SIDE_WIDTH = 380
BUTTON_HEIGHT = 34
BUTTON_GAP = 8

PREVIEW_WIDTH = SIDE_WIDTH - PAD * 3
PREVIEW_HEIGHT = 196
PREVIEW_Y = PAD + 86
NAV_Y = PREVIEW_Y + PREVIEW_HEIGHT + 8
CLASH_Y = NAV_Y + 32
META_Y = NAV_Y + 54
DESC_Y = META_Y + 108
DESC_MIN = 96
LOG_MIN = 110

BOTTLE_HEIGHT = 230
BOTTLE_GAP = 26
SHELF_MARGIN = 34
SHELF_PLANK = 18
SHELF_CAPTION = 26
SHELF_OVERHANG = 16
SHELF_BAR = 8
SHELF_THUMB_MIN = 24
LABEL_BOX = (0.085, 0.4333, 0.915, 0.7564)
SHOULDER = 0.3431
GLASS_LUMA_MAX = 150

SPARKLE_TICK_MS = 33
SPARKLE_SPAWN_MS = 1400
SPARKLE_MAX_PER_BOTTLE = 7
CLASH_COLOURS = ("#E0913C", "#4E9CC4", "#B85A9C", "#48B08A", "#D24A6E", "#8E5BC8")
CLASH_NAMES_SHOWN = 2

def shade(colour: str, factor: float) -> tuple[int, int, int]:
    value = colour.lstrip("#")
    parts = [int(value[i:i + 2], 16) for i in (0, 2, 4)]
    return tuple(max(0, min(255, round(part * factor))) for part in parts)

def render_bottle(base: Image.Image, colour: str, filled: bool) -> Image.Image:
    width, height = base.size
    alpha = base.getchannel("A")
    luma = base.convert("RGB").convert("L")
    dark = luma.point(lambda value: 255 if value <= GLASS_LUMA_MAX else 0)
    solid = alpha.point(lambda value: 255 if value >= 128 else 0)
    mask = ImageChops.multiply(dark, solid)
    cut = ImageDraw.Draw(mask)
    cut.rectangle([0, 0, width, int(height * SHOULDER)], fill=0)
    cut.rectangle(
        [int(LABEL_BOX[0] * width), int(LABEL_BOX[1] * height),
         int(LABEL_BOX[2] * width), int(LABEL_BOX[3] * height)],
        fill=0,
    )

    if filled:
        tinted = ImageOps.colorize(
            luma, black=shade(colour, 0.30), white=shade(colour, 1.55), mid=shade(colour, 1.0)
        )
    else:
        tinted = ImageOps.colorize(luma, black="#08080a", white="#4c4c55", mid="#2a2a30")

    out = base.copy()
    out.paste(tinted.convert("RGBA"), (0, 0), mask)
    return out

def find_clashes(recipes: list) -> dict[str, int]:
    parent: dict[str, str] = {recipe.mod_id: recipe.mod_id for recipe in recipes}

    def find(item):
        while parent[item] != item:
            parent[item] = parent[parent[item]]
            item = parent[item]
        return item

    holder: dict[tuple, str] = {}
    clashed: set[str] = set()
    for recipe in recipes:
        for entry in recipe.entries:
            slot = (entry.idx_marker, entry.entry_off)
            first = holder.setdefault(slot, recipe.mod_id)
            if first == recipe.mod_id:
                continue
            parent[find(first)] = find(recipe.mod_id)
            clashed.add(first)
            clashed.add(recipe.mod_id)

    groups: dict[str, int] = {}
    numbering: dict[str, int] = {}
    for recipe in recipes:
        mod_id = recipe.mod_id
        if mod_id not in clashed:
            continue
        root = find(mod_id)
        if root not in numbering:
            numbering[root] = len(numbering)
        groups[mod_id] = numbering[root]
    return groups

@dataclass
class Pour:
    recipe: Recipe
    enabled: bool
    problem: str = ""
    clash_colour: str = ""

    @property
    def mod_id(self) -> str:
        return self.recipe.mod_id

class Bottle:

    def __init__(self, canvas, theme: Theme, shelf):
        self.canvas = canvas
        self.theme = theme
        self.shelf = shelf
        self.pour: Pour | None = None
        self.x = 0.0
        self.y = 0.0
        self.sparkles: list = []
        self.sparkle_items: list[int] = []
        self.halo = canvas.create_rectangle(
            0, 0, 0, 0, outline="", width=2, state="hidden", tags="shelf"
        )
        self.image_item = canvas.create_image(
            0, 0, anchor="center", state="hidden", tags="shelf"
        )
        self.label_item = canvas.create_text(
            0, 0, text="", anchor="center", fill="#3b3226", font=shelf.label_font,
            justify="center", width=shelf.label_width, state="hidden", tags="shelf",
        )
        self.caption_item = canvas.create_text(
            0, 0, text="", anchor="n", font=("Segoe UI", 7), justify="center",
            width=shelf.bottle_width + BOTTLE_GAP, state="hidden", tags="shelf",
        )
        self.clash_item = canvas.create_oval(
            0, 0, 0, 0, fill="", outline="", state="hidden", tags="shelf"
        )
        self.items = [self.halo, self.image_item, self.label_item,
                      self.caption_item, self.clash_item]
        for item in (self.image_item, self.label_item, self.halo):
            shelf.item_lookup[item] = self

    def show(self, pour: Pour, x: float, y: float, selected: bool):
        changed = pour is not self.pour
        self.pour = pour
        self.x, self.y = x, y
        if changed:
            self.clear_sparkles()

        canvas = self.canvas
        shelf = self.shelf
        width, height = shelf.bottle_width, shelf.bottle_height
        centre_x = x + width / 2
        centre_y = y + height / 2

        canvas.coords(self.halo, x - 6, y - 6, x + width + 6, y + height + 6)
        canvas.itemconfigure(
            self.halo, outline=self.theme.accent if selected else "", state="normal"
        )
        canvas.coords(self.image_item, centre_x, centre_y)
        canvas.itemconfigure(self.image_item, image=shelf.bottle_art(pour), state="normal")

        canvas.coords(self.label_item, centre_x, centre_y + shelf.label_offset)
        if changed:
            canvas.itemconfigure(self.label_item, text=shelf.label_lines(pour.recipe.name))
        canvas.itemconfigure(self.label_item, state="normal")

        canvas.coords(self.caption_item, centre_x, y + height + SHELF_PLANK + 6)
        if changed:
            canvas.itemconfigure(
                self.caption_item,
                text=shelf.caption_for(pour),
                fill=self.theme.danger if pour.problem else self.theme.text_muted,
            )
        canvas.itemconfigure(self.caption_item, state="normal")

        if pour.clash_colour:
            radius = 7
            canvas.coords(self.clash_item, x + width - radius * 2, y,
                          x + width, y + radius * 2)
            canvas.itemconfigure(self.clash_item, fill=pour.clash_colour, state="normal")
        else:
            canvas.itemconfigure(self.clash_item, state="hidden")

    def restyle(self, selected: bool):
        if self.pour is None:
            return
        self.canvas.itemconfigure(self.image_item, image=self.shelf.bottle_art(self.pour))
        self.canvas.itemconfigure(
            self.halo, outline=self.theme.accent if selected else ""
        )

    def release(self):
        self.pour = None
        self.clear_sparkles()
        for item in self.items:
            self.canvas.itemconfigure(item, state="hidden")

    def clear_sparkles(self):
        self.sparkles = []
        for item in self.sparkle_items:
            self.canvas.itemconfigure(item, state="hidden")

    def spawn_sparkles(self):
        if self.pour is None or not self.pour.enabled:
            return
        room = SPARKLE_MAX_PER_BOTTLE - len(self.sparkles)
        if room <= 0:
            return
        self.sparkles.extend(
            spawn_sparkle_cluster(0, 0, spread=int(self.shelf.bottle_width * 0.4))[:room]
        )

    def step_sparkles(self):
        if not self.sparkles:
            return
        if self.pour is None or not self.pour.enabled:
            self.clear_sparkles()
            return
        canvas = self.canvas
        while len(self.sparkle_items) < len(self.sparkles):
            self.sparkle_items.append(
                canvas.create_image(0, 0, anchor="center", state="hidden", tags="shelf")
            )
        centre_x = self.x + self.shelf.bottle_width / 2
        centre_y = self.y + self.shelf.bottle_height / 2
        colour = self.pour.recipe.colour
        alive = []
        for index, burst in enumerate(self.sparkles):
            item = self.sparkle_items[index]
            burst.step()
            if burst.finished():
                canvas.itemconfigure(item, state="hidden")
                continue
            if burst.visible:
                canvas.coords(item, centre_x + burst.x, centre_y + burst.y)
                canvas.itemconfigure(
                    item, image=sparkle_photo(colour, burst.frame_index), state="normal"
                )
            alive.append(burst)
        self.sparkles = alive
        for index in range(len(self.sparkles), len(self.sparkle_items)):
            canvas.itemconfigure(self.sparkle_items[index], state="hidden")

    def destroy(self):
        for item in self.items + self.sparkle_items:
            self.shelf.item_lookup.pop(item, None)
            self.canvas.delete(item)
        self.items = []
        self.sparkle_items = []
        self.sparkles = []
        self.pour = None

class ModManagerWindow(tk.Toplevel):

    def __init__(self, master, theme: Theme, game: dict, game_dir, backend):
        super().__init__(master)
        self.theme = theme
        self.game = game or {}
        self.game_id = self.game.get("game_id", "")
        self.game_index = int(self.game.get("game_index", -1))
        self.game_dir = Path(game_dir) if game_dir else None
        self.backend = backend

        self.pours: list[Pour] = []
        self.selected: Pour | None = None
        self.item_lookup: dict[int, Bottle] = {}
        self.busy = False
        self.columns = 1
        self.shelf_size = (0, 0)
        self.pool: list[Bottle] = []
        self.planks: list[tuple[int, int, int]] = []
        self.scroll_px = 0
        self.shelf_dragging = False
        self.shelf_grab = 0.0
        self.label_cache: dict[tuple, str] = {}
        self.caption_cache: dict[tuple, str] = {}
        self.resize_job = None
        self.sparkle_job = None
        self.spawn_job = None
        self.preview_photo = None
        self.preview_images: list[bytes] = []
        self.preview_index = 0
        self.current_audio: bytes | None = None
        self.settings = load_settings(PROJECT_ROOT)
        self.audio_enabled = bool(self.settings.get("mod_audio", True))
        self.player = WinMemoryAudioPlayer()

        if self.game_dir is None:
            raise GokonSoftworksError("Set the game folder before opening the mod manager.")
        self.mods_dir = mods_dir()
        self.tab = load_tab(self.game_dir, self.game_id)
        self.measured_now = capture_container_sizes(self.game, self.game_dir, self.tab)

        self.title(f"Mod Shelf, {self.game.get('display_name', '')}")
        self.geometry(f"{WINDOW_WIDTH}x{WINDOW_HEIGHT}")
        self.minsize(MIN_WIDTH, MIN_HEIGHT)
        self.resizable(False, False)
        self.configure(bg=theme.bg)
        self.protocol("WM_DELETE_WINDOW", self.close)
        self.label_font = tkfont.Font(family="Segoe UI", size=7, weight="bold")
        self.button_font = tkfont.Font(family="Segoe UI", size=10, weight="bold")

        self.native_art = load_png("bottle.png")
        self.art_cache: dict[tuple[str, bool], ImageTk.PhotoImage] = {}
        sample = self.art_for("#808080", True)
        self.bottle_width = sample.width()
        self.bottle_height = sample.height()
        self.label_width = int((LABEL_BOX[2] - LABEL_BOX[0]) * self.bottle_width) - 4
        self.label_offset = ((LABEL_BOX[1] + LABEL_BOX[3]) / 2 - 0.5) * self.bottle_height

        self.build()
        self.worker = Worker(self.shelf)
        own_window(self, master)
        self.rescan()
        self.tick_sparkles()
        self.spawn_sparkles()
    def art_for(self, colour: str, filled: bool) -> ImageTk.PhotoImage:
        key = (colour, filled)
        photo = self.art_cache.get(key)
        if photo is None:
            photo = ImageTk.PhotoImage(
                scale_to_height(render_bottle(self.native_art, colour, filled), BOTTLE_HEIGHT)
            )
            self.art_cache[key] = photo
        return photo

    def bottle_art(self, pour: Pour) -> ImageTk.PhotoImage:
        return self.art_for(pour.recipe.colour, pour.enabled)

    def build(self):
        theme = self.theme

        self.top = tk.Canvas(self, bg=theme.bg, height=TOP_HEIGHT, highlightthickness=0)
        self.top.pack(side="top", fill="x")
        self.top_buttons = {}
        specs = [
            ("rescan", "Rescan", self.rescan, "normal"),
            ("pour", "Pour", self.enable_selected, "accent"),
            ("empty", "Empty", self.disable_selected, "normal"),
            ("empty_all", "Empty Every Bottle", self.disable_all, "danger"),
            ("folder", "Open Mods Folder", self.open_mods_folder, "normal"),
        ]
        cursor = PAD
        for key, text, command, tone in specs:
            width = self.button_font.measure(text) + 30
            self.top_buttons[key] = Button(
                self.top, theme, cursor, (TOP_HEIGHT - BUTTON_HEIGHT) / 2,
                width, BUTTON_HEIGHT, text, command, tone=tone,
            )
            cursor += width + BUTTON_GAP

        body = tk.Frame(self, bg=theme.bg)
        body.pack(side="top", fill="both", expand=True)

        self.side = tk.Canvas(body, bg=theme.bg, width=SIDE_WIDTH, highlightthickness=0)
        self.side.pack(side="right", fill="y")
        self.side.pack_propagate(False)

        self.shelf = tk.Canvas(body, bg=theme.bg, highlightthickness=0)
        self.shelf.pack(side="left", fill="both", expand=True)
        self.shelf_track = self.shelf.create_rectangle(
            0, 0, 0, 0, outline="", state="hidden", tags="shelf_bar"
        )
        self.shelf_thumb = self.shelf.create_rectangle(
            0, 0, 0, 0, outline="", state="hidden", tags="shelf_bar"
        )
        self.shelf.bind("<Configure>", self.on_shelf_configure)
        self.shelf.bind("<MouseWheel>", self.on_wheel)
        self.shelf.bind("<Button-1>", self.on_shelf_press, add="+")
        self.shelf.bind("<Button-1>", self.on_shelf_click, add="+")
        self.shelf.bind("<B1-Motion>", self.on_shelf_drag, add="+")
        self.shelf.bind("<ButtonRelease-1>", self.on_shelf_release, add="+")
        self.build_side()
        self.update_buttons()

    def build_side(self):
        theme = self.theme
        self.side_panel = Panel(self.side, theme, PAD // 2, PAD, SIDE_WIDTH - PAD,
                                DETAILS_HEIGHT, title="Bottle Details")

        self.detail_title = self.side.create_text(
            PAD, PAD + 40, text="Nothing selected", anchor="nw", fill=theme.text,
            font=("Segoe UI", 12, "bold"), width=SIDE_WIDTH - PAD * 2,
        )

        self.side.create_rectangle(
            PAD, PREVIEW_Y, PAD + PREVIEW_WIDTH, PREVIEW_Y + PREVIEW_HEIGHT,
            fill=theme.field, outline=theme.panel_soft,
        )
        self.preview_item = self.side.create_image(
            PAD + PREVIEW_WIDTH / 2, PREVIEW_Y + PREVIEW_HEIGHT / 2, anchor="center"
        )
        self.preview_empty = self.side.create_text(
            PAD + PREVIEW_WIDTH / 2, PREVIEW_Y + PREVIEW_HEIGHT / 2,
            text="No bottle picked", fill=theme.text_muted, font=("Segoe UI", 9),
        )

        Button(self.side, theme, PAD, NAV_Y, 54, 26, "<", lambda: self.cycle_preview(-1))
        self.preview_count = self.side.create_text(
            PAD + 78, NAV_Y + 13, text="0/0", anchor="w", fill=theme.text_muted,
            font=("Segoe UI", 9),
        )
        Button(self.side, theme, PAD + 122, NAV_Y, 54, 26, ">", lambda: self.cycle_preview(1))
        self.audio_button = Button(
            self.side, theme, PAD + PREVIEW_WIDTH - 128, NAV_Y, 128, 26,
            self.audio_label(), self.toggle_audio,
        )

        self.clash_dot = self.side.create_oval(
            PAD, CLASH_Y + 2, PAD + 14, CLASH_Y + 16, fill="", outline="",
        )
        self.clash_font = tkfont.Font(family="Segoe UI", size=9)
        self.clash_width = SIDE_WIDTH - PAD * 2 - 22
        self.clash_note = self.side.create_text(
            PAD + 22, CLASH_Y, text="", anchor="nw", fill=theme.text_muted,
            font=self.clash_font, width=self.clash_width,
        )

        self.detail_meta = self.side.create_text(
            PAD, META_Y, text="", anchor="nw", fill=theme.text_muted,
            font=("Segoe UI", 9), width=SIDE_WIDTH - PAD * 2,
        )
        self.detail_description = StatusLog(
            self.side, theme, PAD, DESC_Y, SIDE_WIDTH - PAD * 2, DESC_MIN,
            font=("Segoe UI", 9), follow_tail=False,
        )

        self.progress = ProgressBar(self.side, theme, PAD, PAD + DETAILS_HEIGHT + 12,
                                    SIDE_WIDTH - PAD * 2)
        self.log = StatusLog(self.side, theme, PAD // 2, PAD + DETAILS_HEIGHT + 34,
                             SIDE_WIDTH - PAD, 160)
        self.side.bind("<Configure>", self.on_side_configure)
        self.side.bind("<Button-1>", self.on_side_press, add="+")
        self.side.bind("<B1-Motion>", self.on_side_drag, add="+")
        self.side.bind("<ButtonRelease-1>", self.on_side_release, add="+")

    def scroll_panels(self):
        return (self.detail_description, self.log)

    def on_side_press(self, event):
        for panel in self.scroll_panels():
            if panel.begin_drag(event.x, event.y):
                return "break"
        return None

    def on_side_drag(self, event):
        for panel in self.scroll_panels():
            if panel.dragging:
                panel.drag_to(event.y)
                return "break"
        return None

    def on_side_release(self, event):
        for panel in self.scroll_panels():
            if panel.dragging:
                panel.end_drag()
                panel.refresh_bar()
        return None

    def on_side_configure(self, event):
        spare = event.height - PAD - LOG_MIN - 34 - PAD
        panel_h = max(DETAILS_HEIGHT, spare)
        self.side_panel.place(PAD // 2, PAD, SIDE_WIDTH - PAD, panel_h)

        desc_h = max(DESC_MIN, (PAD + panel_h) - DESC_Y - 12)
        self.detail_description.place(PAD, DESC_Y, SIDE_WIDTH - PAD * 2, desc_h)

        self.progress.place(PAD, PAD + panel_h + 12, SIDE_WIDTH - PAD * 2)
        log_y = PAD + panel_h + 34
        self.log.place(PAD // 2, log_y, SIDE_WIDTH - PAD, max(48, event.height - log_y - PAD))

    def dead_bytes(self) -> int:
        total = 0
        for index, name in enumerate(self.game.get("containers", [])):
            original = self.tab.original_size(index)
            if original is None:
                continue
            path = container_file(self.game, self.game_dir, index, name)
            if path.is_file():
                total += max(0, path.stat().st_size - int(original))
        return total

    def say(self, message: str, tone: str = "muted"):
        self.log.write(message, tone)

    def clash_text(self, others: list[str]) -> str:
        if not others:
            return ""
        candidate = ""
        for shown in range(min(CLASH_NAMES_SHOWN, len(others)), -1, -1):
            named = ", ".join(others[:shown])
            extra = len(others) - shown
            if named and extra:
                body = f"{named} and {extra} more"
            elif named:
                body = named
            else:
                body = f"{extra} other bottles"
            candidate = f"Shares slots with {body}"
            if self.clash_font.measure(candidate) <= self.clash_width:
                return candidate
        return fit_line(self.clash_font, candidate, self.clash_width)

    def rescan(self):
        if getattr(self, "worker", None) and self.worker.busy:
            return
        keep = self.selected.mod_id if self.selected else None
        self.stop_audio()

        for card in self.pool:
            card.release()
        self.pours = []

        recipes, problems = scan_folder(self.mods_dir, self.game_id, self.game_index)
        clash_groups = find_clashes(recipes)
        for recipe in recipes:
            group = clash_groups.get(recipe.mod_id)
            self.pours.append(Pour(
                recipe=recipe,
                enabled=self.tab.is_enabled(recipe.mod_id),
                clash_colour=CLASH_COLOURS[group % len(CLASH_COLOURS)] if group is not None else "",
            ))
        for path, message in problems:
            self.say(f"{path.name}: {message}", "danger")

        self.draw_shelf()
        poured = sum(1 for pour in self.pours if pour.enabled)
        if self.measured_now:
            sizes = ", ".join(
                f"{name} {human_size(int(self.tab.original_size(i)))}"
                for i, name in enumerate(self.game.get("containers", []))
                if self.tab.original_size(i) is not None
            )
            self.say(f"Measured vanilla containers: {sizes}")
            self.measured_now = False
        self.say(f"{len(self.pours)} bottles on the shelf, {poured} poured.", "accent")
        dead = self.dead_bytes()
        if dead and not poured:
            self.say(
                f"{human_size(dead)} of appended data is still on the containers from "
                "earlier mods. Empty Every Bottle slices it off."
            )
        if not self.pours:
            self.say(f"Build a mod in the hub's Mod Creator or drop .gokon files into {self.mods_dir}.")

        if keep:
            self.select(next((p for p in self.pours if p.mod_id == keep), None))
        else:
            self.select(None)
        self.update_buttons()

    def cell_size(self) -> tuple[int, int]:
        return (
            self.bottle_width + BOTTLE_GAP,
            self.bottle_height + SHELF_PLANK + SHELF_CAPTION,
        )

    def columns_for(self, width: int) -> int:
        cell_w = self.cell_size()[0]
        room = max(1, width - SHELF_MARGIN * 2 - SHELF_BAR)
        return max(1, int(room // cell_w))

    def visible_rows(self) -> int:
        cell_h = self.cell_size()[1]
        return int(max(1, self.shelf.winfo_height()) // cell_h) + 2

    def row_count(self) -> int:
        if not self.pours:
            return 0
        return (len(self.pours) + max(1, self.columns) - 1) // max(1, self.columns)

    def content_height(self) -> int:
        return SHELF_MARGIN * 2 + self.row_count() * self.cell_size()[1]

    def max_scroll(self) -> int:
        return max(0, self.content_height() - max(1, self.shelf.winfo_height()))

    def label_lines(self, name: str) -> str:
        key = (name, self.label_width)
        cached = self.label_cache.get(key)
        if cached is None:
            cached = "\n".join(
                wrap_lines(self.label_font, name, self.label_width, max_lines=3)
            )
            self.label_cache[key] = cached
        return cached

    def caption_for(self, pour: Pour) -> str:
        key = (pour.problem, len(pour.recipe.entries), self.bottle_width)
        cached = self.caption_cache.get(key)
        if cached is None:
            cached = fit_line(
                self.label_font,
                pour.problem or f"{len(pour.recipe.entries)} files",
                self.bottle_width + BOTTLE_GAP - 4,
            )
            self.caption_cache[key] = cached
        return cached

    def ensure_pool(self):
        need = max(1, self.columns) * self.visible_rows()
        while len(self.pool) < need:
            self.pool.append(Bottle(self.shelf, self.theme, self))
        while len(self.pool) > need * 2 and len(self.pool) > self.columns:
            self.pool.pop().destroy()

        while len(self.planks) < self.visible_rows():
            self.planks.append((
                self.shelf.create_rectangle(0, 0, 0, 0, outline="", state="hidden", tags="shelf"),
                self.shelf.create_rectangle(0, 0, 0, 0, outline="", state="hidden", tags="shelf"),
                self.shelf.create_line(0, 0, 0, 0, state="hidden", tags="shelf"),
            ))

    def draw_shelf(self):
        theme = self.theme
        width = self.shelf.winfo_width()
        if width < 20:
            width = max(20, self.winfo_width() - SIDE_WIDTH,
                        WINDOW_WIDTH - SIDE_WIDTH)
            if self.resize_job is None:
                self.resize_job = self.after(16, self.redraw_after_resize)
        self.columns = self.columns_for(width)
        self.ensure_pool()

        cell_w, cell_h = self.cell_size()
        self.scroll_px = max(0, min(self.max_scroll(), self.scroll_px))
        first_row = max(0, (self.scroll_px - SHELF_MARGIN) // cell_h)
        rows = self.row_count()

        plank_left = SHELF_MARGIN - SHELF_OVERHANG
        plank_right = SHELF_MARGIN + self.columns * cell_w - BOTTLE_GAP + SHELF_OVERHANG
        for offset, (plank, edge, under) in enumerate(self.planks):
            row = first_row + offset
            if row >= rows:
                for item in (plank, edge, under):
                    self.shelf.itemconfigure(item, state="hidden")
                continue
            plank_y = SHELF_MARGIN + row * cell_h + self.bottle_height - self.scroll_px
            self.shelf.coords(plank, plank_left, plank_y, plank_right, plank_y + SHELF_PLANK)
            self.shelf.itemconfigure(plank, fill=theme.shelf, state="normal")
            self.shelf.coords(edge, plank_left, plank_y, plank_right, plank_y + 4)
            self.shelf.itemconfigure(edge, fill=theme.shelf_edge, state="normal")
            self.shelf.coords(under, plank_left, plank_y + SHELF_PLANK,
                              plank_right, plank_y + SHELF_PLANK)
            self.shelf.itemconfigure(under, fill=theme.panel_soft, state="normal")

        for index, card in enumerate(self.pool):
            row = first_row + index // self.columns
            column = index % self.columns
            slot = row * self.columns + column
            if slot >= len(self.pours):
                card.release()
                continue
            pour = self.pours[slot]
            card.show(
                pour,
                SHELF_MARGIN + column * cell_w,
                SHELF_MARGIN + row * cell_h - self.scroll_px,
                pour is self.selected,
            )

        self.refresh_shelf_bar()

    def shelf_track_bounds(self):
        width = max(1, self.shelf.winfo_width())
        height = max(1, self.shelf.winfo_height())
        left = width - SHELF_BAR - 4
        return left, 4, left + SHELF_BAR, height - 4

    def shelf_thumb_bounds(self):
        limit = self.max_scroll()
        if limit <= 0:
            return None
        left, top, right, bottom = self.shelf_track_bounds()
        span = bottom - top
        height = max(SHELF_THUMB_MIN, span * (max(1, self.shelf.winfo_height()) / self.content_height()))
        travel = span - height
        start = top + travel * (self.scroll_px / limit)
        return left, start, right, start + height

    def refresh_shelf_bar(self):
        thumb = self.shelf_thumb_bounds()
        if thumb is None:
            self.shelf.itemconfigure(self.shelf_track, state="hidden")
            self.shelf.itemconfigure(self.shelf_thumb, state="hidden")
            return
        left, top, right, bottom = self.shelf_track_bounds()
        self.shelf.coords(self.shelf_track, left, top, right, bottom)
        self.shelf.coords(self.shelf_thumb, *thumb)
        self.shelf.itemconfigure(self.shelf_track, fill=self.theme.panel, state="normal")
        self.shelf.itemconfigure(
            self.shelf_thumb,
            fill=self.theme.accent if self.shelf_dragging else self.theme.text_muted,
            state="normal",
        )

    def scroll_to(self, px: int):
        target = max(0, min(self.max_scroll(), int(px)))
        if target == self.scroll_px:
            return
        self.scroll_px = target
        self.draw_shelf()

    def scroll_by(self, notches: int):
        self.scroll_to(self.scroll_px + notches * (self.cell_size()[1] // 3))

    def on_wheel(self, event):
        self.scroll_by(-1 if event.delta > 0 else 1)
        return "break"

    def on_shelf_press(self, event):
        thumb = self.shelf_thumb_bounds()
        if thumb is None:
            return None
        left, top, right, bottom = thumb
        track_left, track_top, track_right, track_bottom = self.shelf_track_bounds()
        if left - 4 <= event.x <= right + 4 and top <= event.y <= bottom:
            self.shelf_dragging = True
            self.shelf_grab = event.y - top
            self.refresh_shelf_bar()
            return "break"
        if track_left - 4 <= event.x <= track_right + 4 and track_top <= event.y <= track_bottom:
            self.shelf_dragging = True
            self.shelf_grab = (bottom - top) / 2
            self.shelf_drag_to(event.y)
            return "break"
        return None

    def shelf_drag_to(self, py):
        thumb = self.shelf_thumb_bounds()
        if thumb is None:
            return
        left, track_top, right, track_bottom = self.shelf_track_bounds()
        height = thumb[3] - thumb[1]
        travel = (track_bottom - track_top) - height
        if travel <= 0:
            return
        fraction = max(0.0, min(1.0, (py - self.shelf_grab - track_top) / travel))
        self.scroll_to(round(self.max_scroll() * fraction))

    def on_shelf_drag(self, event):
        if not self.shelf_dragging:
            return None
        self.shelf_drag_to(event.y)
        return "break"

    def on_shelf_release(self, event):
        if not self.shelf_dragging:
            return None
        self.shelf_dragging = False
        self.refresh_shelf_bar()
        return "break"

    def refresh_bottle(self, pour: Pour):
        for card in self.pool:
            if card.pour is pour:
                card.restyle(pour is self.selected)
                return

    def on_shelf_click(self, event):
        if self.shelf_dragging:
            return
        item = self.shelf.find_withtag("current")
        card = self.item_lookup.get(item[0]) if item else None
        self.select(card.pour if card is not None else None)

    def on_shelf_configure(self, event):
        if (event.width, event.height) == self.shelf_size:
            return
        self.shelf_size = (event.width, event.height)
        if self.resize_job is not None:
            self.after_cancel(self.resize_job)
        self.resize_job = self.after(50, self.redraw_after_resize)

    def redraw_after_resize(self):
        self.resize_job = None
        self.draw_shelf()

    def select(self, pour: Pour | None):
        previous = self.selected
        self.selected = pour
        if previous is not None and previous in self.pours:
            self.refresh_bottle(previous)
        if pour is not None:
            self.refresh_bottle(pour)

        self.detail_description.clear()

        if pour is None:
            self.side.itemconfig(self.detail_title, text="Nothing selected")
            self.side.itemconfig(self.detail_meta, text="")
            self.preview_images = []
            self.preview_index = 0
            self.current_audio = None
            self.stop_audio()
            self.side.itemconfig(self.preview_item, image="")
            self.side.itemconfig(self.preview_empty, state="normal", text="No bottle picked")
            self.side.itemconfig(self.preview_count, text="0/0")
            self.side.itemconfig(self.clash_dot, fill="", outline="")
            self.side.itemconfig(self.clash_note, text="")
            self.update_buttons()
            return

        recipe = pour.recipe
        self.side.itemconfig(self.detail_title, text=recipe.name)

        try:
            self.preview_images = read_images(recipe)
        except Exception as exc:
            self.preview_images = []
            self.say(f"couldnt read previews: {exc}", "danger")
        self.preview_index = 0
        self.show_preview(pour)

        try:
            self.current_audio = read_audio(recipe)
        except Exception:
            self.current_audio = None
        self.refresh_audio()

        meta = [
            "Poured" if pour.enabled else "Sealed",
            f"by {recipe.author}" if recipe.author else "",
            f"v{recipe.version_text}" if recipe.version_text else "",
            f"{len(recipe.entries)} files",
            human_size(recipe.payload_bytes),
        ]
        self.side.itemconfig(self.detail_meta, text="\n".join(x for x in meta if x))
        self.detail_description.set_text(
            recipe.description or "No notes on this bottle.", "muted"
        )

        if pour.clash_colour:
            others = [
                other.recipe.name for other in self.pours
                if other is not pour and other.clash_colour == pour.clash_colour
            ]
            self.side.itemconfig(self.clash_dot, fill=pour.clash_colour, outline="")
            self.side.itemconfig(self.clash_note, text=self.clash_text(others))
        else:
            self.side.itemconfig(self.clash_dot, fill="", outline="")
            self.side.itemconfig(self.clash_note, text="")

        self.update_buttons()

    def show_preview(self, pour: Pour | None = None):
        pour = pour or self.selected
        if not self.preview_images:
            if pour is not None:
                self.preview_photo = ImageTk.PhotoImage(
                    scale_to_height(
                        render_bottle(self.native_art, pour.recipe.colour, pour.enabled),
                        PREVIEW_HEIGHT - 16,
                    )
                )
                self.side.itemconfig(self.preview_item, image=self.preview_photo)
                self.side.itemconfig(self.preview_empty, state="hidden")
            self.side.itemconfig(self.preview_count, text="0/0")
            return

        self.preview_index %= len(self.preview_images)
        self.side.itemconfig(
            self.preview_count,
            text=f"{self.preview_index + 1}/{len(self.preview_images)}",
        )
        try:
            raw_image_data = self.preview_images[self.preview_index]
            img = Image.open(io.BytesIO(raw_image_data))
            rendered = img.resize(
                (int(PREVIEW_WIDTH), int(PREVIEW_HEIGHT)), 
                Image.LANCZOS
            )
        except Exception as exc:
            self.side.itemconfig(self.preview_item, image="")
            self.side.itemconfig(self.preview_empty, state="normal",
                                 text=f"Preview error: {exc}")
            return
        self.preview_photo = ImageTk.PhotoImage(rendered)
        self.side.itemconfig(self.preview_item, image=self.preview_photo)
        self.side.itemconfig(self.preview_empty, state="hidden")

    def cycle_preview(self, delta: int):
        if not self.preview_images:
            return
        self.preview_index = (self.preview_index + delta) % len(self.preview_images)
        self.show_preview()

    def audio_label(self) -> str:
        return "Music: On" if self.audio_enabled else "Music: Off"

    def toggle_audio(self):
        self.audio_enabled = not self.audio_enabled
        self.audio_button.set_text(self.audio_label())
        self.settings["mod_audio"] = self.audio_enabled
        save_settings(PROJECT_ROOT, self.settings)
        self.refresh_audio()
        self.say("Theme tunes on." if self.audio_enabled else "Theme tunes off.")

    def refresh_audio(self):
        if not self.audio_enabled or not self.current_audio:
            self.stop_audio()
            return
        if not self.player.available:
            self.say("Audio playback is unavailable on this system.", "danger")
            return
        if not self.player.play_loop_bytes(self.current_audio):
            self.say("Bundled audio isnt a playable WAV.", "danger")

    def stop_audio(self):
        self.player.stop()

    def update_buttons(self):
        pour = self.selected
        ready = not self.busy
        self.top_buttons["pour"].set_enabled(bool(pour) and ready and not (pour and pour.enabled))
        self.top_buttons["empty"].set_enabled(bool(pour) and ready and bool(pour and pour.enabled))
        self.top_buttons["empty_all"].set_enabled(ready and bool(self.tab.enabled_ids()))
        self.top_buttons["rescan"].set_enabled(ready)
        self.top_buttons["folder"].set_enabled(True)

    def set_busy(self, busy: bool):
        self.busy = busy
        self.update_buttons()

    def sparkling(self) -> bool:
        return any(
            card.pour is not None and (card.pour.enabled or card.sparkles)
            for card in self.pool
        )

    def spawn_sparkles(self):
        for card in self.pool:
            card.spawn_sparkles()
        self.spawn_job = self.after(SPARKLE_SPAWN_MS, self.spawn_sparkles)

    def tick_sparkles(self):
        if self.sparkling() and self.winfo_viewable():
            for card in self.pool:
                card.step_sparkles()
        self.sparkle_job = self.after(SPARKLE_TICK_MS, self.tick_sparkles)
        
    def run_job(self, job, label: str, on_result=None):
        if self.busy:
            return
        self.set_busy(True)
        self.progress.set_fraction(0.0)
        self.say(label, "accent")

        def finished(result):
            self.set_busy(False)
            self.progress.set_fraction(1.0)
            if on_result is not None:
                on_result(result)
            else:
                self.say(f"{label} done.", "ok")
            self.tab = load_tab(self.game_dir, self.game_id)
            self.rescan()
            keep_front(self)

        def failed(exc):
            self.set_busy(False)
            self.progress.set_fraction(0.0)
            self.say(f"{type(exc).__name__}: {exc}", "danger")
            log.error("%s failed", label, exc_info=exc)
            messagebox.showerror("Mod Shelf", f"{type(exc).__name__}\n\n{exc}", parent=self)

        self.worker.start(
            job,
            {"progress": self.on_progress, "done": finished, "error": failed},
            name="gokon-bar",
        )

    def on_progress(self, payload):
        done, total = payload
        self.progress.set_fraction(done / total if total else 0.0)

    def enable_selected(self):
        pour = self.selected
        if pour is None:
            messagebox.showinfo("Pour", "Pick a bottle off the shelf first.", parent=self)
            keep_front(self)
            return
        if pour.enabled:
            self.say(f"{pour.recipe.name} is already poured.", "danger")
            return
        if pour.problem:
            messagebox.showerror("Pour", pour.problem, parent=self)
            return
        if not pour.recipe.entries:
            messagebox.showerror("Pour", "This package has no files in it.", parent=self)
            return

        recipe, game, game_dir, tab = pour.recipe, self.game, self.game_dir, self.tab

        def job(report):
            return apply_recipe(
                recipe, game, game_dir, tab,
                progress=lambda done, total: report("progress", (done, total)),
            )

        self.run_job(job, f"Pouring {recipe.name}")

    def disable_selected(self):
        pour = self.selected
        if pour is None:
            messagebox.showinfo("Empty", "Pick a bottle off the shelf first.", parent=self)
            keep_front(self)
            return
        if not pour.enabled:
            self.say(f"{pour.recipe.name} is already empty.", "danger")
            return

        mod_id, name = pour.mod_id, pour.recipe.name
        game, game_dir, tab = self.game, self.game_dir, self.tab

        def job(_report):
            return disable_recipe(mod_id, game, game_dir, tab)

        def report(restored):
            self.say(
                f"Emptied {name}, {restored} TOC entr{'y' if restored == 1 else 'ies'} "
                "back to vanilla. Empty Every Bottle is what trims the containers.",
                "ok",
            )

        self.run_job(job, f"Emptying {name}", on_result=report)

    def disable_all(self):
        poured = sum(1 for pour in self.pours if pour.enabled)
        if not poured and not self.tab.enabled_ids():
            self.say("Every bottle is already empty and the containers are vanilla sized.")
            return

        lines = [
            "Put every container index back to vanilla and cut every appended "
            "byte off the end?"
        ]
        if poured:
            lines.append(f"This empties {poured} poured bottles.")
        if not messagebox.askyesno("Empty Every Bottle", "\n\n".join(lines), parent=self):
            return

        game, game_dir, tab = self.game, self.game_dir, self.tab

        def job(_report):
            return disable_all(game, game_dir, tab)

        def report(result):
            restored, trimmed = result
            self.say(
                f"Emptied the shelf: {restored} TOC entr{'y' if restored == 1 else 'ies'} "
                f"back to vanilla, {trimmed} containers trimmed.",
                "ok",
            )

        self.run_job(job, "Emptying the whole shelf", on_result=report)

    def open_mods_folder(self):
        self.mods_dir.mkdir(parents=True, exist_ok=True)
        try:
            subprocess.Popen(["explorer", str(self.mods_dir)])
        except OSError as exc:
            self.say(f"couldnt open {self.mods_dir}: {exc}", "danger")

    def close(self):
        for job in (self.sparkle_job, self.spawn_job, self.resize_job):
            if job is not None:
                try:
                    self.after_cancel(job)
                except Exception:
                    pass
        self.sparkle_job = self.spawn_job = self.resize_job = None
        self.stop_audio()
        self.destroy()
