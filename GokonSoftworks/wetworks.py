from __future__ import annotations
import hashlib, json, logging, os
from datetime import datetime, timezone
from pathlib import Path
from typing import Callable

PACKAGE_ROOT = Path(__file__).resolve().parent
PROJECT_ROOT = PACKAGE_ROOT.parent
PNG_DIR = PACKAGE_ROOT / "pngs"
TAILDATA_DIR = PACKAGE_ROOT / "Taildata"
FILENAMES_DIR = PACKAGE_ROOT / "Filenames"
MODS_DIR = PROJECT_ROOT / "Mods"
LOG_PATH = PROJECT_ROOT / "gokonsoftworks.log"

def taildata_dir() -> Path:
    TAILDATA_DIR.mkdir(parents=True, exist_ok=True)
    return TAILDATA_DIR

def filenames_dir() -> Path:
    FILENAMES_DIR.mkdir(parents=True, exist_ok=True)
    return FILENAMES_DIR

def mods_dir() -> Path:
    MODS_DIR.mkdir(parents=True, exist_ok=True)
    return MODS_DIR

def unpack_root() -> Path:
    return PROJECT_ROOT

def taildata_path(game_id: str) -> Path:
    return taildata_dir() / f"gokon_taildata_{game_id}.json"

ProgressCallback = Callable[[int, int, str], None]

class GokonSoftworksError(RuntimeError):
    pass

def build_logger() -> logging.Logger:
    logger = logging.getLogger("gokonsoftworks")
    if logger.handlers:
        return logger
    logger.setLevel(logging.INFO)
    formatter = logging.Formatter("%(asctime)s %(levelname)s %(name)s: %(message)s")
    try:
        handler = logging.FileHandler(LOG_PATH, encoding="utf-8")
    except OSError:
        handler = logging.StreamHandler()
    handler.setFormatter(formatter)
    logger.addHandler(handler)
    return logger

log = build_logger()

def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="seconds")

def human_size(size: int) -> str:
    value = float(size)
    for unit in ("B", "KB", "MB", "GB", "TB"):
        if value < 1024 or unit == "TB":
            return f"{value:.0f} {unit}" if unit == "B" else f"{value:.1f} {unit}"
        value /= 1024
    return f"{value:.1f} TB"

def align_up(value: int, alignment: int = 16) -> int:
    return (value + (alignment - 1)) & ~(alignment - 1)

def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()

def write_json(path: Path, data: dict):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temp = path.with_suffix(path.suffix + ".tmp")
    temp.write_text(json.dumps(data, indent=1), encoding="utf-8")
    temp.replace(path)

