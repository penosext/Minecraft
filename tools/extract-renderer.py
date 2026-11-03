#!/usr/bin/env python3
"""Extract unmodified legacy block-rendering functions and their provenance.

Run from any directory. Outputs default to jsapi/build/generated beside this
repository's sources. --check verifies both generated files without writing.
SHA-256 values cover the original bytes from the signature through the matching
closing brace (inclusive); line numbers are one-based and byte ends exclusive.
"""

import argparse
import hashlib
import json
from pathlib import Path
import re
import sys


RENDERER_SOURCE = "port/upstream/Minecraft.Client/TileRenderer.cpp"
GRASS_SOURCE = "port/upstream/Minecraft.World/GrassTile.cpp"
TARGETS = (
    (RENDERER_SOURCE,
     "bool TileRenderer::tesselateBlockInWorld(Tile* tt, int x, int y, int z, float r, float g, float b)"),
    (RENDERER_SOURCE,
     "void TileRenderer::renderFaceDown(Tile* tt, double x, double y, double z, Icon *tex)"),
    (RENDERER_SOURCE,
     "void TileRenderer::renderFaceUp(Tile* tt, double x, double y, double z, Icon *tex)"),
    # These four upstream names intentionally have no 'Face' prefix.
    (RENDERER_SOURCE,
     "void TileRenderer::renderNorth(Tile* tt, double x, double y, double z, Icon *tex)"),
    (RENDERER_SOURCE,
     "void TileRenderer::renderSouth(Tile* tt, double x, double y, double z, Icon *tex)"),
    (RENDERER_SOURCE,
     "void TileRenderer::renderWest(Tile* tt, double x, double y, double z, Icon *tex)"),
    (RENDERER_SOURCE,
     "void TileRenderer::renderEast(Tile* tt, double x, double y, double z, Icon *tex)"),
    (GRASS_SOURCE, "Icon *GrassTile::getTexture(int face, int data)"),
    (GRASS_SOURCE,
     "Icon *GrassTile::getTexture(LevelSource *level, int x, int y, int z, int face)"),
    (GRASS_SOURCE, "void GrassTile::registerIcons(IconRegister *iconRegister)"),
    (GRASS_SOURCE,
     "int GrassTile::getColor(LevelSource *level, int x, int y, int z, int data)"),
    (GRASS_SOURCE, "Icon *GrassTile::getSideTextureOverlay()"),
    (RENDERER_SOURCE,
     "void TileRenderer::tesselateCrossTexture(Tile* tt, int data, float x, float y, float z, float scale)"),
    (RENDERER_SOURCE,
     "bool TileRenderer::tesselateCactusInWorld(Tile* tt, int x, int y, int z, float r, float g, float b)"),
    (RENDERER_SOURCE,
     "bool TileRenderer::tesselateLilypadInWorld(Tile *tt, int x, int y, int z)"),
    (RENDERER_SOURCE,
     "void TileRenderer::tesselateRowTexture(Tile* tt, int data, float x, float y, float z)"),
)
INCLUDE_NAME = "LegacyTileRenderer.inc"
MANIFEST_NAME = "LegacyTileRenderer.manifest.json"
RAW_STRING = re.compile(rb'(?:u8|u|U|L)?R"([^ ()\\\t\v\f\r\n]{0,16})\(')
TOKEN = re.compile(r"[A-Za-z_][A-Za-z_0-9]*|::|[^\s]")


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def mask_cpp(source):
    """Hide comments and literals, preserving byte offsets and line endings."""
    masked = bytearray(source)
    pos = 0
    size = len(source)
    while pos < size:
        start = pos
        if source.startswith(b"//", pos):
            end = source.find(b"\n", pos + 2)
            while end != -1 and source[pos:end].rstrip(b"\r").endswith(b"\\"):
                pos = end + 1
                end = source.find(b"\n", pos)
            pos = size if end == -1 else end
        elif source.startswith(b"/*", pos):
            end = source.find(b"*/", pos + 2)
            if end == -1:
                raise ValueError("unterminated C++ block comment")
            pos = end + 2
        else:
            raw = RAW_STRING.match(source, pos)
            if raw:
                terminator = b")" + raw.group(1) + b'"'
                end = source.find(terminator, raw.end())
                if end == -1:
                    raise ValueError("unterminated C++ raw string")
                pos = end + len(terminator)
            elif source[pos] in (ord('"'), ord("'")):
                quote = source[pos]
                pos += 1
                while pos < size:
                    if source[pos] == ord("\\"):
                        pos += 2
                    elif source[pos] == quote:
                        pos += 1
                        break
                    else:
                        pos += 1
                else:
                    raise ValueError("unterminated C++ string or character literal")
            else:
                pos += 1
                continue
        for index in range(start, min(pos, size)):
            if source[index] not in (ord("\r"), ord("\n")):
                masked[index] = ord(" ")
    return bytes(masked)


