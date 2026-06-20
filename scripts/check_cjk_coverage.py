#!/usr/bin/env python3
"""Check OpenOS CJK font resource coverage against project UI/document text.

The script understands OpenOS .ofnt and .ofntz resources produced by
scripts/generate_cjk_font.py. It scans UTF-8 source/document files, collects CJK
codepoints, and reports characters missing from the resource.
"""
from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path
from typing import Iterable, Sequence, Set

OFNT_MAGIC = 0x544E464F
OFNTZ_MAGIC = 0x5A544E4F
OFNT_FLAG_U16_CODEPOINTS = 0x00000001
OFNT_FLAG_U32_CODEPOINTS = 0x00000002
OFNTZ_FLAG_RLE8 = 0x00000001

DEFAULT_SCAN_DIRS = ("README.md", "TODOLIST.md", "docs", "src")
TEXT_SUFFIXES = {
    ".c", ".h", ".S", ".asm", ".ld", ".txt", ".md", ".py", ".ps1", ".sh",
}
SKIP_DIRS = {".git", "target", "skills", "node_modules", "__pycache__"}


def is_cjk_codepoint(cp: int) -> bool:
    return (
        (0x3400 <= cp <= 0x4DBF)
        or (0x4E00 <= cp <= 0x9FFF)
        or (0xF900 <= cp <= 0xFAFF)
        or (0x20000 <= cp <= 0x2FA1F)
        or (0x3000 <= cp <= 0x303F)
        or (0xFF00 <= cp <= 0xFFEF)
    )


def rle8_decompress(data: bytes, expected_size: int) -> bytes:
    out = bytearray()
    i = 0
    while i < len(data) and len(out) < expected_size:
        control = data[i]
        i += 1
        count = (control & 0x7F) + 1
        if control & 0x80:
            if i >= len(data):
                raise ValueError("truncated RLE run")
            out.extend([data[i]] * count)
            i += 1
        else:
            if i + count > len(data):
                raise ValueError("truncated RLE literal")
            out.extend(data[i:i + count])
            i += count
    if len(out) != expected_size:
        raise ValueError(f"RLE size mismatch: got {len(out)}, expected {expected_size}")
    return bytes(out)


def load_resource_payload(path: Path) -> bytes:
    data = path.read_bytes()
    if len(data) < 4:
        raise ValueError(f"{path} is too small")
    magic = struct.unpack_from("<I", data, 0)[0]
    if magic == OFNT_MAGIC:
        return data
    if magic != OFNTZ_MAGIC:
        raise ValueError(f"{path} is not an OFNT/OFNTZ resource")
    if len(data) < 32:
        raise ValueError(f"{path} has truncated OFNTZ header")
    _magic, version, flags, raw_size, compressed_size, inner_magic, data_offset, _reserved = struct.unpack_from(
        "<IIIIIIII", data, 0
    )
    if version != 1:
        raise ValueError(f"unsupported OFNTZ version {version}")
    if flags != OFNTZ_FLAG_RLE8:
        raise ValueError(f"unsupported OFNTZ flags 0x{flags:X}")
    if inner_magic != OFNT_MAGIC:
        raise ValueError("OFNTZ inner magic mismatch")
    payload = data[data_offset:data_offset + compressed_size]
    return rle8_decompress(payload, raw_size)


def load_ofnt_codepoints(path: Path) -> Set[int]:
    data = load_resource_payload(path)
    if len(data) < 40:
        raise ValueError(f"{path} has truncated OFNT header")
    magic, version, flags, count, width, height, cp_offset, _bitmap_offset, _stride, _reserved = struct.unpack_from(
        "<IIIIIIIIII", data, 0
    )
    if magic != OFNT_MAGIC:
        raise ValueError("OFNT magic mismatch")
    if version != 1:
        raise ValueError(f"unsupported OFNT version {version}")
    if width == 0 or height == 0:
        raise ValueError("invalid OFNT dimensions")
    if flags & OFNT_FLAG_U16_CODEPOINTS:
        fmt = "<H"
        step = 2
    elif flags & OFNT_FLAG_U32_CODEPOINTS:
        fmt = "<I"
        step = 4
    else:
        raise ValueError(f"unsupported OFNT codepoint flags 0x{flags:X}")
    end = cp_offset + count * step
    if end > len(data):
        raise ValueError("OFNT codepoint table is truncated")
    return {struct.unpack_from(fmt, data, cp_offset + i * step)[0] for i in range(count)}


def iter_text_files(paths: Sequence[Path]) -> Iterable[Path]:
    for path in paths:
        if not path.exists():
            continue
        if path.is_file():
            if path.suffix in TEXT_SUFFIXES or path.name in {"README.md", "TODOLIST.md"}:
                yield path
            continue
        for child in path.rglob("*"):
            if any(part in SKIP_DIRS for part in child.parts):
                continue
            if child.is_file() and child.suffix in TEXT_SUFFIXES:
                yield child


def scan_codepoints(paths: Sequence[Path]) -> Set[int]:
    out: Set[int] = set()
    for path in iter_text_files(paths):
        try:
            text = path.read_text(encoding="utf-8")
        except UnicodeDecodeError:
            continue
        out.update(ord(ch) for ch in text if is_cjk_codepoint(ord(ch)))
    return out


def main(argv: Sequence[str]) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("resource", type=Path, help=".ofnt or .ofntz resource to inspect")
    parser.add_argument("--scan", type=Path, action="append", default=[], help="file or directory to scan")
    parser.add_argument("--fail-on-missing", action="store_true", help="return non-zero if any glyph is missing")
    parser.add_argument("--max-list", type=int, default=32, help="maximum missing glyphs to print")
    args = parser.parse_args(argv)

    scan_paths = args.scan or [Path(p) for p in DEFAULT_SCAN_DIRS]
    glyphs = load_ofnt_codepoints(args.resource)
    needed = scan_codepoints(scan_paths)
    missing = sorted(needed - glyphs)
    covered = len(needed) - len(missing)
    percent = 100.0 if not needed else covered * 100.0 / len(needed)

    print(
        f"CJK coverage: resource={args.resource} glyphs={len(glyphs)} "
        f"needed={len(needed)} covered={covered} missing={len(missing)} ({percent:.2f}%)"
    )
    if missing:
        sample = " ".join(f"U+{cp:04X}({chr(cp)})" for cp in missing[: args.max_list])
        print(f"WARNING: missing CJK glyphs: {sample}", file=sys.stderr)
        if len(missing) > args.max_list:
            print(f"WARNING: ... and {len(missing) - args.max_list} more", file=sys.stderr)
    return 1 if missing and args.fail_on_missing else 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
