from __future__ import annotations
import queue, random, threading
import tkinter as tk
import tkinter.font as tkfont
from dataclasses import dataclass
from pathlib import Path
from .wetworks import PNG_DIR, log, read_json, write_json

try:
    from PIL import Image, ImageDraw, ImageTk

    PIL_AVAILABLE = True
except ImportError: 
    Image = ImageDraw = ImageTk = None
    PIL_AVAILABLE = False

PIL_MESSAGE = (
    "GokonSoftworks needs Pillow for the drawn interface.\n\nInstall it with:  python -m pip install pillow"
)

SETTINGS_FILENAME = "gokonsoftworks_settings.json"

@dataclass(frozen=True)
class Theme:

    key: str
    name: str
    liquid_bottom: str
    liquid_top: str
    bg: str
    panel: str
    panel_soft: str
    field: str
    text: str
    text_muted: str
    accent: str
    ok: str
    danger: str
    shelf: str
    shelf_edge: str

THEMES: tuple[Theme, ...] = (
    Theme(
        key="sakura_fizz",
        name="Sakura Fizz",
        liquid_bottom="#E26F9E",
        liquid_top="#FFF86B",
        bg="#140d15",
        panel="#221420",
        panel_soft="#33202f",
        field="#0e090f",
        text="#fff4fa",
        text_muted="#dcc0cf",
        accent="#FFF86B",
        ok="#8ff0b4",
        danger="#ff8098",
        shelf="#3a2029",
        shelf_edge="#E26F9E",
    ),
    Theme(
        key="midori_sour",
        name="Midori Sour",
        liquid_bottom="#58B082",
        liquid_top="#EB5851",
        bg="#0d1410",
        panel="#152219",
        panel_soft="#213326",
        field="#080f0b",
        text="#f2fff6",
        text_muted="#bfd8c8",
        accent="#EB5851",
        ok="#9ef0bd",
        danger="#ff9a86",
        shelf="#20301f",
        shelf_edge="#58B082",
    ),
    Theme(
        key="ramune_blue",
        name="Ramune Blue",
        liquid_bottom="#4FC3C7",
        liquid_top="#C467BF",
        bg="#0b1116",
        panel="#141d24",
        panel_soft="#1f2c35",
        field="#070c10",
        text="#f0fbff",
        text_muted="#b8ccd8",
        accent="#C467BF",
        ok="#8ff0d5",
        danger="#ff90b8",
        shelf="#1b2a30",
        shelf_edge="#4FC3C7",
    ),
    Theme(
        key="ume_sour",
        name="Ume Sour",
        liquid_bottom="#EADCB9",
        liquid_top="#A01C1C",
        bg="#15100c",
        panel="#211a14",
        panel_soft="#33281e",
        field="#0f0b08",
        text="#fff8ee",
        text_muted="#d6c4ac",
        accent="#EADCB9",
        ok="#9fe6a8",
        danger="#ff8f7a",
        shelf="#2e211a",
        shelf_edge="#A01C1C",
    ),
)

THEMES_BY_KEY = {theme.key: theme for theme in THEMES}

def settings_path(project_root: Path) -> Path:
    return Path(project_root) / SETTINGS_FILENAME

def load_settings(project_root: Path) -> dict:
    path = settings_path(project_root)
    defaults = {"last_theme": "", "output_dir": "", "volume_path": "", "mod_audio": True}
    if not path.is_file():
        return defaults
    try:
        defaults.update(read_json(path, "settings"))
    except Exception as exc:
        log.warning("Ignoring unreadable settings file: %s", exc)
    return defaults

def save_settings(project_root: Path, settings: dict):
    try:
        write_json(settings_path(project_root), settings)
    except OSError as exc:
        log.warning("Couldnt save settings: %s", exc)

def own_window(window: tk.Toplevel, master: tk.Misc):
    try:
        window.transient(master.winfo_toplevel())
    except (tk.TclError, AttributeError):
        log.debug("Couldn't mark %r transient", window)

def bring_forward(window: tk.Misc):
    try:
        window.deiconify()
        window.lift()
        window.focus_force()
    except tk.TclError:
        pass

def pick_theme(previous_key: str = "") -> Theme:
    others = [theme for theme in THEMES if theme.key != previous_key]
    if not others:
        return random.choice(THEMES)
    if previous_key and random.random() < 0.15:
        return THEMES_BY_KEY[previous_key]
    return random.choice(others)

def cut_to_fit(font: tkfont.Font, text: str, max_width: int) -> int:
    low, high = 1, min(len(text), max(1, max_width))
    while low < high:
        middle = (low + high + 1) // 2
        if font.measure(text[:middle]) <= max_width:
            low = middle
        else:
            high = middle - 1
    return low

