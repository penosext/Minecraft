#!/usr/bin/env python3
"""Read-only provenance/semantics manifest for ItemPlacement's audited subset.

Prints JSON to stdout; --check validates without writing artifacts. This is not a
full Tile engine extractor and does not claim tick/neighbor/physics equivalence.
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
WORLD = "port/upstream/Minecraft.World/"
FUNCTIONS = {
    "TileItem.cpp": ["TileItem::useOn"],
    "Item.cpp": ["Item::getLevelDataForAuxValue"],
    "Tile.cpp": ["Tile::isSolidBlockingTile", "Tile::getPlacedOnFaceDataValue"],
    "Material.cpp": ["Material::isSolidBlocking"],
    "StairTile.cpp": ["StairTile::setPlacedBy", "StairTile::getPlacedOnFaceDataValue"],
    "RotatedPillarTile.cpp": ["RotatedPillarTile::getPlacedOnFaceDataValue"],
    "HalfSlabTile.cpp": ["HalfSlabTile::getPlacedOnFaceDataValue"],
    "StoneSlabTileItem.cpp": ["StoneSlabTileItem::useOn", "StoneSlabTileItem::tryConvertTargetTile"],
    "DoorItem.cpp": ["DoorItem::useOn", "DoorItem::place"],
    "DoorTile.cpp": ["DoorTile::mayPlace"],
    "LadderTile.cpp": ["LadderTile::mayPlace", "LadderTile::getPlacedOnFaceDataValue"],
    "TorchTile.cpp": ["TorchTile::isConnection", "TorchTile::getPlacedOnFaceDataValue"],
    "ButtonTile.cpp": ["ButtonTile::getPlacedOnFaceDataValue"],
    "BasePressurePlateTile.cpp": ["BasePressurePlateTile::mayPlace"],
    "ChestTile.cpp": ["ChestTile::setPlacedBy", "ChestTile::mayPlace"],
    "FurnaceTile.cpp": ["FurnaceTile::setPlacedBy"],
    "DispenserTile.cpp": ["DispenserTile::setPlacedBy"],
    "PistonBaseTile.cpp": ["PistonBaseTile::getNewFacing"],
    "SeedItem.cpp": ["SeedItem::useOn"],
    "SeedFoodItem.cpp": ["SeedFoodItem::useOn"],
    "WaterLilyTileItem.cpp": ["WaterLilyTileItem::use"],
    "SnowItem.cpp": ["SnowItem::useOn"],
    "TopSnowTile.cpp": ["TopSnowTile::mayPlace"],
    "Level.cpp": ["Level::isTopSolidBlocking"],
}


def generate(root):
    spec = importlib.util.spec_from_file_location("renderer_extractor", root / "tools/extract-renderer.py")
    helper = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(helper)
    sources, functions, bodies, constants = {}, [], {}, []

    def source(name):
        path = WORLD + name
        if path not in sources:
            raw = (root / path).read_bytes()
            sources[path] = raw, helper.mask_cpp(raw)
        return path, *sources[path]

    for filename, names in FUNCTIONS.items():
        path, raw, masked = source(filename)
        for name in names:
            signatures = re.findall(rb"^[ \t]*[^\r\n;{}]*\b" + re.escape(name.encode()) + rb"\([^\r\n]*\)[ \t]*\r?$", masked, re.M)
            # Keep overloads (Level top-solid has both coordinate and tile/data).
            if not signatures:
                raise ValueError("missing definition: " + name)
            for signature in signatures:
                signature = signature.strip().decode()
                body, entry = helper.extract_function(raw, masked, signature)
                entry.update(source_file=path, source_text=body.decode(), use="reference for audited rule subset; not copied full engine")
                functions.append(entry)
                bodies.setdefault(name, []).append(body)

    def require(name, text):
        flat = re.sub(rb"\s+", b"", b"\n".join(bodies[name]))
        if re.sub(rb"\s+", b"", text.encode()) not in flat:
            raise ValueError("source placement semantics changed: " + name + ": " + text)

    for name, text in [
        ("TileItem::useOn", "getLevelDataForAuxValue(instance->getAuxValue())"),
        ("TileItem::useOn", "tile->getPlacedOnFaceDataValue"),
    ]:
        # The original spells the getPlaced call through Tile::tiles[tileId].
        if text == "tile->getPlacedOnFaceDataValue":
            text = "Tile::tiles[tileId]->getPlacedOnFaceDataValue"
        require(name, text)
    for name, text in [
        ("TileItem::useOn", "Tile::tiles[tileId]->finalizePlacement(level, x, y, z, dataValue)"),
        ("StairTile::getPlacedOnFaceDataValue", "face == Facing::DOWN || (face != Facing::UP && clickY > 0.5)"),
        ("StairTile::setPlacedBy", "Mth::floor(by->yRot * 4 / (360) + 0.5)"),
        ("RotatedPillarTile::getPlacedOnFaceDataValue", "return type | facing;"),
        ("DoorItem::useOn", "Mth::floor(((player->yRot + 180) * 4) / 360 - 0.5) & 3"),
        ("DoorItem::place", "if (doorLeft && !doorRight) flip = true;"),
        ("DoorItem::place", "else if (solidRight > solidLeft) flip = true;"),
        ("DoorItem::place", "8 | (flip ? 1 : 0)"),
        ("StoneSlabTileItem::useOn", "slabType == instance->getAuxValue()"),
        ("SeedItem::useOn", "targetType == targetLand && level->isEmptyTile(x, y + 1, z)"),
        ("WaterLilyTileItem::use", "level->getMaterial(xt, yt, zt) == Material::water && level->getData(xt, yt, zt) == 0"),
    ]:
        require(name, text)

    references = [
        ("Material.cpp", rb"^[ \t]*Material::(?:explosive|glass|ice|leaves|stone|sand) = [^;]+;"),
        ("Tile.cpp", rb"^[ \t]*Tile::(?:netherRack|tnt) = [^;]+;"),
        ("StairTile.h", rb"static const int (?:UPSIDEDOWN_BIT|DIR_EAST|DIR_WEST|DIR_SOUTH|DIR_NORTH) = [0-9]+;"),
        ("HalfSlabTile.h", rb"static const int (?:TYPE_MASK|TOP_SLOT_BIT) = [0-9]+;"),
        ("RotatedPillarTile.h", rb"static const int (?:MASK_TYPE|MASK_FACING|FACING_Y|FACING_X|FACING_Z) = [^;]+;"),
        ("TopSnowTile.cpp", rb"const int TopSnowTile::(?:MAX_HEIGHT|HEIGHT_MASK) = [0-9]+;"),
        ("Item.cpp", rb"Item::Item\(int id\) : id\( 256 \+ id \)"),
        ("Item.cpp", rb"^[ \t]*Item::(?:door_wood|door_iron|seeds_wheat|wheat|carrots|potato|diode)\s*=.*;"),
    ]
    for filename, expression in references:
        path, raw, _ = source(filename)
        matches = list(re.finditer(expression, raw, re.M))
        if not matches:
            raise ValueError("missing constant/registration: " + filename)
        for match in matches:
            constants.append(dict(source_file=path, source_text=match.group().decode(), start_byte=match.start(), end_byte=match.end(),
                                  start_line=raw.count(b"\n", 0, match.start()) + 1, sha256=hashlib.sha256(match.group()).hexdigest()))
    return dict(schema_version=1, generator="tools/extract-placement.py", byte_range_end="exclusive",
                source_commit="09df8928eeb15328bca499d225e48ea95af6d119", functions=functions, constant_references=constants,
                semantics=dict(stairs_direction={"east": 0, "west": 1, "south": 2, "north": 3}, stairs_upper_bit=4,
                               slab_type_mask=7, slab_upper_bit=8, log_axis={"y": 0, "x": 4, "z": 8},
                               door_upper_bit=8, door_hinge_bit=1, source_yaw="-World yaw", wheat_seed_id=295, wheat_nonseed_id=296,
                               source_item_ids="resolved from ItemCatalog ItemSpec; no copied placement item registry"),
                boundaries=["audited ItemSpec to exact catalog world-state subset, not full Tile engine",
                            "unknown/unimplemented metadata rules return false; no catalog representative fallback",
                            "World keeps conservative full-cell DDA/player collision, not exact original block shapes",
                            "attachment subset requires clicked face support; no unrelated-neighbor fallback",
                            "single chests only; no tile entities, double chest rewrites, ticks, growth, redstone or neighbor physics",
                            "source-water lily only: legacy render-only liquid data cannot distinguish flowing levels",
                            "ordinary grass/cube/leaves material subset; support classification is conservative audited subset",
                            "batch publication allocation-safe; existing v2 key-palette chunks retain all placed states; no format upgrade"],
                sources=[dict(source_file=path, sha256=hashlib.sha256(raw).hexdigest()) for path, (raw, _) in sources.items()])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    try:
        manifest = generate(args.root)
        if args.check:
            print(f"Verified {len(manifest['functions'])} source placement functions and {len(manifest['constant_references'])} constants/registrations (subset only)")
        else:
            print(json.dumps(manifest, indent=2))
        return 0
    except (OSError, ValueError) as error:
        print(f"extract-placement: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
