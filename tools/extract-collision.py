#!/usr/bin/env python3
"""Audit the source AABB subset used by BlockCollision (not a full engine).

Prints a provenance JSON manifest to stdout. --check is read-only: pins the
upstream revision, records SHA-256 and exact [start_byte,end_byte) per source
function/registration, and validates load-bearing semantics. No render meshes
are collision authority. This script neither generates nor writes artifacts.
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
FUNCTIONS = {
    "Tile.cpp": ["Tile::getAABB", "Tile::addAABBs", "Tile::isCubeShaped"],
    "Bush.cpp": ["Bush::getAABB"],
    "TorchTile.cpp": ["TorchTile::getAABB"],
    "LiquidTile.cpp": ["LiquidTile::getAABB"],
    "LadderTile.cpp": ["LadderTile::getAABB", "LadderTile::setShape"],
    "ReedTile.cpp": ["ReedTile::getAABB"],
    "VineTile.cpp": ["VineTile::getAABB"],
    "BaseRailTile.cpp": ["BaseRailTile::getAABB"],
    "RedStoneDustTile.cpp": ["RedStoneDustTile::getAABB"],
    "LeverTile.cpp": ["LeverTile::getAABB"],
    "ButtonTile.cpp": ["ButtonTile::getAABB"],
    "BasePressurePlateTile.cpp": ["BasePressurePlateTile::getAABB"],
    "TripWireTile.cpp": ["TripWireTile::getAABB"],
    "TripWireSourceTile.cpp": ["TripWireSourceTile::getAABB"],
    "FireTile.cpp": ["FireTile::getAABB"],
    "WebTile.cpp": ["WebTile::getAABB"],
    "HalfSlabTile.cpp": ["HalfSlabTile::updateShape", "HalfSlabTile::addAABBs", "HalfSlabTile::isCubeShaped"],
    "TopSnowTile.cpp": ["TopSnowTile::getAABB", "TopSnowTile::getHeight"],
    "StairTile.cpp": ["StairTile::setBaseShape", "StairTile::setStepShape", "StairTile::setInnerPieceShape", "StairTile::isLockAttached", "StairTile::addAABBs"],
    "DoorTile.cpp": ["DoorTile::getAABB", "DoorTile::setShape", "DoorTile::getCompositeData"],
    "FenceTile.cpp": ["FenceTile::addAABBs", "FenceTile::connectsTo"],
    "ThinFenceTile.cpp": ["ThinFenceTile::addAABBs", "ThinFenceTile::attachsTo"],
    "FarmTile.cpp": ["FarmTile::updateDefaultShape", "FarmTile::getAABB"],
    "SoulSandTile.cpp": ["SoulSandTile::getAABB"],
    "CactusTile.cpp": ["CactusTile::getAABB"],
    "ChestTile.cpp": ["ChestTile::updateShape"],
    "WaterLilyTile.cpp": ["WaterlilyTile::updateDefaultShape", "WaterlilyTile::getAABB", "WaterlilyTile::addAABBs"],
    "FlowerPotTile.cpp": ["FlowerPotTile::updateDefaultShape"],
    "DiodeTile.cpp": ["DiodeTile::DiodeTile"],
    "CauldronTile.cpp": ["CauldronTile::addAABBs"],
    "TheEndPortalFrameTile.cpp": ["TheEndPortalFrameTile::addAABBs", "TheEndPortalFrameTile::hasEye"],
    "CocoaTile.cpp": ["CocoaTile::getAABB", "CocoaTile::updateShape"],
    "PistonBaseTile.cpp": ["PistonBaseTile::addAABBs", "PistonBaseTile::isCubeShaped"],
    "Material.cpp": ["Material::isSolidBlocking"],
    "GlassTile.cpp": ["GlassTile::isCubeShaped"],
    "LivingEntity.cpp": ["LivingEntity::onLadder", "LivingEntity::travel"],
    "Level.cpp": ["Level::getCubes"],
    "AABB.cpp": ["AABB::intersects"],
}


def generate(root):
    spec = importlib.util.spec_from_file_location("collision_source_parser", root / "tools/extract-renderer.py")
    helper = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(helper)
    sources, functions, bodies, references = {}, [], {}, []

    def source(name):
        path = WORLD + name
        if path not in sources:
            raw = (root / path).read_bytes()
            sources[path] = raw, helper.mask_cpp(raw)
        return path, *sources[path]

    for filename, names in FUNCTIONS.items():
        path, raw, masked = source(filename)
        for name in names:
            matches = re.findall(rb"^[ \t]*[^\r\n;{}]*\b" + re.escape(name.encode()) + rb"\([^\r\n]*\)[^\r\n;{}]*\r?$", masked, re.M)
            if not matches:
                raise ValueError("missing collision definition: " + name)
            for signature in matches:
                body, entry = helper.extract_function(raw, masked, signature.strip().decode())
                entry.update(source_file=path, source_text=body.decode(), use="audited collision subset, not copied full engine")
                functions.append(entry)
                bodies.setdefault(name, []).append(body)

    def require(name, text):
        flat = re.sub(rb"\s+", b"", b"\n".join(bodies[name]))
        if re.sub(rb"\s+", b"", text.encode()) not in flat:
            raise ValueError("source collision semantics changed: " + name + ": " + text)

    for name in ["Bush", "TorchTile", "LiquidTile", "ReedTile", "VineTile", "BaseRailTile", "RedStoneDustTile", "LeverTile", "ButtonTile", "BasePressurePlateTile", "TripWireTile", "TripWireSourceTile", "FireTile", "WebTile"]:
        require(name + "::getAABB", "return nullptr;")
    for name, text in [
        ("Tile::addAABBs", "aabb != nullptr && box->intersects(aabb)"),
        ("LadderTile::getAABB", "return Tile::getAABB(level, x, y, z);"),
        ("LadderTile::setShape", "float r = 2 / 16.0f;"),
        ("LadderTile::setShape", "if (dir == 2) setShape(0, 0, 1 - r, 1, 1, 1);"),
        ("HalfSlabTile::updateShape", "setShape(0, 0.5f, 0, 1, 1, 1);"),
        ("HalfSlabTile::updateShape", "setShape(0, 0, 0, 1, 0.5f, 1);"),
        ("TopSnowTile::getAABB", "y + (height * offset)"),
        ("StairTile::addAABBs", "bool checkInnerPiece = setStepShape(level, x, y, z);"),
        ("StairTile::addAABBs", "setInnerPieceShape(level, x, y, z)"),
        ("DoorTile::setShape", "float r = 3 / 16.0f;"),
        ("DoorTile::getCompositeData", "lowerData = level->getData(x, y - 1, z);"),
        ("DoorTile::getCompositeData", "bool isRightHinge = (upperData & 1) != 0;"),
        ("FenceTile::addAABBs", "setShape(west, 0, north, east, 1.5f, south);"),
        ("FenceTile::connectsTo", "tileInstance->material->isSolidBlocking() && tileInstance->isCubeShaped()"),
        ("FenceTile::connectsTo", "return tileInstance->material != Material::vegetable;"),
        ("ThinFenceTile::attachsTo", "Tile::solid[tile]"),
        ("FarmTile::getAABB", "x + 1, y + 1, z + 1"),
        ("SoulSandTile::getAABB", "float r = 2 / 16.0f;"),
        ("CactusTile::getAABB", "y + 1 - r"),
        ("CauldronTile::addAABBs", "setShape(0, 0, 0, 1, 5.0f / 16.0f, 1);"),
        ("Material::isSolidBlocking", "if (_neverBuildable) return false;"),
        ("LivingEntity::onLadder", "Mth::floor(bb->y0)"),
        ("LivingEntity::travel", "if (yd < -0.15) yd = -0.15;"),
        ("LivingEntity::travel", "if (horizontalCollision && onLadder())"),
        ("LivingEntity::travel", "yd = 0.2;"),
    ]:
        require(name, text)
    # Capture exact material/registration bytes as well as function ranges;
    # attachment predicates cannot be derived from render-shape strings.
    for filename, expression in [
        ("Material.cpp", rb"^[ \t]*Material::(?:explosive|glass|ice|leaves|vegetable) = [^;]+;"),
        ("Tile.cpp", rb"^[ \t]*Tile::[A-Za-z_]+\s*=\s*(?:static_cast<[^;]+|\(new [^;]+);"),
        ("StairTile.h", rb"static const int (?:UPSIDEDOWN_BIT|DIR_EAST|DIR_WEST|DIR_SOUTH|DIR_NORTH) = [0-9]+;"),
        ("TheEndPortalFrameTile.h", rb"static const int EYE_BIT = [0-9]+;"),
    ]:
        path, raw, _ = source(filename)
        matches = list(re.finditer(expression, raw, re.M))
        if not matches:
            raise ValueError("missing collision reference: " + filename)
        for match in matches:
            references.append(dict(source_file=path, source_text=match.group().decode(), start_byte=match.start(), end_byte=match.end(),
                                   start_line=raw.count(b"\n", 0, match.start())+1, sha256=hashlib.sha256(match.group()).hexdigest()))
    return dict(schema_version=1, generator="tools/extract-collision.py", source_commit=SOURCE_COMMIT,
                line_number_base=1, byte_range_end="exclusive", functions=functions, constant_references=references,
                sources=[dict(source_file=path, sha256=hashlib.sha256(raw).hexdigest()) for path, (raw, _) in sources.items()],
                boundaries=["current catalog source TileID/data+neighbors subset, NOT full Tile/Entity engine",
                            "no render-mesh-based collision classification; source farm box is full despite 15/16 rendering",
                            "unknown state/id and invalid ladder data0 conservatively full; no guessed orientation or save remapping",
                            "catalog states retain only represented metadata, not all upstream fluid levels/tile entities",
                            "no fluid forces/swimming/damage/health/web/soulsand slowing/ticks/redstone/neighbor updates",
                            "World keeps .5 width and simplified seconds-based gravity/one-block step",
                            "ladder .15/.2 per tick adapted at 20Hz to 3/4 blocks per second; not original full physics",
                            "World candidate scan includes cell below for fence1.5; unavailable chunks and finite faces remain walls",
                            "placement player-overlap and picking are separate callers, not authoritative collision geometry"])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    try:
        manifest = generate(args.root)
        if args.check:
            print(f"Verified {len(manifest['functions'])} source collision functions and {len(manifest['constant_references'])} references at {SOURCE_COMMIT} (AABB subset; not full engine)")
        else:
            print(json.dumps(manifest, indent=2))
        return 0
    except (OSError, ValueError) as error:
        print(f"extract-collision: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