def wrap_one(font: tkfont.Font, text: str, max_width: int, start: int = 0) -> tuple[str, int]:
    length = len(text)
    index = start
    while index < length and text[index] == " ":
        index += 1
    if index >= length:
        return "", length

    begin = index
    ceiling = begin + max(1, max_width)
    last_fit = -1
    cursor = index
    while cursor < length and cursor <= ceiling:
        word_end = cursor
        while word_end < length and text[word_end] != " " and word_end <= ceiling:
            word_end += 1
        if font.measure(text[begin:word_end]) > max_width:
            break
        last_fit = word_end
        while word_end < length and text[word_end] == " ":
            word_end += 1
        cursor = word_end

    if last_fit < 0:
        cut = cut_to_fit(font, text[begin:ceiling], max_width)
        return text[begin:begin + cut], begin + cut
    return text[begin:last_fit], last_fit

def fit_line(font: tkfont.Font, text: str, max_width: int) -> str:
    if max_width <= 0 or font.measure(text) <= max_width:
        return text
    ellipsis = "..."
    budget = max_width - font.measure(ellipsis)
    if budget <= 0:
        return ellipsis
    cut = cut_to_fit(font, text, budget)
    if font.measure(text[:cut]) > budget:
        cut = 0
    return text[:cut].rstrip() + ellipsis

def wrap_lines(font: tkfont.Font, text: str, max_width: int, max_lines: int = 0) -> list[str]:
    lines: list[str] = []
    for paragraph in text.splitlines() or [""]:
        cursor = 0
        if not paragraph:
            lines.append("")
        while cursor < len(paragraph):
            line, cursor = wrap_one(font, paragraph, max_width, cursor)
            lines.append(line)
            if max_lines and len(lines) >= max_lines:
                if cursor < len(paragraph):
                    lines[-1] = fit_line(font, line + "...", max_width)
                return lines[:max_lines]
    return lines

def require_pil():
    if not PIL_AVAILABLE:
        raise RuntimeError(PIL_MESSAGE)

def scale_to_height(image, height: int):
    require_pil()
    width = max(1, round(image.width * height / image.height))
    return image.resize((width, max(1, height)), Image.Resampling.LANCZOS)

def load_png(name: str):
    require_pil()
    path = PNG_DIR / name
    if not path.is_file():
        raise FileNotFoundError(f"Missing artwork: {path}")
    return Image.open(path).convert("RGBA")

class CanvasWidget:

    def __init__(self, canvas: tk.Canvas, theme: Theme):
        self.canvas = canvas
        self.theme = theme
        self.items: list[int] = []

    def track(self, item: int) -> int:
        self.items.append(item)
        return item

    def destroy(self):
        for item in self.items:
            self.canvas.delete(item)
        self.items.clear()

class Panel(CanvasWidget):

    def __init__(self, canvas, theme, x, y, width, height, title="", fill=None):
        super().__init__(canvas, theme)
        self.x, self.y, self.width, self.height = x, y, width, height
        self.rect = self.track(
            canvas.create_rectangle(
                x, y, x + width, y + height,
                fill=fill or theme.panel, outline=theme.panel_soft,
            )
        )
        self.title_item = None
        if title:
            self.title_item = self.track(
                canvas.create_text(
                    x + 14, y + 12, text=title, anchor="nw", fill=theme.text,
                    font=("Segoe UI", 11, "bold"), width=max(10, width - 28),
                )
            )

    def place(self, x, y, width, height):
        self.x, self.y, self.width, self.height = x, y, width, height
        self.canvas.coords(self.rect, x, y, x + width, y + height)
        if self.title_item is not None:
            self.canvas.coords(self.title_item, x + 14, y + 12)
            self.canvas.itemconfigure(self.title_item, width=max(10, width - 28))

