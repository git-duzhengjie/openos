#!/usr/bin/env python3
"""Generate OpenOS 16x16 CJK bitmap glyph tables/resources from a real font.

The small generated C source remains the always-available kernel fallback.
For wider Chinese coverage, this script can also emit external bitmap resources
that the kernel font renderer can load at runtime from VFS/ramfs/disk.

Resource formats:
  .ofnt  - uncompressed OpenOS bitmap font resource
  .ofntz - RLE-compressed wrapper containing one .ofnt payload

Dependencies:
  - Pillow when regenerating glyphs from TTF/OTF/TTC; or
  - use the pre-generated src/kernel/generated/cjk_font.c checked into the tree.
"""
from __future__ import annotations

import argparse
import os
import re
import struct
import sys
from pathlib import Path
from typing import Iterable, List, Sequence, Tuple

WIDTH = 16
HEIGHT = 16
OFNT_MAGIC = 0x544E464F  # "OFNT", little-endian
OFNTZ_MAGIC = 0x5A544E4F  # "ONTZ", little-endian compressed resource wrapper
OFNT_VERSION = 1
OFNTZ_VERSION = 1
OFNT_FLAG_U16_CODEPOINTS = 0x00000001
OFNT_FLAG_U32_CODEPOINTS = 0x00000002
OFNTZ_FLAG_RLE8 = 0x00000001

FONT_CANDIDATE_HINTS = (
    "noto", "cjk", "sourcehan", "source-han", "wqy", "wenquanyi", "droid",
    "uming", "ukai", "arphic", "simhei", "simsun", "msyh", "yahei",
)
FONT_SEARCH_DIRS = (
    "/usr/share/fonts",
    "/usr/local/share/fonts",
    str(Path.home() / ".fonts"),
    "C:/Windows/Fonts",
    "/mnt/c/Windows/Fonts",
)


def is_cjk_resource_codepoint(cp: int) -> bool:
    return (
        (0x3400 <= cp <= 0x4DBF)
        or (0x4E00 <= cp <= 0x9FFF)
        or (0xF900 <= cp <= 0xFAFF)
        or (0x20000 <= cp <= 0x2FA1F)
        or (0x3000 <= cp <= 0x303F)
        or (0xFF00 <= cp <= 0xFFEF)
    )


def collect_codepoints(paths: Sequence[Path]) -> List[int]:
    text = ""
    for path in paths:
        if path.exists():
            text += path.read_text(encoding="utf-8")
    cps = {ord(ch) for ch in text if is_cjk_resource_codepoint(ord(ch))}
    return sorted(cps)


def gb2312_codepoints() -> List[int]:
    cps = set()
    for high in range(0xA1, 0xF8):
        for low in range(0xA1, 0xFF):
            try:
                text = bytes((high, low)).decode("gb2312")
            except UnicodeDecodeError:
                continue
            for ch in text:
                cp = ord(ch)
                if is_cjk_resource_codepoint(cp):
                    cps.add(cp)
    return sorted(cps)


def coverage_codepoints(name: str) -> List[int]:
    if name == "ui":
        return []
    if name == "gb2312":
        return gb2312_codepoints()
    if name == "cjk-basic":
        return list(range(0x4E00, 0xA000))
    if name == "cjk-all":
        cps: List[int] = []
        cps.extend(range(0x3400, 0x4DC0))
        cps.extend(range(0x4E00, 0xA000))
        cps.extend(range(0xF900, 0xFB00))
        cps.extend(range(0x20000, 0x2FA20))
        return cps
    raise ValueError(f"unknown coverage: {name}")


def parse_codepoint_list(value: str) -> List[int]:
    out = []
    for part in re.split(r"[\s,;]+", value.strip()):
        if not part:
            continue
        if part.startswith(("U+", "u+")):
            out.append(int(part[2:], 16))
        elif part.startswith(("0x", "0X")):
            out.append(int(part, 16))
        elif len(part) == 1:
            out.append(ord(part))
        elif any(ord(ch) > 0x7F for ch in part):
            out.extend(ord(ch) for ch in part if is_cjk_resource_codepoint(ord(ch)))
        else:
            out.append(int(part, 16))
    return sorted(set(out))


def find_cjk_font(explicit: Path | None = None) -> Path | None:
    if explicit:
        return explicit if explicit.exists() else None
    env_font = os.environ.get("OPENOS_CJK_FONT")
    if env_font:
        p = Path(env_font)
        if p.exists():
            return p
    candidates: List[Path] = []
    for root_text in FONT_SEARCH_DIRS:
        root = Path(root_text)
        if not root.exists():
            continue
        for path in root.rglob("*"):
            if path.suffix.lower() not in (".ttf", ".ttc", ".otf"):
                continue
            low = path.as_posix().lower()
            score = 0
            for i, hint in enumerate(FONT_CANDIDATE_HINTS):
                if hint in low:
                    score += 100 - i
            if score:
                candidates.append(path)
    if not candidates:
        return None
    candidates.sort(key=lambda p: p.as_posix().lower())
    candidates.sort(key=lambda p: -sum((100 - i) for i, h in enumerate(FONT_CANDIDATE_HINTS) if h in p.as_posix().lower()))
    return candidates[0]


