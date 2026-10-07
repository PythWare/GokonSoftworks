from __future__ import annotations
import struct
from pathlib import Path
from .ledger import Tab
from .recipe import Recipe, read_payload
from .wetworks import GokonSoftworksError, log, region_pair, wants_region_pair

ALIGNMENT = 16
HEADER_SLOT_OFF = 0
HEADER_WIDTH = 16
LINKDATA_V2_MAGICS = (0x00077DF9, 0x00011E54)

class WriteError(GokonSoftworksError):
    pass

def part_for(game: dict, idx_marker: int) -> dict | None:
    parts = game.get("parts") or []
    if 0 <= idx_marker < len(parts):
        return parts[idx_marker]
    return None

def has_candidates(part: dict | None) -> bool:
    return part is not None and bool(part.get("candidates"))

def read_linkdata_header(path: Path) -> tuple[int, int] | None:
    try:
        with path.open("rb") as handle:
            raw = handle.read(HEADER_WIDTH)
    except OSError:
        return None
    if len(raw) < HEADER_WIDTH:
        return None
    magic, count = struct.unpack_from("<II", raw)
    return magic, count

def resolve_candidate(game_dir: Path, part: dict) -> Path:
    candidates = [game_dir / name for name in part["candidates"]]
    present = [path for path in candidates if path.is_file()]
    for path in present:
        header = read_linkdata_header(path)
        if header is not None and header[0] in LINKDATA_V2_MAGICS:
            return path
    return present[0] if present else candidates[0]

def container_file(game: dict, game_dir, idx_marker: int, name: str) -> Path:
    game_dir = Path(game_dir)
    part = part_for(game, idx_marker)
    if has_candidates(part):
        return resolve_candidate(game_dir, part)
    return game_dir / name

def check_linkdata_target(recipe: Recipe, entry, part: dict, bin_path: Path):
    if entry.container:
        built = entry.container.replace("\\", "/").rsplit("/", 1)[-1]
        if built.casefold() != bin_path.name.casefold():
            raise WriteError(
                f"{recipe.name} was built against {built} but this install has "
                f"{bin_path.name} for that slot.\n\nThese are different regional versions "
                "of the game. Their tables may not line up, so pouring it would corrupt "
                "the container. Use a mod built for your region."
            )
    header = read_linkdata_header(bin_path)
    if header is None or header[0] not in LINKDATA_V2_MAGICS:
        raise WriteError(f"{bin_path.name} isnt a LINKDATA container.")
    table_end = int(part.get("toc_offset", HEADER_WIDTH)) + header[1] * int(part["entry_size"])
    if entry.entry_off >= table_end:
        raise WriteError(
            f"{recipe.name} points past the end of the {bin_path.name} table. "
            "It was built for a different version of the game."
        )

def resolve_pair(game_dir: Path, bin_path: Path, idx_path: Path) -> tuple[Path, Path]:
    if bin_path.is_file() or not wants_region_pair(bin_path.name):
        return bin_path, idx_path
    pair = region_pair(game_dir)
    if pair is None:
        return bin_path, idx_path
    return game_dir / pair[0], game_dir / pair[1]

def container_for(game: dict, game_dir: Path, idx_marker: int) -> tuple[Path, Path]:
    part = part_for(game, idx_marker)
    if has_candidates(part):
        path = resolve_candidate(game_dir, part)
        return path, path
    if part is not None:
        toc = part.get("toc") or part["container"]
        return resolve_pair(game_dir, game_dir / part["container"], game_dir / toc)
    try:
        return resolve_pair(
            game_dir,
            game_dir / game["containers"][idx_marker],
            game_dir / game["idx_files"][idx_marker],
        )
    except (IndexError, KeyError):
        raise WriteError(f"{game.get('game_id', '?')} has no container {idx_marker}.") from None

def entry_size(game: dict) -> int:
    return int(game.get("entry_size", 32))

def alignment_for(game: dict, idx_marker: int) -> int:
    part = part_for(game, idx_marker)
    return int(part.get("alignment", ALIGNMENT)) if part is not None else ALIGNMENT

