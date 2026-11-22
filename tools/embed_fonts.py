#!/usr/bin/env python3
"""Régénère src/app/FontData.h depuis assets/fonts (Inter, sous-ensemble latin)."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "src" / "app" / "FontData.h"


def emit(name: str, path: Path) -> str:
    data = path.read_bytes()
    lines = [f"inline constexpr unsigned char {name}[] = {{"]
    row: list[str] = []
    for b in data:
        row.append(f"0x{b:02x}")
        if len(row) == 20:
            lines.append("    " + ", ".join(row) + ",")
            row = []
    if row:
        lines.append("    " + ", ".join(row) + ",")
    lines.append("};")
    lines.append(f"inline constexpr int {name}Size = {len(data)};")
    return "\n".join(lines)


regular = ROOT / "assets" / "fonts" / "Inter-Regular.ttf"
semibold = ROOT / "assets" / "fonts" / "Inter-SemiBold.ttf"
text = (
    "#pragma once\n"
    "// Généré par tools/embed_fonts.py — ne pas éditer à la main.\n"
    "// Inter, sous-ensemble latin, SIL Open Font License 1.1 (assets/fonts/LICENSE.txt).\n"
    "// Embarqué pour que l'exécutable reste autonome, y compris hors ligne.\n"
    "namespace drapeo::font {\n"
    + emit("kRegular", regular)
    + "\n\n"
    + emit("kSemiBold", semibold)
    + "\n} // namespace drapeo::font\n"
)
OUT.write_text(text)
print(f"{OUT} ({OUT.stat().st_size} octets)")