def render_with_pillow(font_path: Path, codepoints: Iterable[int]) -> List[Tuple[int, List[int]]]:
    try:
        from PIL import Image, ImageDraw, ImageFont
    except Exception as exc:  # pragma: no cover - environment dependent
        raise RuntimeError(
            "Pillow is required to regenerate CJK glyphs. "
            "Install python3-pil or use --from-c for the checked-in fallback."
        ) from exc

    font = ImageFont.truetype(str(font_path), 16)
    glyphs: List[Tuple[int, List[int]]] = []
    for cp in codepoints:
        ch = chr(cp)
        img = Image.new("L", (WIDTH * 3, HEIGHT * 3), 0)
        draw = ImageDraw.Draw(img)
        try:
            bbox = draw.textbbox((0, 0), ch, font=font)
        except AttributeError:  # Pillow < 8
            w, h = draw.textsize(ch, font=font)
            bbox = (0, 0, w, h)
        gw = max(1, bbox[2] - bbox[0])
        gh = max(1, bbox[3] - bbox[1])
        x = (WIDTH - gw) // 2 - bbox[0]
        y = (HEIGHT - gh) // 2 - bbox[1]
        draw.text((x, y), ch, fill=255, font=font)
        rows: List[int] = []
        for y0 in range(HEIGHT):
            bits = 0
            for x0 in range(WIDTH):
                if img.getpixel((x0, y0)) >= 96:
                    bits |= 0x8000 >> x0
            rows.append(bits)
        glyphs.append((cp, rows))
    return glyphs


def write_c(out_path: Path, font_path: Path, glyphs: Sequence[Tuple[int, Sequence[int]]]) -> None:
    lines = []
    lines.append("/* Auto-generated by scripts/generate_cjk_font.py. Do not edit by hand. */")
    lines.append("#include \"font.h\"")
    lines.append("#include \"generated/cjk_font.h\"")
    lines.append("")
    lines.append("const font_cjk_glyph_t g_generated_cjk_glyphs[] = {")
    for cp, rows in glyphs:
        row_text = ",".join(f"0x{row:04x}u" for row in rows)
        lines.append(f"    {{ 0x{cp:04X}u, {{ {row_text} }} }},")
    lines.append("};")
    lines.append("")
    lines.append("const uint32_t g_generated_cjk_glyph_count =")
    lines.append("    (uint32_t)(sizeof(g_generated_cjk_glyphs) / sizeof(g_generated_cjk_glyphs[0]));")
    lines.append("")
    lines.append(f"const char g_generated_cjk_font_source[] = \"{font_path.as_posix()}\";")
    lines.append("")
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text("\n".join(lines), encoding="utf-8")


def build_ofnt_bytes(glyphs: Sequence[Tuple[int, Sequence[int]]]) -> bytes:
    if not glyphs:
        raise ValueError("cannot write empty CJK resource")
    glyphs = sorted(glyphs, key=lambda item: item[0])
    max_cp = max(cp for cp, _rows in glyphs)
    use_u16 = max_cp <= 0xFFFF
    flags = OFNT_FLAG_U16_CODEPOINTS if use_u16 else OFNT_FLAG_U32_CODEPOINTS
    header_size = 40
    codepoint_offset = header_size
    codepoint_data = bytearray()
    for cp, _rows in glyphs:
        codepoint_data += struct.pack("<H" if use_u16 else "<I", cp)
    bitmap_offset = codepoint_offset + len(codepoint_data)
    bitmap_data = bytearray()
    for _cp, rows in glyphs:
        if len(rows) != HEIGHT:
            raise ValueError("invalid glyph row count")
        for row in rows:
            bitmap_data += struct.pack("<H", row & 0xFFFF)
    header = struct.pack(
        "<IIIIIIIIII",
        OFNT_MAGIC,
        OFNT_VERSION,
        flags,
        len(glyphs),
        WIDTH,
        HEIGHT,
        codepoint_offset,
        bitmap_offset,
        HEIGHT * 2,
        0,
    )
    return bytes(header + codepoint_data + bitmap_data)


def write_ofnt(out_path: Path, glyphs: Sequence[Tuple[int, Sequence[int]]]) -> None:
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_bytes(build_ofnt_bytes(glyphs))