def check_pourable(game: dict, idx_marker: int):
    part = part_for(game, idx_marker)
    if part is not None:
        if not part.get("patchable", True):
            raise WriteError(
                f"{Path(part['container']).name} has no table to repoint, so a mod "
                "cant be poured into it."
            )
        return
    if int(game.get("family", 0)) != 0:
        raise WriteError(
            f"{game.get('display_name', game.get('game_id', '?'))} has no container "
            f"{idx_marker} this writer knows how to pour into."
        )

def pack_part_slot(part: dict, offset: int, size: int) -> bytes:
    alignment = int(part.get("alignment", ALIGNMENT))
    shift = int(part.get("offset_shift", 0))
    width = int(part.get("entry_size", 0))
    field_size = int(part.get("field_size", 0))
    names = list(part.get("fields") or [])
    shifted = set(part.get("shift_fields") or [])

    if not names or field_size <= 0 or field_size * len(names) != width:
        raise WriteError(
            f"{Path(part['container']).name} has no usable field description."
        )
    if "Offset" not in names:
        raise WriteError(
            f"{Path(part['container']).name} stores no offsets, so an entry "
            "can't be pointed anywhere."
        )
    if alignment <= 0 or offset % alignment:
        raise WriteError(
            f"A payload landed at {offset}, which isnt a {alignment} byte boundary."
        )

    values = {
        "Offset": offset,
        "Sector_Span": (size + alignment - 1) // alignment,
        "Original_Size": size,
    }
    ceiling = (1 << (field_size * 8)) - 1
    out = bytearray()
    order = "big" if part.get("big_endian") else "little"
    for name in names:
        v = values.get(name, 0)
        if name in shifted:
            v >>= shift
        if v > ceiling:
            raise WriteError(
                f"{Path(part['container']).name} cant hold {name} of {v}."
            )
        out += int(v).to_bytes(field_size, order)
    return bytes(out)

def pack_slot(game: dict, idx_marker: int, offset: int, size: int, comp_marker: int) -> bytes:
    part = part_for(game, idx_marker)
    if part is not None:
        return pack_part_slot(part, offset, size)

    field_size = int(game.get("field_size", 8))
    fields = entry_size(game) // field_size
    step = 1 if field_size == 8 else 2
    if field_size * fields != entry_size(game) or step * 3 >= fields:
        raise WriteError(
            f"{game.get('game_id', '?')} has a TOC layout this writer cant pack."
        )
    values = [0] * fields
    values[0] = offset
    values[step] = size
    values[step * 2] = size
    values[step * 3] = comp_marker
    code = {1: "B", 2: "H", 4: "I", 8: "Q"}[field_size]
    return struct.pack("<" + code * fields, *values)

def has_sector_header(part: dict | None) -> bool:
    return part is not None and int(part.get("sector_field", 0)) > 0

