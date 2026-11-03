#!/usr/bin/env python3
"""Generate byte-preserving build inputs; --check audits without writing files."""
import argparse
import hashlib
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parent
UPSTREAM = ROOT.parent / "upstream" / "Minecraft.World"
COMMIT = "09df8928eeb15328bca499d225e48ea95af6d119"
RAW = f"https://raw.githubusercontent.com/gstaaij/minecraft-console/{COMMIT}/Minecraft.World/"

# Pin the received files, not hashes computed anew on each build.
PINNED = {
    "Random.cpp": "d0a0952991ca9cace3200fa709a8fea8aeb8f7f3144dcac2a5869cbd97a38193",
    "Random.h": "6155c9a5521b1a0a36405bdb0082be9d765e786278d072d51191a527d1f75a3e",
    "ImprovedNoise.cpp": "74dec73dacdba734dbbc0147a1bb4af0ea49279ba176efb51744c5a78f1a3ffe",
    "ImprovedNoise.h": "4b751525bc538deb102c1deee3d0965fb7940f59f41c89938879822b259728c9",
    "PerlinNoise.cpp": "0c96a51308a6936162ba48e33476c7cf1044d37fbde116b049ded1f9819d8650",
    "PerlinNoise.h": "bc07e9f79b0edb685a6b82b3f013fe03aa36cf24d649b50e4f3b41de16eb2cee",
    "Synth.h": "6452cffd965dd23af84e44fe522c6ed5f6e772c514f9092ecab882694276ea31",
    "ArrayWithLength.h": "8ae25b36b2811843c436e2a1b4f5270a30a58c30ef05c884900d6643419ba1bf",
    "Mth.cpp": "3b86fd381c759a4e69c717530c9e83a3fd537bac7be37c4a085d71f3394fd911",
    "Mth.h": "406b4b6168e78f0aa1489802106ed58ede7bd4ad12ad80afa00d68c409b1148c",
    "stdafx.h": "e915de70166a8be9a324de188f65a1ede81b7cf7bbd70bce2c231d40ea6576cf",
}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def unique(data, token):
    if data.count(token) != 1:
        raise ValueError(f"Expected one byte marker, got {data.count(token)}: {token!r}")
    return data.index(token)


def span(name, data, start, end):
    return {
        "source": name,
        "source_byte_range": [start, end],
        "source_lines_inclusive": [data[:start].count(b"\n") + 1,
                                   data[:end - 1].count(b"\n") + 1],
        "sha256": sha(data[start:end]),
    }