def rle8_compress(data: bytes) -> bytes:
    out = bytearray()
    i = 0
    while i < len(data):
        run = 1
        while i + run < len(data) and run < 128 and data[i + run] == data[i]:
            run += 1
        if run >= 3:
            out.append(0x80 | (run - 1))
            out.append(data[i])
            i += run
            continue
        start = i
        i += run
        while i < len(data):
            run = 1
            while i + run < len(data) and run < 128 and data[i + run] == data[i]:
                run += 1
            if run >= 3 or i - start + run > 128:
                break
            i += run
        literal_len = i - start
        out.append(literal_len - 1)
        out.extend(data[start:i])
    return bytes(out)


def write_ofntz(out_path: Path, glyphs: Sequence[Tuple[int, Sequence[int]]]) -> None:
    raw = build_ofnt_bytes(glyphs)
    compressed = rle8_compress(raw)
    header = struct.pack(
        "<IIIIIIII",
        OFNTZ_MAGIC,
        OFNTZ_VERSION,
        OFNTZ_FLAG_RLE8,
        len(raw),
        len(compressed),
        OFNT_MAGIC,
        32,
        0,
    )
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_bytes(header + compressed)


def write_resource(out_path: Path, glyphs: Sequence[Tuple[int, Sequence[int]]], compress: bool = False) -> None:
    if compress or out_path.suffix.lower() == ".ofntz":
        write_ofntz(out_path, glyphs)
    else:
        write_ofnt(out_path, glyphs)


def parse_generated_c_glyphs(path: Path) -> List[Tuple[int, List[int]]]:
    text = path.read_text(encoding="utf-8")
    pattern = re.compile(r"\{\s*0x([0-9A-Fa-f]+)u?\s*,\s*\{([^}]*)\}\s*\}")
    glyphs: List[Tuple[int, List[int]]] = []
    for match in pattern.finditer(text):
        cp = int(match.group(1), 16)
        rows = [int(value, 16) for value in re.findall(r"0x([0-9A-Fa-f]+)u?", match.group(2))]
        if len(rows) != HEIGHT:
            raise ValueError(f"invalid row count for U+{cp:04X}: {len(rows)}")
        glyphs.append((cp, rows))
    if not glyphs:
        raise ValueError(f"no generated glyphs found in {path}")
    return sorted(glyphs, key=lambda item: item[0])


def main(argv: Sequence[str]) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--font", type=Path, help="TTF/OTF/TTC font path; defaults to OPENOS_CJK_FONT or auto-discovery")
    parser.add_argument("--out", type=Path, help="output fallback C source path")
    parser.add_argument("--from-c", type=Path, help="export resource from an existing generated cjk_font.c")
    parser.add_argument("--resource-out", type=Path, help="optional external .ofnt/.ofntz resource path")
    parser.add_argument("--compress", action="store_true", help="write compressed .ofntz resource")
    parser.add_argument(
        "--coverage",
        choices=("ui", "gb2312", "cjk-basic", "cjk-all"),
        default="ui",
        help="additional glyph coverage for --resource-out; fallback C output stays scan/extra based",
    )
    parser.add_argument("--scan", type=Path, action="append", default=[], help="UTF-8 file to scan for UI fallback glyphs")
    parser.add_argument("--chars", default="", help="extra characters / U+xxxx / hex codepoints")
    args = parser.parse_args(argv)

    if args.from_c:
        if not args.resource_out:
            raise SystemExit("--from-c requires --resource-out")
        glyphs = parse_generated_c_glyphs(args.from_c)
        write_resource(args.resource_out, glyphs, args.compress)
        print(f"exported resource {len(glyphs)} glyphs from {args.from_c} -> {args.resource_out}")
        return 0

    font_path = find_cjk_font(args.font)
    if not font_path:
        raise SystemExit(
            "no CJK font found; pass --font or set OPENOS_CJK_FONT to a TTF/OTF/TTC Chinese font"
        )

    fallback_cps = set(collect_codepoints(args.scan))
    if args.chars:
        fallback_cps.update(parse_codepoint_list(args.chars))

    if args.out:
        if not fallback_cps:
            raise SystemExit("no CJK codepoints found for fallback C output")
        fallback_glyphs = render_with_pillow(font_path, sorted(fallback_cps))
        write_c(args.out, font_path, fallback_glyphs)
        print(f"generated fallback {len(fallback_glyphs)} glyphs -> {args.out}")

    if args.resource_out:
        resource_cps = set(fallback_cps)
        resource_cps.update(coverage_codepoints(args.coverage))
        if not resource_cps:
            raise SystemExit("no CJK codepoints found for resource output")
        resource_glyphs = render_with_pillow(font_path, sorted(resource_cps))
        write_resource(args.resource_out, resource_glyphs, args.compress)
        mode = "compressed " if args.compress or args.resource_out.suffix.lower() == ".ofntz" else ""
        print(f"generated {mode}resource {len(resource_glyphs)} glyphs using {font_path} -> {args.resource_out}")

    if not args.out and not args.resource_out:
        raise SystemExit("nothing to do: pass --out and/or --resource-out")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
