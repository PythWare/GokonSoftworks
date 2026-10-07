from __future__ import annotations
import hashlib, json, struct
from dataclasses import dataclass, field
from pathlib import Path
from .wetworks import GokonSoftworksError

PACKAGE_EXTENSION = ".gokon"
PACKAGE_MAGIC = b"GOKON\x00"
PACKAGE_FORMAT = "gokon-mix"
SUPPORTED_VERSIONS = (2,)
MAX_PREVIEW_IMAGES = 5
HEAD_STRUCT = struct.Struct("<IBI")

class RecipeError(GokonSoftworksError):
    pass

@dataclass
class PouredEntry:

    idx_marker: int
    entry_off: int
    comp_marker: int
    size: int
    digest: str = ""
    container: str = ""

@dataclass
class Recipe:
    path: Path
    version: int
    game_index: int
    header: dict
    data_start: int
    entries: list[PouredEntry] = field(default_factory=list)

    @property
    def mod_id(self) -> str:
        return self.path.name

    @property
    def name(self) -> str:
        return self.header.get("name") or self.path.stem

    @property
    def game(self) -> str:
        return self.header.get("game", "")

    @property
    def author(self) -> str:
        return self.header.get("author", "")

    @property
    def version_text(self) -> str:
        return self.header.get("mod_version", "")

    @property
    def description(self) -> str:
        return self.header.get("description", "")

    @property
    def colour(self) -> str:
        return self.header.get("color", "#7B1E3A")

    @property
    def images(self) -> list[dict]:
        return self.header.get("images") or []

    @property
    def audio(self) -> dict | None:
        return self.header.get("audio")

    @property
    def payload_bytes(self) -> int:
        return sum(entry.size for entry in self.entries)

    @property
    def images_start(self) -> int:
        return self.data_start + self.payload_bytes

    @property
    def audio_start(self) -> int:
        return self.images_start + sum(int(image["size"]) for image in self.images)

def read_recipe(path) -> Recipe:
    path = Path(path)
    try:
        with path.open("rb") as handle:
            if handle.read(len(PACKAGE_MAGIC)) != PACKAGE_MAGIC:
                raise RecipeError(f"{path.name} isnt a {PACKAGE_EXTENSION} package.")

            version, game_index, header_size = HEAD_STRUCT.unpack(
                handle.read(HEAD_STRUCT.size)
            )
            if version not in SUPPORTED_VERSIONS:
                raise RecipeError(
                    f"{path.name} is format v{version}, which this build cant pour."
                )

            header = json.loads(handle.read(header_size).decode("utf-8"))
            if header.get("format") != PACKAGE_FORMAT:
                raise RecipeError(f"{path.name} isnt a {PACKAGE_EXTENSION} package.")

            data_start = len(PACKAGE_MAGIC) + HEAD_STRUCT.size + header_size
            recipe = Recipe(path=path, version=version, game_index=game_index,
                            header=header, data_start=data_start)
            for raw in header.get("entries", []):
                recipe.entries.append(PouredEntry(
                    idx_marker=int(raw["idx_marker"]),
                    entry_off=int(raw["entry_off"]),
                    comp_marker=int(raw.get("comp_marker", 0)),
                    size=int(raw["payload_size"]),
                    digest=raw.get("payload_sha256", ""),
                    container=raw.get("container", ""),
                ))
            return recipe
    except OSError as exc:
        raise RecipeError(f"Couldnt read {path.name}: {exc}") from None
    except (ValueError, KeyError, struct.error) as exc:
        raise RecipeError(f"{path.name} is malformed: {exc}") from None

def peek_game_index(path) -> int | None:
    try:
        with Path(path).open("rb") as handle:
            if handle.read(len(PACKAGE_MAGIC)) != PACKAGE_MAGIC:
                return None
            version, game_index, _size = HEAD_STRUCT.unpack(handle.read(HEAD_STRUCT.size))
            return game_index if version in SUPPORTED_VERSIONS else None
    except (OSError, struct.error):
        return None

def read_payload(recipe: Recipe, index: int) -> bytes:
    offset = recipe.data_start + sum(entry.size for entry in recipe.entries[:index])
    entry = recipe.entries[index]
    with recipe.path.open("rb") as handle:
        handle.seek(offset)
        data = handle.read(entry.size)
    if len(data) != entry.size:
        raise RecipeError(f"{recipe.path.name} is truncated inside a payload.")
    if entry.digest and hashlib.sha256(data).hexdigest() != entry.digest:
        raise RecipeError(f"{recipe.path.name} slot {entry.entry_off} is corrupt.")
    return data

def read_images(recipe: Recipe) -> list[bytes]:
    blobs: list[bytes] = []
    if not recipe.images:
        return blobs
    with recipe.path.open("rb") as handle:
        handle.seek(recipe.images_start)
        for image in recipe.images:
            size = int(image["size"])
            data = handle.read(size)
            if len(data) != size:
                raise RecipeError(f"{recipe.path.name} is truncated inside a preview.")
            blobs.append(data)
    return blobs

def read_audio(recipe: Recipe) -> bytes | None:
    audio = recipe.audio
    if not audio:
        return None
    with recipe.path.open("rb") as handle:
        handle.seek(recipe.audio_start)
        data = handle.read(int(audio["size"]))
    if len(data) != int(audio["size"]):
        raise RecipeError(f"{recipe.path.name} is truncated inside the theme tune.")
    return data

def list_recipes(folder, game_id: str = "", game_index: int | None = None) -> list[Recipe]:
    folder = Path(folder)
    if not folder.is_dir():
        return []

    found = []
    for path in sorted(folder.glob(f"*{PACKAGE_EXTENSION}")):
        if game_index is not None and peek_game_index(path) != game_index:
            continue
        try:
            recipe = read_recipe(path)
        except RecipeError:
            continue
        if game_id and recipe.game != game_id:
            continue
        found.append(recipe)
    return found

def collect_source_files(source_folder, records: dict) -> list[tuple[str, Path]]:
    source_folder = Path(source_folder).resolve()
    matched: list[tuple[str, Path]] = []
    seen: set[str] = set()

    for file_path in sorted(source_folder.rglob("*")):
        if not file_path.is_file():
            continue
        if file_path.suffix.lower() == PACKAGE_EXTENSION:
            continue

        parts = file_path.resolve().parts
        for depth in range(min(len(parts), 12), 0, -1):
            candidate = "/".join(parts[-depth:])
            if candidate in records and candidate not in seen:
                seen.add(candidate)
                matched.append((candidate, file_path))
                break
    return matched

def scan_folder(folder, game_id: str = "", game_index: int | None = None):
    folder = Path(folder)
    if not folder.is_dir():
        return [], []

    found: list[Recipe] = []
    problems: list[tuple[Path, str]] = []
    for path in sorted(folder.glob(f"*{PACKAGE_EXTENSION}")):
        index = peek_game_index(path)
        if index is None:
            problems.append((path, "Not a readable .gokon package"))
            continue
        if game_index is not None and index != game_index:
            continue
        try:
            recipe = read_recipe(path)
        except RecipeError as exc:
            problems.append((path, str(exc)))
            continue
        if game_id and recipe.game != game_id:
            continue
        found.append(recipe)
    return found, problems