def refresh_sector_header(part: dict, idx_path: Path, container_size: int,
                          alignment: int) -> bytes | None:
    at = int(part["sector_field"])
    width = int(part.get("field_size", 4)) or 4
    order = "big" if part.get("big_endian") else "little"
    try:
        with idx_path.open("r+b") as handle:
            original = handle.read(HEADER_WIDTH)
            if len(original) < at + width:
                return None
            handle.seek(at)
            handle.write(int(container_size // alignment).to_bytes(width, order))
        return original
    except OSError as exc:
        raise WriteError(f"couldnt update {idx_path.name}: {exc}") from None

def capture_container_sizes(game: dict, game_dir, tab: Tab) -> bool:
    game_dir = Path(game_dir)
    if tab.measured():
        return False
    for idx_marker, name in enumerate(game.get("containers", [])):
        path = container_file(game, game_dir, idx_marker, name)
        if path.is_file():
            tab.remember_size(idx_marker, path.stat().st_size)
    if tab.container_sizes:
        tab.save()
        return True
    return False

def apply_recipe(recipe: Recipe, game: dict, game_dir, tab: Tab, progress=None) -> int:
    game_dir = Path(game_dir)
    capture_container_sizes(game, game_dir, tab)
    slots: list[dict] = []
    total = len(recipe.entries)

    for index, entry in enumerate(recipe.entries):
        check_pourable(game, entry.idx_marker)
        part = part_for(game, entry.idx_marker)
        bin_path, idx_path = container_for(game, game_dir, entry.idx_marker)
        if not bin_path.is_file() or not idx_path.is_file():
            raise WriteError(f"Missing container {bin_path.name} or index {idx_path.name}.")
        if has_candidates(part):
            check_linkdata_target(recipe, entry, part, bin_path)

        payload = read_payload(recipe, index)
        width = int(part["entry_size"]) if part is not None else entry_size(game)
        alignment = alignment_for(game, entry.idx_marker)

        floor = int(part.get("toc_offset", 0)) if part is not None else 0
        if entry.entry_off < floor or (entry.entry_off - floor) % width:
            raise WriteError(
                f"{recipe.mod_id} points at {entry.entry_off} in {idx_path.name}, "
                "which is not the start of a table entry."
            )

        try:
            with idx_path.open("rb") as handle:
                handle.seek(entry.entry_off)
                original = handle.read(width)
            if len(original) < width:
                raise WriteError(f"{idx_path.name} is too damn small for slot {entry.entry_off}.")

            with bin_path.open("r+b") as handle:
                handle.seek(0, 2)
                pad = (-handle.tell()) % alignment
                if pad:
                    handle.write(b"\x00" * pad)
                offset = handle.tell()
                handle.write(payload)
                tail = (-handle.tell()) % alignment
                if tail:
                    handle.write(b"\x00" * tail)
                grown = handle.tell()

            if has_sector_header(part):
                was = refresh_sector_header(part, idx_path, grown, alignment)
                if was is not None:
                    tab.remember_slot(entry.idx_marker, HEADER_SLOT_OFF, was.hex())

            with idx_path.open("r+b") as handle:
                handle.seek(entry.entry_off)
                handle.write(
                    pack_slot(game, entry.idx_marker, offset, len(payload), entry.comp_marker)
                )
        except OSError as exc:
            raise WriteError(f"couldnt write {bin_path.name}: {exc}") from None

        tab.remember_slot(entry.idx_marker, entry.entry_off, original.hex())
        slots.append({
            "idx_marker": entry.idx_marker,
            "entry_off": entry.entry_off,
        })
        if progress is not None:
            progress(index + 1, total)
    if progress is not None:
        progress(total, total)

    tab.record(recipe.mod_id, slots)
    tab.save()
    return len(slots)

def restore_slots(slots: list[dict], game: dict, game_dir) -> int:
    game_dir = Path(game_dir)
    restored = 0
    for slot in slots:
        bin_path, idx_path = container_for(game, game_dir, int(slot["idx_marker"]))
        if not idx_path.is_file():
            continue
        try:
            original = bytes.fromhex(slot["original"])
            with idx_path.open("r+b") as handle:
                handle.seek(int(slot["entry_off"]))
                handle.write(original)
            restored += 1
        except (OSError, ValueError) as exc:
            log.warning("couldnt restore slot %s: %s", slot.get("entry_off"), exc)
    return restored

def disable_recipe(mod_id: str, game: dict, game_dir, tab: Tab) -> int:
    slots = tab.forget(mod_id)
    if not slots:
        return 0

    restorable = []
    for slot in slots:
        original = tab.vanilla_for(slot["idx_marker"], slot["entry_off"])
        if original is None:
            continue
        restorable.append({
            "idx_marker": slot["idx_marker"],
            "entry_off": slot["entry_off"],
            "original": original,
        })

    restored = restore_slots(restorable, game, game_dir)
    tab.save()
    return restored

def disable_all(game: dict, game_dir, tab: Tab) -> tuple[int, int]:
    game_dir = Path(game_dir)
    restored = restore_slots(tab.all_vanilla_slots(), game, game_dir)

    trimmed = 0
    for key, size in list(tab.container_sizes.items()):
        try:
            bin_path, idx = container_for(game, game_dir, int(key))
        except WriteError:
            continue
        if not bin_path.is_file():
            continue
        try:
            if bin_path.stat().st_size > size:
                with bin_path.open("r+b") as handle:
                    handle.truncate(size)
                trimmed += 1
        except OSError as exc:
            log.warning("couldnt trim %s: %s", bin_path.name, exc)

    tab.clear()
    tab.vanilla_slots = {}
    tab.save()
    return restored, trimmed