class Button(CanvasWidget):

    def __init__(self, canvas, theme, x, y, width, height, text, command, tone="normal"):
        super().__init__(canvas, theme)
        self.x, self.y, self.width, self.height = x, y, width, height
        self.command = command
        self.tone = tone
        self.enabled = True
        self.hovering = False
        self.font = tkfont.Font(family="Segoe UI", size=10, weight="bold")
        self.label = text

        self.rect = self.track(
            canvas.create_rectangle(
                x, y, x + width, y + height,
                fill=theme.panel_soft, outline=self.edge(), width=1,
            )
        )
        self.text_item = self.track(
            canvas.create_text(
                x + width / 2, y + height / 2, text=fit_line(self.font, text, width - 18),
                fill=theme.text, font=self.font, width=max(10, width - 18), justify="center",
            )
        )
        for item in (self.rect, self.text_item):
            canvas.tag_bind(item, "<Enter>", self.enter)
            canvas.tag_bind(item, "<Leave>", self.leave)
            canvas.tag_bind(item, "<ButtonRelease-1>", self.click)

    def edge(self) -> str:
        if self.tone == "danger":
            return self.theme.danger
        if self.tone == "accent":
            return self.theme.accent
        return self.theme.panel_soft

    def hover_fill(self) -> str:
        if self.tone == "danger":
            return self.theme.danger
        if self.tone == "accent":
            return self.theme.accent
        return self.theme.text_muted

    def repaint(self):
        if not self.enabled:
            self.canvas.itemconfigure(self.rect, fill=self.theme.panel, outline=self.theme.panel_soft)
            self.canvas.itemconfigure(self.text_item, fill=self.theme.text_muted)
            return
        fill = self.hover_fill() if self.hovering else self.theme.panel_soft
        text = self.theme.bg if self.hovering else self.theme.text
        self.canvas.itemconfigure(self.rect, fill=fill, outline=self.edge())
        self.canvas.itemconfigure(self.text_item, fill=text)

    def enter(self, event=None):
        if not self.enabled:
            return
        self.hovering = True
        self.repaint()
        self.canvas.configure(cursor="hand2")

    def leave(self, event=None):
        self.hovering = False
        self.repaint()
        self.canvas.configure(cursor="")

    def click(self, event=None):
        if self.enabled and callable(self.command):
            self.command()

    def set_enabled(self, enabled: bool):
        if enabled == self.enabled:
            return
        self.enabled = enabled
        self.repaint()

    def set_text(self, text: str):
        if text == self.label:
            return
        self.label = text
        self.canvas.itemconfigure(self.text_item, text=fit_line(self.font, text, self.width - 18))

    def place(self, x, y, width, height):
        self.x, self.y, self.width, self.height = x, y, width, height
        self.canvas.coords(self.rect, x, y, x + width, y + height)
        self.canvas.coords(self.text_item, x + width / 2, y + height / 2)
        self.canvas.itemconfigure(
            self.text_item,
            text=fit_line(self.font, self.label, width - 18),
            width=max(10, width - 18),
        )

class ProgressBar(CanvasWidget):

    def __init__(self, canvas, theme, x, y, width, height=10):
        super().__init__(canvas, theme)
        self.x, self.y, self.width, self.height = x, y, width, height
        self.fraction = 0.0
        self.track_item = self.track(
            canvas.create_rectangle(x, y, x + width, y + height, fill=theme.field, outline="")
        )
        self.fill_item = self.track(
            canvas.create_rectangle(x, y, x, y + height, fill=theme.liquid_bottom, outline="")
        )

    def set_fraction(self, fraction: float):
        fraction = min(1.0, max(0.0, fraction))
        if abs(fraction - self.fraction) < 0.001:
            return
        self.fraction = fraction
        self.canvas.coords(
            self.fill_item, self.x, self.y, self.x + self.width * fraction, self.y + self.height
        )
        self.canvas.itemconfigure(
            self.fill_item,
            fill=self.theme.liquid_top if fraction >= 1.0 else self.theme.liquid_bottom,
        )

    def place(self, x, y, width, height=None):
        self.x, self.y, self.width = x, y, width
        self.height = height or self.height
        self.canvas.coords(self.track_item, x, y, x + width, y + self.height)
        self.canvas.coords(self.fill_item, x, y, x + width * self.fraction, y + self.height)

