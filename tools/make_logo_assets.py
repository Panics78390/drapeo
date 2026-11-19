"""Génère les ressources du logo Drapeo à partir de assets/logo/drapeo-logo.png :
  - src/app/drapeo.ico      icône de l'exécutable Windows (16 à 256 px)
  - src/app/LogoData.h      images RGBA intégrées au programme (icône de fenêtre,
                            logo de la barre de menus, logo complet « À propos »)
Usage : python3 tools/make_logo_assets.py   (depuis la racine du projet)
"""
from PIL import Image, ImageDraw

SRC = "assets/logo/drapeo-logo.png"
BG = (15, 15, 15)

full = Image.open(SRC).convert("RGB")
work = full.copy()
d = ImageDraw.Draw(work)
d.rectangle((300, 445, 395, 500), fill=BG)   # flèche gauche
d.rectangle((915, 445, 1000, 505), fill=BG)  # flèche droite
crop = work.crop((300, 40, 960, 790))
w, h = crop.size
s = max(w, h)
square = Image.new("RGB", (s, s), BG)
square.paste(crop, ((s - w) // 2, (s - h) // 2))

def icon(size, rounded=True):
    img = square.resize((size, size), Image.LANCZOS).convert("RGBA")
    if rounded and size >= 32:
        mask = Image.new("L", (size * 4, size * 4), 0)
        ImageDraw.Draw(mask).rounded_rectangle((0, 0, size * 4 - 1, size * 4 - 1), radius=size * 4 // 5, fill=255)
        img.putalpha(mask.resize((size, size), Image.LANCZOS))
    return img

icon(256).save("src/app/drapeo.ico", sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
icon(256).save("assets/logo/drapeo-icon.png")

def emit(out, name, img):
    img = img.convert("RGBA")
    data = img.tobytes()
    out.write(f"inline constexpr int {name}Width = {img.width};\n")
    out.write(f"inline constexpr int {name}Height = {img.height};\n")
    out.write(f"inline constexpr unsigned char {name}Pixels[] = {{\n")
    for i in range(0, len(data), 24):
        out.write(",".join(str(b) for b in data[i:i + 24]) + ",\n")
    out.write("};\n\n")

with open("src/app/LogoData.h", "w") as out:
    out.write("// Généré par tools/make_logo_assets.py - ne pas modifier à la main.\n")
    out.write("// Logo Drapeo en RGBA 8 bits, ligne par ligne depuis le haut.\n#pragma once\n\nnamespace drapeo::logo {\n\n")
    for size in (16, 32, 48, 64):
        emit(out, f"kIcon{size}", icon(size))
    emit(out, "kFull", full.resize((360, 360), Image.LANCZOS))
    out.write("} // namespace drapeo::logo\n")
print("ok")