def generate():
    sources = {}
    for name, expected in PINNED.items():
        data = (UPSTREAM / name).read_bytes()
        if sha(data) != expected:
            raise ValueError(f"Upstream SHA-256 mismatch: {name}")
        sources[name] = data

    outputs, records = {}, []
    old_include = b'#include "stdafx.h"'
    new_include = b'#include "PlatformPch.h"'
    for name in ("Random.cpp", "ImprovedNoise.cpp", "PerlinNoise.cpp"):
        data = sources[name]
        pos = unique(data, old_include)
        if pos != 0:
            raise ValueError(f"Unexpected stdafx include position: {name}")
        generated = data.replace(old_include, new_include, 1)
        records.append({
            "output": name,
            "operation": "Replace only stdafx include; all remaining bytes unchanged",
            "replaced_source_byte_range": [pos, pos + len(old_include)],
            "original_bytes": old_include.decode(),
            "replacement_bytes": new_include.decode(),
            "unchanged_tail": span(name, data, len(old_include), len(data)),
        })
        # Each complete function definition in these files is covered separately.
        definitions = list(re.finditer(
            rb"^(?:[A-Za-z_][A-Za-z_0-9]*[ \t]+)?[A-Za-z_][A-Za-z_0-9]*::~?[A-Za-z_][A-Za-z_0-9]*\([^\r\n]*\)[ \t]*\r?\n\{",
            data, re.MULTILINE))
        counts = {"Random.cpp": 12, "ImprovedNoise.cpp": 11, "PerlinNoise.cpp": 8}
        if len(definitions) != counts[name]:
            raise ValueError(f"Unexpected function count in {name}: {len(definitions)}")
        functions = []
        for match in definitions:
            # These upstream definitions have no nested column-zero closing brace.
            close = re.search(rb"^\}", data[match.end():], re.MULTILINE)
            if close is None:
                raise ValueError(f"Missing function end: {name}")
            end = match.end() + close.end()
            item = span(name, data, match.start(), end)
            item["signature"] = data[match.start():].splitlines()[0].decode()
            # Verify the generated output has precisely the same function bytes.
            new_start = match.start() + len(new_include) - len(old_include)
            if generated[new_start:new_start + end - match.start()] != data[match.start():end]:
                raise ValueError(f"Function bytes changed: {item['signature']}")
            item["output_byte_range"] = [new_start, new_start + end - match.start()]
            functions.append(item)
        records[-1]["unchanged_functions"] = functions
        outputs[name] = generated

    for name in ("Random.h", "ImprovedNoise.h", "PerlinNoise.h", "Synth.h"):
        outputs[name] = sources[name]
        records.append({"output": name, "operation": "Byte-identical header copy",
                        "segments": [span(name, sources[name], 0, len(sources[name]))]})

    data = sources["ArrayWithLength.h"]
    start = unique(data, b"template <class T> class arrayWithLength")
    end = data.index(b"};", start) + len(b"};")
    alias_start = unique(data, b"typedef arrayWithLength<double> doubleArray;")
    alias_end = alias_start + len(b"typedef arrayWithLength<double> doubleArray;")
    outputs["ArrayWithLength.h"] = (b"#pragma once\n#include <algorithm>\n#include <cassert>\n#include <cstring>\n\n"
                                    + data[start:end] + b"\n\n" + data[alias_start:alias_end] + b"\n")
    records.append({"output": "ArrayWithLength.h", "operation": "Exact template and doubleArray extraction; platform includes only",
                    "segments": [span("ArrayWithLength.h", data, start, end),
                                 span("ArrayWithLength.h", data, alias_start, alias_end)]})

    data = sources["Mth.cpp"]
    start = unique(data, b"int64_t Mth::lfloor(double v)")
    end = data.index(b"}", start) + len(b"}")
    outputs["Mth.cpp"] = b'#include "PlatformPch.h"\n#include "Mth.h"\n\n' + data[start:end] + b"\n"
    records.append({"output": "Mth.cpp", "operation": "Exact Mth::lfloor definition extraction; platform includes only",
                    "segments": [span("Mth.cpp", data, start, end)]})
    data = sources["Mth.h"]
    start = unique(data, b"static int64_t lfloor(double v);")
    end = start + len(b"static int64_t lfloor(double v);")
    outputs["Mth.h"] = b"#pragma once\n#include <cstdint>\nclass Mth\n{\npublic:\n    " + data[start:end] + b"\n};\n"
    records.append({"output": "Mth.h", "operation": "Exact lfloor declaration in minimal class shell",
                    "segments": [span("Mth.h", data, start, end)]})

    manifest = {
        "schema": 1,
        "repository": "https://github.com/gstaaij/minecraft-console.git",
        "commit": COMMIT,
        "range_convention": "Zero-based byte offsets [begin, end); one-based inclusive lines",
        "sources": {name: {"sha256": PINNED[name], "bytes": len(data), "raw_url": RAW + name}
                    for name, data in sorted(sources.items())},
        "generated": {name: {"sha256": sha(data), "bytes": len(data)}
                      for name, data in sorted(outputs.items())},
        "transformations": records,
        "compiler_contract": ["C++11", "-fwrapv", "-ffp-contract=off", "-fno-fast-math", "-UNDEBUG"],
        "limitations": [
            "Random::nextLong retains signed-left-shift undefined behavior for negative next(32); -fwrapv does not fix it; tests never call nextLong.",
            "Explicit seeds use the original Java-style 48-bit LCG; distinct 64-bit seeds that share their low 48 bits intentionally collide.",
            "Default Random seed uses steady_clock via QueryPerformanceCounter shim; not Windows timer equivalence.",
            "Synth::create is declared by the unchanged header but not implemented or used in this baseline.",
            "No RandomLevelSource, biome, chunk, structure, feature, terrain or app integration is compiled.",
            "Floating point contraction is disabled; no claim of bit equality to an unavailable original-console executable.",
        ],
    }
    outputs["source-manifest.json"] = (json.dumps(manifest, indent=2, sort_keys=True) + "\n").encode()
    return outputs


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    output = args.output.resolve()
    # Permit standalone audits and the application's native build tree only.
    app_build = ROOT.parents[1] / 'jsapi' / 'build'
    if output == ROOT or not (output.is_relative_to(ROOT) or output.is_relative_to(app_build)):
        parser.error("--output must be inside original-core or the app native build tree")
    files = generate()
    if not args.check:
        output.mkdir(parents=True, exist_ok=True)
    for name, data in files.items():
        path = output / name
        if args.check:
            if not path.is_file() or path.read_bytes() != data:
                raise ValueError(f"Generated byte audit failed: {path}")
        elif not path.is_file() or path.read_bytes() != data:
            path.write_bytes(data)
    print(f"{'Audited' if args.check else 'Generated'} {len(files)} files; all pinned source hashes, 31 core definitions and exact Mth::lfloor extraction verified")


if __name__ == "__main__":
    main()