class StatusLog(CanvasWidget):

    MAX_LINES = 400
    BAR_WIDTH = 7
    THUMB_MIN = 20
    CHUNK = 60
    BLOCK_CHARS = 2400
    BLOCK_CACHE = 64

    def __init__(self, canvas, theme, x, y, width, height,
                 font=("Consolas", 9), follow_tail=True):
        super().__init__(canvas, theme)
        self.x, self.y, self.width, self.height = x, y, width, height
        self.font = tkfont.Font(family=font[0], size=font[1])
        self.line_height = self.font.metrics("linespace") + 2
        self.follow_tail = follow_tail
        self.messages: list[tuple[str, str]] = []
        self.lines: list[tuple[str, str]] = []
        self.block_colour = theme.text
        self.offset = 0

        self.blocks: list[str] = []
        self.wrapped: dict[int, list[str]] = {}
        self.wrap_order: list[int] = []
        self.top_block = 0
        self.top_line = 0
        self.avg_lines = 1.0

        self.dragging = False
        self.drag_grab = 0.0

        self.background = self.track(
            canvas.create_rectangle(x, y, x + width, y + height, fill=theme.field, outline=theme.panel_soft)
        )
        self.line_items: list[int] = []
        self.rebuild_pool()
        self.track_item = self.track(
            canvas.create_rectangle(0, 0, 0, 0, fill=theme.panel, outline="", state="hidden")
        )
        self.bar_item = self.track(
            canvas.create_rectangle(0, 0, 0, 0, fill=theme.panel_soft, outline="", state="hidden")
        )
        canvas.bind("<MouseWheel>", self.wheel, add="+")

    def visible_count(self) -> int:
        return max(1, int((self.height - 16) // self.line_height))

    def rebuild_pool(self):
        for item in self.line_items:
            self.canvas.delete(item)
        self.line_items = []
        for index in range(self.visible_count()):
            item = self.canvas.create_text(
                self.x + 10,
                self.y + 8 + index * self.line_height,
                text="", anchor="nw", fill=self.theme.text_muted, font=self.font,
            )
            self.line_items.append(item)

    def tone_colour(self, tone: str) -> str:
        return {
            "muted": self.theme.text_muted,
            "text": self.theme.text,
            "ok": self.theme.ok,
            "danger": self.theme.danger,
            "accent": self.theme.accent,
        }.get(tone, self.theme.text_muted)

    def block_mode(self) -> bool:
        return bool(self.blocks)

    def split_blocks(self, text: str) -> list[str]:
        if not text:
            return []
        out: list[str] = []
        for paragraph in (text.splitlines() or [""]):
            if len(paragraph) <= self.BLOCK_CHARS:
                out.append(paragraph)
                continue
            at = 0
            while at < len(paragraph):
                end = min(len(paragraph), at + self.BLOCK_CHARS)
                if end < len(paragraph):
                    space = paragraph.rfind(" ", at + self.BLOCK_CHARS // 2, end)
                    if space > at:
                        end = space + 1
                out.append(paragraph[at:end])
                at = end
        return out

    def block_wrapped(self, index: int) -> list[str]:
        cached = self.wrapped.get(index)
        if cached is not None:
            return cached
        source = self.blocks[index]
        lines = wrap_lines(self.font, source, self.width - 20) if source else [""]
        self.wrapped[index] = lines or [""]
        self.wrap_order.append(index)
        while len(self.wrap_order) > self.BLOCK_CACHE:
            self.wrapped.pop(self.wrap_order.pop(0), None)
        self.avg_lines = sum(len(v) for v in self.wrapped.values()) / max(1, len(self.wrapped))
        return self.wrapped[index]

    def end_top(self) -> tuple[int, int]:
        need = len(self.line_items)
        seen = 0
        index = len(self.blocks) - 1
        while index >= 0:
            lines = self.block_wrapped(index)
            if seen + len(lines) >= need:
                return index, len(lines) - (need - seen)
            seen += len(lines)
            index -= 1
        return 0, 0

    def clamp_top(self):
        last = self.end_top()
        if (self.top_block, self.top_line) > last:
            self.top_block, self.top_line = last
        if self.top_block < 0:
            self.top_block, self.top_line = 0, 0

    def step_lines(self, delta: int):
        block, line = self.top_block, self.top_line
        while delta > 0:
            lines = self.block_wrapped(block)
            if line + 1 < len(lines):
                line += 1
            elif block + 1 < len(self.blocks):
                block, line = block + 1, 0
            else:
                break
            delta -= 1
        while delta < 0:
            if line > 0:
                line -= 1
            elif block > 0:
                block -= 1
                line = len(self.block_wrapped(block)) - 1
            else:
                break
            delta += 1
        self.top_block, self.top_line = block, line
        self.clamp_top()

    def set_text(self, text: str, tone: str = "text"):
        colour = self.tone_colour(tone)
        self.messages = [(text, colour)] if text else []
        self.block_colour = colour
        self.lines = []
        self.offset = 0
        self.blocks = self.split_blocks(text or "")
        self.wrapped = {}
        self.wrap_order = []
        self.top_block = 0
        self.top_line = 0
        self.avg_lines = 1.0
        self.refresh()

    def clear(self):
        self.set_text("")

    def write(self, text: str, tone: str = "muted"):
        colour = self.tone_colour(tone)
        self.messages.append((text, colour))
        self.blocks = []
        self.wrapped = {}
        self.wrap_order = []
        if len(self.messages) > self.MAX_LINES:
            del self.messages[: len(self.messages) - self.MAX_LINES]
        for line in wrap_lines(self.font, text, self.width - 20):
            self.lines.append((line, colour))
        if len(self.lines) > self.MAX_LINES:
            del self.lines[: len(self.lines) - self.MAX_LINES]
        self.offset = max(0, len(self.lines) - self.visible_count())
        self.refresh()

    def max_offset(self) -> int:
        return max(0, len(self.lines) - len(self.line_items))

    def rewrap(self):
        if self.block_mode():
            self.wrapped = {}
            self.wrap_order = []
            self.clamp_top()
            return
        self.lines = [
            (line, colour)
            for text, colour in self.messages
            for line in wrap_lines(self.font, text, self.width - 20)
        ]
        if len(self.lines) > self.MAX_LINES:
            del self.lines[: len(self.lines) - self.MAX_LINES]

    def window(self) -> list[tuple[str, str]]:
        if not self.block_mode():
            return self.lines[self.offset : self.offset + len(self.line_items)]
        room = len(self.line_items)
        out: list[tuple[str, str]] = []
        block, line = self.top_block, self.top_line
        while block < len(self.blocks) and len(out) < room:
            lines = self.block_wrapped(block)
            while line < len(lines) and len(out) < room:
                out.append((lines[line], self.block_colour))
                line += 1
            block, line = block + 1, 0
        return out

    def refresh(self):
        if not self.block_mode():
            self.offset = min(self.offset, self.max_offset())
        window = self.window()
        for index, item in enumerate(self.line_items):
            if index < len(window):
                text, colour = window[index]
                self.canvas.itemconfigure(item, text=text, fill=colour)
            else:
                self.canvas.itemconfigure(item, text="")
        self.refresh_bar()

    def track_bounds(self) -> tuple[float, float, float, float]:
        left = self.x + self.width - self.BAR_WIDTH - 3
        return left, self.y + 4, left + self.BAR_WIDTH, self.y + self.height - 4

    def overflowing(self) -> bool:
        if self.block_mode():
            return self.end_top() > (0, 0)
        return self.max_offset() > 0

    def scroll_fraction(self) -> float:
        if not self.block_mode():
            return self.offset / self.max_offset() if self.max_offset() else 0.0
        last_block, last_line = self.end_top()
        if last_block == 0 and last_line == 0:
            return 0.0
        here = self.top_block + self.top_line / max(1, len(self.block_wrapped(self.top_block)))
        there = last_block + last_line / max(1, len(self.block_wrapped(last_block)))
        return max(0.0, min(1.0, here / there)) if there else 0.0

    def thumb_bounds(self):
        if not self.overflowing():
            return None
        left, top, right, bottom = self.track_bounds()
        span = bottom - top
        if self.block_mode():
            total = max(1.0, self.avg_lines * len(self.blocks))
        else:
            total = max(1, len(self.lines))
        portion = min(1.0, len(self.line_items) / total)
        height = max(self.THUMB_MIN, span * portion)
        travel = span - height
        start = top + travel * self.scroll_fraction()
        return left, start, right, start + height

    def hit_track(self, px, py) -> bool:
        if not self.overflowing():
            return False
        left, top, right, bottom = self.track_bounds()
        return left - 3 <= px <= right + 3 and top <= py <= bottom

    def begin_drag(self, px, py) -> bool:
        thumb = self.thumb_bounds()
        if thumb is None:
            return False
        left, top, right, bottom = thumb
        if left - 3 <= px <= right + 3 and top <= py <= bottom:
            self.dragging = True
            self.drag_grab = py - top
            return True
        if self.hit_track(px, py):
            self.dragging = True
            self.drag_grab = (bottom - top) / 2
            self.drag_to(py)
            return True
        return False

    def drag_to(self, py):
        thumb = self.thumb_bounds()
        if thumb is None:
            return
        leftt, track_top, rightt, track_bottom = self.track_bounds()
        height = thumb[3] - thumb[1]
        travel = (track_bottom - track_top) - height
        if travel <= 0:
            return
        fraction = max(0.0, min(1.0, (py - self.drag_grab - track_top) / travel))
        self.scroll_to_fraction(fraction)

    def scroll_to_fraction(self, fraction: float):
        if self.block_mode():
            last_block, last_line = self.end_top()
            target = fraction * (last_block + (1 if last_line else 0))
            self.top_block = max(0, min(len(self.blocks) - 1, int(round(target))))
            self.top_line = 0
            self.clamp_top()
        else:
            self.offset = int(round(self.max_offset() * fraction))
        self.refresh()

    def end_drag(self):
        self.dragging = False

    def refresh_bar(self):
        thumb = self.thumb_bounds()
        if thumb is None:
            self.canvas.itemconfigure(self.track_item, state="hidden")
            self.canvas.itemconfigure(self.bar_item, state="hidden")
            return
        left, top, right, bottom = self.track_bounds()
        self.canvas.coords(self.track_item, left, top, right, bottom)
        self.canvas.coords(self.bar_item, thumb[0], thumb[1], thumb[2], thumb[3])
        self.canvas.itemconfigure(self.track_item, fill=self.theme.panel, state="normal")
        self.canvas.itemconfigure(
            self.bar_item,
            fill=self.theme.accent if self.dragging else self.theme.text_muted,
            state="normal",
        )

    def wheel(self, event):
        inside = (self.x <= event.x <= self.x + self.width
                  and self.y <= event.y <= self.y + self.height)
        if not inside:
            return None
        step = 3 if event.delta > 0 else -3
        if self.block_mode():
            if not self.overflowing():
                return "break"
            self.step_lines(-step)
        else:
            if self.max_offset() == 0:
                return "break"
            self.offset = max(0, min(self.max_offset(), self.offset - step))
        self.refresh()
        return "break"

    def place(self, x, y, width, height):
        rewrap = width != self.width
        rebuild = height != self.height
        self.x, self.y, self.width, self.height = x, y, width, height
        self.canvas.coords(self.background, x, y, x + width, y + height)
        if rebuild:
            self.rebuild_pool()
        else:
            for index, item in enumerate(self.line_items):
                self.canvas.coords(item, x + 10, y + 8 + index * self.line_height)
        if rewrap:
            self.rewrap()
        if self.block_mode():
            self.clamp_top()
        else:
            self.offset = self.max_offset() if self.follow_tail else min(self.offset, self.max_offset())
        self.refresh()

class GlassGauge(CanvasWidget):
    REFERENCE = (400.0, 500.0)
    INTERIOR = (
        389, 26, 345, 85, 303, 129, 230, 188, 220, 194,
        181, 196, 142, 171, 109, 143, 88, 123, 66, 100,
        43, 75, 28, 53, 12, 29, 388, 26,
    )
    TOP_SHARE = 0.35
    ARC_DEPTH = 4

    def __init__(self, canvas, theme, x, y, height=96):
        super().__init__(canvas, theme)
        require_pil()
        self.theme = theme
        self.glass = scale_to_height(load_png("glass.png"), height)
        self.width, self.height = self.glass.size

        scale_x = self.width / self.REFERENCE[0]
        scale_y = self.height / self.REFERENCE[1]
        self.polygon = [
            round(value * (scale_x if index % 2 == 0 else scale_y))
            for index, value in enumerate(self.INTERIOR)
        ]
        ys = self.polygon[1::2]
        self.top_y = min(ys)
        self.bottom_y = max(ys)
        self.arc = max(1, round(self.ARC_DEPTH * scale_y))
        self.fill_limit = self.top_y + max(2, round(8 * scale_y))

        self.mask = Image.new("L", (self.width, self.height), 0)
        ImageDraw.Draw(self.mask).polygon(self.polygon, fill=255)

        self.frames = self.bake_frames()
        self.glass_photo = ImageTk.PhotoImage(self.glass)

        self.x, self.y = x, y
        self.level = 0
        self.liquid_item = self.track(
            canvas.create_image(x, y, image=self.frames[0], anchor="nw")
        )
        self.glass_item = self.track(canvas.create_image(x, y, image=self.glass_photo, anchor="nw"))
        self.caption = self.track(
            canvas.create_text(
                x + self.width / 2, y + self.height + 6, text="", anchor="n",
                fill=theme.text_muted, font=("Segoe UI", 8), width=self.width + 40,
                justify="center",
            )
        )

    @property
    def steps(self) -> int:
        return max(1, self.bottom_y - self.fill_limit)

    def bake_frames(self) -> list:
        mid_y = self.bottom_y - int((self.bottom_y - self.top_y) * self.TOP_SHARE)
        frames = []
        for step in range(self.steps + 1):
            surface_y = self.bottom_y - step
            layer = Image.new("RGBA", (self.width, self.height), (0, 0, 0, 0))
            draw = ImageDraw.Draw(layer)

            bottom_top = max(surface_y, mid_y)
            if bottom_top < self.bottom_y:
                draw.rectangle(
                    [0, bottom_top, self.width, self.bottom_y], fill=self.theme.liquid_bottom
                )
                if bottom_top == mid_y:
                    draw.chord(
                        [0, bottom_top - self.arc, self.width, bottom_top + self.arc],
                        start=0, end=180, fill=self.theme.liquid_bottom,
                    )
            if surface_y < mid_y:
                draw.rectangle([0, surface_y, self.width, mid_y], fill=self.theme.liquid_top)
                draw.chord(
                    [0, surface_y - self.arc, self.width, surface_y + self.arc],
                    start=0, end=180, fill=self.theme.liquid_top,
                )

            masked = Image.new("RGBA", (self.width, self.height), (0, 0, 0, 0))
            masked.paste(layer, (0, 0), self.mask)
            frames.append(ImageTk.PhotoImage(masked))
        return frames

    def set_fraction(self, fraction: float):
        level = round(min(1.0, max(0.0, fraction)) * self.steps)
        if level == self.level:
            return
        self.level = level
        self.canvas.itemconfigure(self.liquid_item, image=self.frames[level])

    def set_caption(self, text: str, tone: str = "muted"):
        colour = self.theme.ok if tone == "ok" else self.theme.text_muted
        self.canvas.itemconfigure(self.caption, text=text, fill=colour)

    def place(self, x, y):
        self.x, self.y = x, y
        self.canvas.coords(self.liquid_item, x, y)
        self.canvas.coords(self.glass_item, x, y)
        self.canvas.coords(self.caption, x + self.width / 2, y + self.height + 6)

class Worker:

    POLL_MS = 60

    def __init__(self, widget: tk.Misc):
        self.widget = widget
        self.queue: queue.Queue = queue.Queue()
        self.thread: threading.Thread | None = None
        self.handlers: dict = {}
        self.polling = False

    @property
    def busy(self) -> bool:
        return self.thread is not None and self.thread.is_alive()

    def start(self, job, handlers: dict, name: str = "gokonsoftworks-job") -> bool:
        if self.busy:
            return False
        self.handlers = handlers

        def report(kind, payload):
            self.queue.put((kind, payload))

        def run():
            try:
                result = job(report)
            except Exception as exc:
                log.exception("Background job %s failed", name)
                self.queue.put(("error", exc))
            else:
                self.queue.put(("done", result))

        self.thread = threading.Thread(target=run, name=name, daemon=True)
        self.thread.start()
        if not self.polling:
            self.polling = True
            self.widget.after(self.POLL_MS, self.drain)
        return True

    def drain(self):
        try:
            while True:
                kind, payload = self.queue.get_nowait()
                handler = self.handlers.get(kind)
                if handler:
                    handler(payload)
        except queue.Empty:
            pass
        except tk.TclError:
            self.polling = False
            return
        if self.busy or not self.queue.empty():
            self.widget.after(self.POLL_MS, self.drain)
        else:
            self.polling = False

SPARKLE_SIZE = 34
SPARKLE_FRAME_COUNT = 14
sparkle_frame_cache: dict = {}
sparkle_photo_cache: dict = {}

def hex_to_rgb(colour: str) -> tuple[int, int, int]:
    value = colour.lstrip("#")
    if len(value) == 3:
        value = "".join(ch * 2 for ch in value)
    try:
        return tuple(int(value[i:i + 2], 16) for i in (0, 2, 4))
    except ValueError:
        return (255, 255, 255)


def draw_sparkle_star(draw, cx: float, cy: float, radius: float,
                      fill_rgba: tuple, pinch: float = 0.2):
    if radius <= 0:
        return
    outer = radius / 2
    inner = outer * pinch
    draw.polygon(
        [
            (cx, cy - outer), (cx + inner, cy - inner),
            (cx + outer, cy), (cx + inner, cy + inner),
            (cx, cy + outer), (cx - inner, cy + inner),
            (cx - outer, cy), (cx - inner, cy - inner),
        ],
        fill=fill_rgba,
    )

def render_sparkle_frames(rgb: tuple, size: int = SPARKLE_SIZE,
                          frame_count: int = SPARKLE_FRAME_COUNT) -> list:
    import math

    frames = []
    cx = cy = size / 2
    for step in range(frame_count):
        progress = step / float(frame_count - 1) if frame_count > 1 else 1.0
        scale = math.sin(progress * math.pi)
        img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
        if scale > 0.05:
            draw = ImageDraw.Draw(img)
            draw_sparkle_star(draw, cx, cy, size * scale, (*rgb, 40), pinch=0.2)
            draw_sparkle_star(draw, cx, cy, size * scale * 0.6, (*rgb, 180), pinch=0.15)
            draw_sparkle_star(draw, cx, cy, size * scale * 0.25, (255, 255, 255, 255), pinch=0.1)
        frames.append(img)
    return frames

def sparkle_frames_for(colour: str, size: int = SPARKLE_SIZE,
                       frame_count: int = SPARKLE_FRAME_COUNT) -> list:
    key = (colour, size, frame_count)
    frames = sparkle_frame_cache.get(key)
    if frames is None:
        frames = render_sparkle_frames(hex_to_rgb(colour), size, frame_count)
        sparkle_frame_cache[key] = frames
    return frames

def sparkle_photo(colour: str, frame_index: int):
    key = (colour, frame_index)
    photo = sparkle_photo_cache.get(key)
    if photo is None:
        frames = sparkle_frames_for(colour)
        photo = ImageTk.PhotoImage(frames[frame_index % len(frames)])
        sparkle_photo_cache[key] = photo
    return photo

class SparkleBurst:

    def __init__(self, x: float, y: float, start_frame: int = 0):
        self.x = x
        self.y = y
        self.frame_index = start_frame

    def step(self):
        self.frame_index += 1

    def finished(self, frame_count: int = SPARKLE_FRAME_COUNT) -> bool:
        return self.frame_index >= frame_count

    @property
    def visible(self) -> bool:
        return self.frame_index >= 0

def spawn_sparkle_cluster(cx: float, cy: float, spread: int = 22,
                          count_range: tuple = (3, 5),
                          delay_range: tuple = (-6, 0)) -> list:
    count = random.randint(*count_range)
    return [
        SparkleBurst(
            cx + random.randint(-spread, spread),
            cy + random.randint(-spread, spread),
            random.randint(*delay_range),
        )
        for _ in range(count)
    ]

class MenuList(CanvasWidget):

    ROW_HEIGHT = 30
    DOT_RADIUS = 5
    DOT_INSET = 10

    def __init__(self, canvas, theme, on_pick):
        super().__init__(canvas, theme)
        self.on_pick = on_pick
        self.rows: list = []
        self.pages: list = []
        self.page = 0
        self.selected_key = ""
        self.geometry = None
        self.heading = canvas.create_text(0, 0, text="", anchor="n",
                                          fill=theme.text_muted,
                                          font=("Segoe UI", 10, "bold"))
        self.dots: list = []

    def set_pages(self, pages: list):
        for row in self.rows:
            for item in row["items"]:
                self.canvas.delete(item)
        self.rows = []
        self.pages = list(pages)

        for page_index, (_heading, items) in enumerate(self.pages):
            for key, label, note in items:
                dot = self.canvas.create_text(0, 0, text="", anchor="w",
                                              fill=self.theme.accent,
                                              font=("Segoe UI", 11, "bold"))
                name = self.canvas.create_text(0, 0, text=label, anchor="w",
                                               fill=self.theme.text_muted,
                                               font=("Segoe UI", 11))
                trail = self.canvas.create_text(0, 0, text=note, anchor="e",
                                                fill=self.theme.text_muted,
                                                font=("Segoe UI", 8))
                row = {"key": key, "page": page_index, "items": [dot, name, trail],
                       "name": name, "dot": dot, "trail": trail}
                for item in row["items"]:
                    self.canvas.tag_bind(item, "<Button-1>",
                                         lambda _e, k=key: self.on_pick(k))
                    self.canvas.tag_bind(item, "<Enter>",
                                         lambda _e, r=row: self.hover(r, True))
                    self.canvas.tag_bind(item, "<Leave>",
                                         lambda _e, r=row: self.hover(r, False))
                self.rows.append(row)

        for item in self.dots:
            self.canvas.delete(item)
        self.dots = []
        for index in range(len(self.pages)):
            dot = self.canvas.create_oval(0, 0, 0, 0, outline="", fill=self.theme.text_muted)
            self.canvas.tag_bind(dot, "<Button-1>", lambda _e, i=index: self.show_page(i))
            self.canvas.tag_bind(dot, "<Enter>",
                                 lambda _e: self.canvas.config(cursor="hand2"))
            self.canvas.tag_bind(dot, "<Leave>",
                                 lambda _e: self.canvas.config(cursor=""))
            self.dots.append(dot)
        self.repaint()

    def rows_per_page(self) -> int:
        return max((len(items) for _h, items in self.pages), default=0)

    def show_page(self, index: int):
        if not self.pages:
            return
        self.page = index % len(self.pages)
        self.repaint()
        if self.geometry:
            self.place(*self.geometry)

    def hover(self, row, entered: bool):
        if row["page"] != self.page:
            return
        self.canvas.config(cursor="hand2" if entered else "")
        if row["key"] != self.selected_key:
            self.canvas.itemconfig(
                row["name"], fill=self.theme.text if entered else self.theme.text_muted
            )

    def select(self, key: str):
        self.selected_key = key
        for row in self.rows:
            if row["key"] == key:
                self.page = row["page"]
                break
        self.repaint()
        if self.geometry:
            self.place(*self.geometry)

    def repaint(self):
        for row in self.rows:
            chosen = row["key"] == self.selected_key
            self.canvas.itemconfig(row["dot"], text="◆" if chosen else "")
            self.canvas.itemconfig(
                row["name"],
                fill=self.theme.text if chosen else self.theme.text_muted,
                font=("Segoe UI", 11, "bold") if chosen else ("Segoe UI", 11),
            )
            self.canvas.itemconfig(
                row["trail"], fill=self.theme.accent if chosen else self.theme.text_muted
            )
        if self.pages:
            self.canvas.itemconfig(self.heading, text=self.pages[self.page][0])
        for index, dot in enumerate(self.dots):
            self.canvas.itemconfig(
                dot, fill=self.theme.accent if index == self.page else self.theme.panel_soft
            )

    def place(self, x, y, width, columns: int = 2):
        self.geometry = (x, y, width, columns)
        self.canvas.coords(self.heading, x + width / 2, y - 44)

        shown = [row for row in self.rows if row["page"] == self.page]
        per_column = (len(shown) + columns - 1) // max(1, columns)
        column_width = width / max(1, columns)
        for row in self.rows:
            if row["page"] != self.page:
                for item in row["items"]:
                    self.canvas.coords(item, -4000, -4000)
        for index, row in enumerate(shown):
            column = index // max(1, per_column)
            line = index % max(1, per_column)
            left = x + column * column_width
            top = y + line * self.ROW_HEIGHT
            self.canvas.coords(row["dot"], left, top)
            self.canvas.coords(row["name"], left + 18, top)
            self.canvas.coords(row["trail"], left + column_width - 24, top)

        rows_down = (self.rows_per_page() + columns - 1) // max(1, columns)
        bottom = y + rows_down * self.ROW_HEIGHT + 14
        count = max(1, len(self.dots) - 1)
        left = x + self.DOT_INSET
        right = x + width - self.DOT_INSET
        for index, dot in enumerate(self.dots):
            cx = left if len(self.dots) < 2 else left + index * (right - left) / count
            self.canvas.coords(dot, cx - self.DOT_RADIUS, bottom - self.DOT_RADIUS,
                               cx + self.DOT_RADIUS, bottom + self.DOT_RADIUS)

def letterbox(data: bytes, width: int, height: int, background: str):
    import io

    image = Image.open(io.BytesIO(data)).convert("RGB")
    scale = min(width / image.width, height / image.height)
    size = (max(1, round(image.width * scale)), max(1, round(image.height * scale)))
    canvas = Image.new("RGB", (width, height), background)
    canvas.paste(image.resize(size, Image.Resampling.LANCZOS),
                 ((width - size[0]) // 2, (height - size[1]) // 2))
    return canvas

def keep_front(window: tk.Misc):
    try:
        window.lift()
        window.focus_force()
    except tk.TclError:
        pass