def extract_function(source, masked, signature):
    """Locate an exact overload and balance only real C++ braces."""
    tokens = TOKEN.findall(signature)
    pattern = rb"^[ \t]*" + rb"\s*".join(
        re.escape(token.encode("ascii")) for token in tokens
    ) + rb"\s*\{"
    matches = list(re.finditer(pattern, masked, re.MULTILINE))
    if len(matches) != 1:
        raise ValueError(f"expected exactly one definition of {signature}; found {len(matches)}")
    match = matches[0]
    start = match.start()
    depth = 0
    for index in range(match.end() - 1, len(masked)):
        if masked[index] == ord("{"):
            depth += 1
        elif masked[index] == ord("}"):
            depth -= 1
            if depth == 0:
                end = index + 1
                return source[start:end], {
                    "signature": signature,
                    "start_line": source.count(b"\n", 0, start) + 1,
                    "end_line": source.count(b"\n", 0, index) + 1,
                    "start_byte": start,
                    "end_byte": end,
                    "sha256": sha256(source[start:end]),
                }
    raise ValueError(f"unbalanced braces in {signature}")


def generate(root):
    sources = {}
    for source_file, _ in TARGETS:
        if source_file not in sources:
            source = (root / source_file).read_bytes()
            sources[source_file] = (source, mask_cpp(source))

    output = bytearray(
        b"// Generated by tools/extract-renderer.py. Do not edit.\n"
        b"// Include at global scope after LegacyBlock.hpp, in one translation unit.\n"
        b"// Function bytes, including platform conditionals, are copied verbatim.\n"
        b"namespace legacy {\n\n"
    )
    functions = []
    for source_file, signature in TARGETS:
        source, masked = sources[source_file]
        body, entry = extract_function(source, masked, signature)
        entry["source_file"] = source_file
        output.extend(
            (f"// source: {source_file}:{entry['start_line']}-{entry['end_line']}\n"
             f"// sha256: {entry['sha256']}\n").encode("ascii")
        )
        entry["generated_start_byte"] = len(output)
        entry["generated_start_line"] = output.count(b"\n") + 1
        output.extend(body)
        entry["generated_end_byte"] = len(output)
        entry["generated_end_line"] = output.count(b"\n") + 1
        output.extend(b"\n\n")
        functions.append(entry)
    output.extend(b"} // namespace legacy\n")
    include = bytes(output)
    manifest = {
        "schema_version": 1,
        "generator": "tools/extract-renderer.py",
        "line_number_base": 1,
        "byte_range_end": "exclusive",
        "hash_scope": "original function bytes from signature through closing brace inclusive",
        "generated_file": INCLUDE_NAME,
        "generated_sha256": sha256(include),
        "sources": [
            {"source_file": path, "sha256": sha256(source)}
            for path, (source, _) in sources.items()
        ],
        "functions": functions,
    }
    return {
        INCLUDE_NAME: include,
        MANIFEST_NAME: (json.dumps(manifest, indent=2, ensure_ascii=True) + "\n").encode("utf-8"),
    }


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1],
                        help="repository root (default: parent of tools/)")
    parser.add_argument("--output-dir", type=Path,
                        help="output directory (default: ROOT/jsapi/build/generated)")
    parser.add_argument("--check", action="store_true",
                        help="verify existing outputs without modifying files")
    args = parser.parse_args(argv)
    root = args.root.resolve()
    output_dir = args.output_dir or root / "jsapi/build/generated"
    try:
        outputs = generate(root)
        if args.check:
            stale = [name for name, content in outputs.items()
                     if not (output_dir / name).is_file()
                     or (output_dir / name).read_bytes() != content]
            if stale:
                raise ValueError("missing or stale generated files: " + ", ".join(stale))
        else:
            output_dir.mkdir(parents=True, exist_ok=True)
            for name, content in outputs.items():
                path = output_dir / name
                if not path.is_file() or path.read_bytes() != content:
                    path.write_bytes(content)
    except (OSError, ValueError) as error:
        print(f"extract-renderer: {error}", file=sys.stderr)
        return 1
    action = "Verified" if args.check else "Extracted"
    print(f"{action} {len(TARGETS)} unmodified functions in {output_dir / INCLUDE_NAME}")
    print(f"Provenance: {output_dir / MANIFEST_NAME}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
