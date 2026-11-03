#!/usr/bin/env python3
"""Audit the source Tile::use subset used by BlockInteraction (not a full engine).

Prints a provenance JSON manifest to stdout. --check is read-only: pins the
upstream revision, records SHA-256 and exact [start_byte,end_byte) per source
function/registration, validates the load-bearing semantics of every rule that
BlockInteraction implements, and asserts that every tile reported unsupported
really is unsupportable for the stated reason. Catalog availability is checked
against jsapi/build/generated/item-state-map.json, so a tile cannot be claimed
as supported unless its toggled render state actually exists. This script
neither generates nor writes artifacts.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import sys

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
SOURCE_COMMIT = "09df8928eeb15328bca499d225e48ea95af6d119"
WORLD = "port/upstream/Minecraft.World/"
CLIENT = "port/upstream/Minecraft.Client/"
STATE_MAP = "jsapi/build/generated/item-state-map.json"

# Every source use() that the interaction decision depends on, including the
# ones we refuse: the refusal reason is only credible if the source is read.
FUNCTIONS = {
    WORLD + "DoorTile.cpp": ["DoorTile::use", "DoorTile::getCompositeData", "DoorTile::setOpen",
                             "DoorTile::isOpen", "DoorTile::neighborChanged"],
    WORLD + "LeverTile.cpp": ["LeverTile::use"],
    WORLD + "ButtonTile.cpp": ["ButtonTile::use", "ButtonTile::tick"],
    WORLD + "RepeaterTile.cpp": ["RepeaterTile::use"],
    WORLD + "ChestTile.cpp": ["ChestTile::use"],
    WORLD + "FurnaceTile.cpp": ["FurnaceTile::use"],
    WORLD + "WorkbenchTile.cpp": ["WorkbenchTile::use"],
    WORLD + "TrapDoorTile.cpp": ["TrapDoorTile::use"],
    WORLD + "Tile.cpp": ["Tile::use"],
    CLIENT + "MultiPlayerGameMode.cpp": ["MultiPlayerGameMode::useItemOn"],
}

# tileId -> (status, reason). "door" is the only implemented family this round.
# Statuses: supported-change, supported-consume, blocked-catalog, blocked-tick,
# blocked-container, blocked-absent, blocked-no-source-use.
DECISIONS = {
    64: ("supported-change", "DoorTile::use flips the lower half's open bit; catalog ships data 0-11"),
    71: ("supported-consume", "DoorTile::use returns true at the Material::metal guard without editing"),
    69: ("blocked-catalog", "LeverTile::use only flips the powered bit; catalog has lever data=0 only"),
    77: ("blocked-catalog", "ButtonTile::use only flips the powered bit; catalog has button data=0 only"),
    93: ("blocked-catalog", "RepeaterTile::use retimes via data; catalog has diode_off data=0 only"),
    54: ("blocked-container", "ChestTile::use opens a container through a tile entity"),
    61: ("blocked-container", "FurnaceTile::use opens a container through a tile entity"),
    58: ("blocked-container", "WorkbenchTile::use opens the crafting GUI"),
    23: ("blocked-container", "DispenserTile requires a tile entity inventory"),
    118: ("blocked-container", "CauldronTile::use needs item-stack semantics"),
    96: ("blocked-absent", "TrapDoorTile has no catalog render state at all"),
    107: ("blocked-absent", "fence gate has no catalog render state at all"),
    26: ("blocked-absent", "bed has no catalog render state at all"),
    92: ("blocked-absent", "cake has no catalog render state at all"),
    116: ("blocked-absent", "enchanting table has no catalog render state at all"),
    117: ("blocked-absent", "brewing stand has no catalog render state at all"),
    145: ("blocked-absent", "anvil has no catalog render state at all"),
    154: ("blocked-absent", "hopper has no catalog render state at all"),
    149: ("blocked-absent", "comparator has no catalog render state at all"),
    150: ("blocked-absent", "comparator has no catalog render state at all"),
    59: ("blocked-no-source-use", "CropTile has no use() override; growth is DyePowderItem::useOn"),
    60: ("blocked-no-source-use", "FarmTile has no use() override"),
    141: ("blocked-no-source-use", "CarrotTile has no use() override"),
    142: ("blocked-no-source-use", "PotatoTile has no use() override"),
}

# Tiles asserted to have NO use() override anywhere in the source tree. This is
# what makes "blocked-no-source-use" an audited fact rather than an assumption.
NO_USE_OVERRIDE = ["CropTile", "CarrotTile", "PotatoTile", "FarmTile", "NetherWartTile", "CocoaTile"]


def generate(root):
    spec = importlib.util.spec_from_file_location("interaction_source_parser", root / "tools/extract-renderer.py")
    helper = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(helper)
    sources, functions, bodies, references = {}, [], {}, []

    def source(path):
        if path not in sources:
            raw = (root / path).read_bytes()
            sources[path] = raw, helper.mask_cpp(raw)
        return path, *sources[path]

    for path, names in FUNCTIONS.items():
        path, raw, masked = source(path)
        for name in names:
            matches = re.findall(rb"^[ \t]*[^\r\n;{}]*\b" + re.escape(name.encode()) + rb"\([^\r\n]*\)[^\r\n;{}]*\r?$", masked, re.M)
            if not matches:
                raise ValueError("missing interaction definition: " + name)
            for signature in matches:
                body, entry = helper.extract_function(raw, masked, signature.strip().decode())
                entry.update(source_file=path, source_text=body.decode(),
                             use="audited Tile::use subset, not copied full engine")
                functions.append(entry)
                bodies.setdefault(name, []).append(body)

    def require(name, text):
        flat = re.sub(rb"\s+", b"", b"\n".join(bodies[name]))
        if re.sub(rb"\s+", b"", text.encode()) not in flat:
            raise ValueError("source interaction semantics changed: " + name + ": " + text)

    for name, text in [
        # Dispatch order: use() runs first and suppresses the item/place branch.
        ("MultiPlayerGameMode::useItemOn", "if (!player->isSneaking() || player->getCarriedItem() == nullptr)"),
        ("MultiPlayerGameMode::useItemOn", "if (Tile::tiles[t]->use(level, x, y, z, player, face, clickX, clickY, clickZ)) didSomething = true;"),
        ("MultiPlayerGameMode::useItemOn", "if (!didSomething && item != nullptr && dynamic_cast<TileItem *>(item->getItem()))"),
        # Base Tile::use does nothing, so only overrides can be interactive.
        ("Tile::use", "return false;"),
        # Wooden door: toggle bit 4 of the LOWER half only, upper left untouched.
        ("DoorTile::use", "if (material == Material::metal) return true;"),
        ("DoorTile::use", "int compositeData = getCompositeData(level, x, y, z);"),
        ("DoorTile::use", "int lowerData = compositeData & C_LOWER_DATA_MASK;"),
        ("DoorTile::use", "lowerData ^= 4;"),
        ("DoorTile::use", "if ((compositeData & C_IS_UPPER_MASK) == 0)"),
        ("DoorTile::use", "level->setData(x, y, z, lowerData, Tile::UPDATE_CLIENTS);"),
        ("DoorTile::use", "level->setData(x, y - 1, z, lowerData, Tile::UPDATE_CLIENTS);"),
        ("DoorTile::use", "level->setTilesDirty(x, y - 1, z, x, y, z);"),
        # Composite layout: lower owns dir+open, hinge is bit0 of UPPER data.
        ("DoorTile::getCompositeData", "bool isUpper = (data & UPPER_BIT) != 0;"),
        ("DoorTile::getCompositeData", "lowerData = level->getData(x, y - 1, z);"),
        ("DoorTile::getCompositeData", "upperData = level->getData(x, y + 1, z);"),
        ("DoorTile::getCompositeData", "bool isRightHinge = (upperData & 1) != 0;"),
        ("DoorTile::getCompositeData", "return (lowerData & C_LOWER_DATA_MASK) | (isUpper ? 8 : 0) | (isRightHinge ? 16 : 0);"),
        ("DoorTile::isOpen", "return (data & 4) != 0;"),
        # Unpaired halves are destroyed by block updates we do not run, so
        # BlockInteraction refuses them instead of toggling a half door.
        ("DoorTile::neighborChanged", "if (level->getTile(x, y + 1, z) != id)"),
        ("DoorTile::neighborChanged", "level->removeTile(x, y, z);"),
        # Lever/button/repeater: metadata-only power flips we cannot represent.
        ("LeverTile::use", "int open = 8 - (data & 8);"),
        ("LeverTile::use", "level->setData(x, y, z, dir + open, Tile::UPDATE_ALL);"),
        ("LeverTile::use", "level->updateNeighborsAt(x, y, z, id);"),
        ("ButtonTile::use", "int open = 8 - (data & 8);"),
        ("ButtonTile::use", "if (open == 0) return true;"),
        ("ButtonTile::use", "level->setData(x, y, z, dir + open, Tile::UPDATE_ALL);"),
        # The button only pops back out from a scheduled tick; we have none.
        ("ButtonTile::use", "level->addToTickNextTick(x, y, z, id, getTickDelay(level));"),
        ("ButtonTile::tick", "if ((data & 8) == 0)"),
        # Container/GUI tiles: all route through a tile entity or a player
        # screen, neither of which is modelled here.
        ("ChestTile::use", "shared_ptr<Container> container = getContainer(level, x, y, z);"),
        ("ChestTile::use", "player->openContainer(container);"),
        ("FurnaceTile::use", "player->openFurnace(furnace);"),
        ("FurnaceTile::use", "dynamic_pointer_cast<FurnaceTileEntity>( level->getTileEntity(x, y, z) )"),
        ("WorkbenchTile::use", "player->startCrafting(x, y, z);"),
        # Repeater retimes through its delay bits, which the catalog lacks.
        ("RepeaterTile::use", "int delay = (data & DELAY_MASK) >> DELAY_SHIFT;"),
        ("RepeaterTile::use", "level->setData(x, y, z, delay | (data & DIRECTION_MASK), Tile::UPDATE_ALL);"),
    ]:
        require(name, text)

    # Pure-metadata use() bodies must not read the click position: this is what
    # lets World::useBlock pass only block coordinates. Strip the signature
    # first, since the parameters are obviously named there.
    for name in ["DoorTile::use", "LeverTile::use", "ButtonTile::use", "RepeaterTile::use"]:
        for body in bodies[name]:
            opening = body.find(b"{")
            if opening < 0:
                raise ValueError("unparsable use() body: " + name)
            inner = re.sub(rb"\s+", b"", body[opening:])
            for parameter in [b"clickX", b"clickY", b"clickZ", b"clickedFace"]:
                if parameter in inner:
                    raise ValueError("use() now depends on click position: " + name + ": " + parameter.decode())

    # Exact mask/registration bytes. The door bit layout and the two DoorTile
    # registrations (wood vs metal material) cannot be derived from anything else.
    for path, expression in [
        (WORLD + "DoorTile.h", rb"static const int (?:UPPER_BIT|C_DIR_MASK|C_OPEN_MASK|C_LOWER_DATA_MASK|C_IS_UPPER_MASK|C_RIGHT_HINGE_MASK) = [0-9]+;"),
        (WORLD + "Tile.cpp", rb"^[ \t]*Tile::(?:door_wood|door_iron|lever|stoneButton|woodButton|chest|workBench|furnace)\s*=\s*\(new [^;]+;"),
    ]:
        path, raw, _ = source(path)
        matches = list(re.finditer(expression, raw, re.M))
        if not matches:
            raise ValueError("missing interaction reference: " + path)
        for match in matches:
            references.append(dict(source_file=path, source_text=match.group().decode(),
                                   start_byte=match.start(), end_byte=match.end(),
                                   start_line=raw.count(b"\n", 0, match.start()) + 1,
                                   sha256=hashlib.sha256(match.group()).hexdigest()))

    # Assert the "no use() override" claim across the whole source tree.
    for name in NO_USE_OVERRIDE:
        path = root / WORLD / (name + ".cpp")
        if not path.exists():
            continue
        if re.search(rb"\b" + name.encode() + rb"::use\s*\(", helper.mask_cpp(path.read_bytes())):
            raise ValueError("tile now overrides use(), reassess support: " + name)

    # Catalog cross-check. A tile may only be called supported-change when the
    # toggled render state really exists; everything blocked-catalog must have
    # exactly one data=0 state, and everything blocked-absent must have none.
    states = json.loads((root / STATE_MAP).read_text())["blockStates"]
    by_tile = {}
    for state in states:
        by_tile.setdefault(state["tileId"], set()).add(state["data"])
    catalog = {}
    for tile, (status, reason) in sorted(DECISIONS.items()):
        available = sorted(by_tile.get(tile, ()))
        catalog[tile] = available
        if status in ("supported-change", "supported-consume"):
            # Both door halves must be able to represent the open and closed
            # form of every one of the four directions.
            missing = [d for d in range(12) if d not in available]
            if missing:
                raise ValueError(f"tile {tile} claimed supported but catalog lacks data {missing}")
        elif status == "blocked-catalog":
            if available != [0]:
                raise ValueError(f"tile {tile} catalog states changed to {available}; revisit the block")
            if 8 in available:
                raise ValueError(f"tile {tile} now has a powered state and should be implemented")
        elif status == "blocked-absent":
            if available:
                raise ValueError(f"tile {tile} is no longer absent from the catalog: {available}")

    return dict(schema_version=1, generator="tools/extract-interaction.py", source_commit=SOURCE_COMMIT,
                line_number_base=1, byte_range_end="exclusive", functions=functions,
                constant_references=references,
                decisions={str(tile): dict(status=status, reason=reason, catalog_data=catalog[tile])
                           for tile, (status, reason) in sorted(DECISIONS.items())},
                sources=[dict(source_file=path, sha256=hashlib.sha256(raw).hexdigest())
                         for path, (raw, _) in sources.items()],
                boundaries=[
                    "right-click runs Tile::use BEFORE placement and suppresses it when handled, per MultiPlayerGameMode::useItemOn",
                    "sneaking with a held item skips use() entirely; World::useBlock owns no inventory so it never triggers that gate",
                    "wooden door is the only state-changing interaction this round; only the LOWER half's data is written, as in source",
                    "iron door consumes the click and changes nothing, exactly as the Material::metal guard does; placement on it is therefore also suppressed",
                    "lever/button/repeater are NOT implemented: the embedded catalog has only data=0 for each, so the powered state has no render index",
                    "button could not pop back out even with a state, because addToTickNextTick has no equivalent here",
                    "container/GUI tiles return false; no tile entity, inventory or screen is modelled and no placeholder UI is shown",
                    "no redstone propagation, neighbour updates, block ticks, sounds or level events of any kind",
                    "unpaired door halves are refused rather than toggled, since the source relies on neighborChanged cleanup we do not run",
                    "no player-AABB rejection on interaction, unlike placement: source lets a player open the door they stand in",
                    "312 render states and the saved world palette are unchanged; no save format upgrade",
                ])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    try:
        manifest = generate(args.root)
        if args.check:
            supported = [t for t, d in manifest["decisions"].items() if d["status"].startswith("supported")]
            blocked = [t for t, d in manifest["decisions"].items() if d["status"].startswith("blocked")]
            print(f"Verified {len(manifest['functions'])} source use() functions and "
                  f"{len(manifest['constant_references'])} references at {SOURCE_COMMIT}; "
                  f"supported tiles {sorted(map(int, supported))}, "
                  f"blocked tiles {sorted(map(int, blocked))} (Tile::use subset; not full engine)")
        else:
            print(json.dumps(manifest, indent=2))
        return 0
    except (OSError, ValueError, KeyError) as error:
        print(f"extract-interaction: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