def read_json(path: Path, label: str = "file") -> dict:
    try:
        return json.loads(Path(path).read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return {}

SOUND_ASYNC = 0x0001
SOUND_NODEFAULT = 0x0002
SOUND_MEMORY = 0x0004
SOUND_LOOP = 0x0008
SOUND_PURGE = 0x0040

class WinMemoryAudioPlayer:

    def __init__(self):
        import ctypes
        from ctypes import wintypes

        self.ctypes = ctypes
        self.buffer = None
        self.available = True
        try:
            self.winmm = ctypes.WinDLL("winmm", use_last_error=True)
            self.play_sound = self.winmm.PlaySoundW
            self.play_sound.argtypes = [ctypes.c_void_p, wintypes.HMODULE, wintypes.DWORD]
            self.play_sound.restype = wintypes.BOOL
        except Exception:
            self.available = False
            self.winmm = None
            self.play_sound = None

    @staticmethod
    def is_wav(data: bytes) -> bool:
        return len(data) >= 12 and data[:4] == b"RIFF" and data[8:12] == b"WAVE"

    def play_loop_bytes(self, wav_bytes: bytes) -> bool:
        if not self.available or not wav_bytes or not self.is_wav(wav_bytes):
            return False
        self.stop()
        self.buffer = self.ctypes.create_string_buffer(wav_bytes)
        pointer = self.ctypes.cast(self.buffer, self.ctypes.c_void_p)
        result = self.play_sound(
            pointer, None, SOUND_MEMORY | SOUND_ASYNC | SOUND_LOOP | SOUND_NODEFAULT
        )
        return bool(result)

    def stop(self):
        if self.available and self.play_sound:
            self.play_sound(None, None, SOUND_PURGE)
        self.buffer = None

RECOMMENDED_FREE_SPACE = 20 * 1000 * 1000 * 1000
CRITICAL_FREE_SPACE = 512 * 1000 * 1000
LONG_PATH_WARNING = 120
PROBE_BYTES = b"gokonsoftworks-probe"

PROTECTED_ROOTS = (
    ("Program Files", "Program Files"),
    ("Program Files (x86)", "Program Files (x86)"),
    ("Windows", "the Windows folder"),
    ("System32", "System32"),
)

def format_bytes(size) -> str:
    if size is None:
        return "Unknown"
    value = float(abs(int(size)))
    sign = "-" if int(size) < 0 else ""
    for unit in ("B", "KB", "MB", "GB", "TB"):
        if value < 1000.0 or unit == "TB":
            if unit == "B":
                return f"{sign}{int(value)} {unit}"
            return f"{sign}{f'{value:.2f}'.rstrip('0').rstrip('.')} {unit}"
        value /= 1000.0
    return f"{sign}{value:.2f} TB"

def protected_location_name(path: Path) -> str:
    parts = {part.lower() for part in Path(path).parts}
    for folder, label in PROTECTED_ROOTS:
        if folder.lower() in parts:
            return label
    return ""

def nearest_existing(path: Path) -> Path:
    candidate = Path(path).resolve()
    while not candidate.exists():
        parent = candidate.parent
        if parent == candidate:
            break
        candidate = parent
    return candidate

class Diagnostics:

    def __init__(self, path: Path):
        self.path = Path(path)
        self.issues: list[tuple[str, str]] = []
        self.free = None
        self.total = None
        self.can_read = False
        self.can_write = False
        self.can_create = False
        self.can_delete = False
        self.protected = protected_location_name(self.path)
        self.run()

    def note(self, severity: str, message: str):
        self.issues.append((severity, message))

    def run(self):
        import shutil as sh
        import tempfile as tf

        base = nearest_existing(self.path)
        self.drive = os.path.splitdrive(str(base))[0] or str(base)

        try:
            usage = sh.disk_usage(base)
            self.total, self.free = usage.total, usage.free
        except OSError as exc:
            self.note("error", f"couldnnt read drive storage: {exc}")

        if not self.path.exists():
            self.note("error", "The software folder doesnt exist.")
            return
        if not self.path.is_dir():
            self.note("error", "The software path isnt a folder.")
            return

        try:
            with os.scandir(self.path) as entries:
                next(entries, None)
            self.can_read = True
        except OSError as exc:
            self.note("error", f"Cant read the folder: {exc}")

        probe = ""
        try:
            handle_fd, probe = tf.mkstemp(prefix="gokon_diag_", suffix=".tmp", dir=self.path)
            with os.fdopen(handle_fd, "wb") as handle:
                handle.write(PROBE_BYTES)
            self.can_write = Path(probe).read_bytes() == PROBE_BYTES
        except OSError as exc:
            self.note("error", f"Cant write here: {exc}")
        finally:
            if probe and os.path.exists(probe):
                try:
                    os.remove(probe)
                    self.can_delete = True
                except OSError as exc:
                    self.note("warning", f"Couldnt remove the test file: {exc}")

        try:
            probe_dir = Path(tf.mkdtemp(prefix="gokon_diag_", dir=self.path))
            self.can_create = True
            probe_dir.rmdir()
        except OSError as exc:
            self.note("error", f"Cant create folders here: {exc}")

        if self.free is not None:
            if self.free < CRITICAL_FREE_SPACE:
                self.note("error", f"Only {format_bytes(self.free)} free. An unpack will not fit.")
            elif self.free < RECOMMENDED_FREE_SPACE:
                self.note(
                    "warning",
                    f"{format_bytes(self.free)} free. A full unpack can want "
                    f"{format_bytes(RECOMMENDED_FREE_SPACE)} or more.",
                )
        if self.protected:
            self.note(
                "warning",
                f"This sits inside {self.protected}, where Windows may block writes.",
            )
        if len(str(self.path)) > LONG_PATH_WARNING:
            self.note(
                "warning",
                f"The path is {len(str(self.path))} characters. Deep unpacks may hit "
                "the Windows path limit.",
            )

    @property
    def has_errors(self) -> bool:
        return any(severity == "error" for severity, _m in self.issues)

    @property
    def has_warnings(self) -> bool:
        return any(severity == "warning" for severity, _m in self.issues)

    @property
    def status(self) -> str:
        if self.has_errors:
            return "Blocked"
        return "Warning" if self.has_warnings else "Good"

    def report_text(self) -> str:
        lines = [
            f"Status: {self.status}",
            f"Path: {self.path}",
            f"Drive: {self.drive or 'Unknown'}",
            f"Free space: {format_bytes(self.free)} of {format_bytes(self.total)}",
            f"Read: {'OK' if self.can_read else 'Blocked'}",
            f"Write file: {'OK' if self.can_write else 'Blocked'}",
            f"Create folder: {'OK' if self.can_create else 'Blocked'}",
            f"Delete probe: {'OK' if self.can_delete else 'Blocked'}",
        ]
        if self.protected:
            lines.append(f"Protected location: {self.protected}")
        lines.append("")
        if self.issues:
            lines.append("Findings:")
            lines.extend(f"- {severity.upper()}: {message}" for severity, message in self.issues)
        else:
            lines.append("No problems found.")
        return "\n".join(lines)


def diagnose(path=None) -> Diagnostics:
    return Diagnostics(Path(path) if path else PROJECT_ROOT)

def region_pair(folder: Path) -> tuple[str, str] | None:
    try:
        items = sorted(folder.iterdir())
    except OSError:
        return None
    for item in items:
        name = item.name
        upper = name.upper()
        if not upper.startswith("LINKDATA_") or not upper.endswith(".BIN"):
            continue
        if not item.is_file():
            continue
        toc = name[:-4] + ".IDX"
        if (folder / toc).is_file():
            return name, toc
    return None

def wants_region_pair(name: str) -> bool:
    base = name.replace("\\", "/").rsplit("/", 1)[-1].upper()
    return base.startswith("LINKDATA_") and base.endswith(".BIN")

def candidate_containers_in(parts: list, folder: Path) -> list[str]:
    found = []
    for part in parts:
        for name in part.get("candidates") or []:
            if (folder / name).is_file():
                found.append(name)
                break
    return found

def containers_in(game: dict, folder: Path) -> list[str]:
    parts = game.get("parts") or []
    if any(part.get("candidates") for part in parts):
        return candidate_containers_in(parts, folder)
    names = list(game.get("containers") or [])
    present = [name for name in names if (folder / name).is_file()]
    if present or not names:
        return present
    if not wants_region_pair(names[0]):
        return []
    pair = region_pair(folder)
    return [pair[0]] if pair else []
