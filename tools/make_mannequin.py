#!/usr/bin/env python3
"""Génère assets/mannequin.obj : mannequin de couture simplifié (asset original de Drapeo).

Unités : centimètres (175 cm), pour tester la conversion d'unités à l'import.
Formes : buste par révolution à section elliptique, tête, bras en pose A, jambes.
"""
import math, os, sys

verts, faces = [], []

def add_grid(points_fn, nu, nv, closed_u=True, cap=True):
    base = len(verts)
    for j in range(nv + 1):
        for i in range(nu):
            verts.append(points_fn(i / nu, j / nv))
    for j in range(nv):
        for i in range(nu):
            i2 = (i + 1) % nu
            a, b = base + j * nu + i, base + j * nu + i2
            c, d = base + (j + 1) * nu + i, base + (j + 1) * nu + i2
            faces.append((a, c, d, b))
    if cap:  # bouchons (éventails) aux deux extrémités
        for ring, flip in ((0, False), (nv, True)):
            ids = [base + ring * nu + i for i in range(nu)]
            cx = sum(verts[k][0] for k in ids) / nu
            cy = sum(verts[k][1] for k in ids) / nu
            cz = sum(verts[k][2] for k in ids) / nu
            verts.append((cx, cy, cz)); c = len(verts) - 1
            for i in range(nu):
                a, b = ids[i], ids[(i + 1) % nu]
                faces.append((c, b, a) if flip else (c, a, b))

def lerp_profile(profile, t):
    y = profile[0][1] + t * (profile[-1][1] - profile[0][1])
    for (r0, y0), (r1, y1) in zip(profile, profile[1:]):
        if y0 <= y <= y1:
            k = (y - y0) / (y1 - y0) if y1 > y0 else 0
            return r0 + k * (r1 - r0), y
    return profile[-1]

# Buste : (rayon, hauteur) en cm, section elliptique (profondeur 70 %)
torso = [(13.0, 80), (17.0, 88), (17.5, 95), (15.5, 102), (13.5, 108), (14.8, 117),
         (17.0, 126), (17.5, 134), (16.5, 140), (11.0, 145), (6.0, 148), (5.5, 152)]
def torso_pt(u, v):
    r, y = lerp_profile(torso, v)
    a = 2 * math.pi * u
    return (r * math.cos(a), y, 0.7 * r * math.sin(a))
add_grid(torso_pt, 48, 40)

def ellipsoid(cx, cy, cz, rx, ry, rz):
    def pt(u, v):
        th, ph = 2 * math.pi * u, math.pi * (0.02 + 0.96 * v)
        return (cx + rx * math.sin(ph) * math.cos(th), cy - ry * math.cos(ph), cz + rz * math.sin(ph) * math.sin(th))
    add_grid(pt, 32, 20)

def capsule(p0, p1, r0, r1):
    ax = [p1[k] - p0[k] for k in range(3)]
    L = math.sqrt(sum(c * c for c in ax)); ax = [c / L for c in ax]
    tmp = (1, 0, 0) if abs(ax[0]) < 0.9 else (0, 0, 1)
    e1 = [ax[1]*tmp[2]-ax[2]*tmp[1], ax[2]*tmp[0]-ax[0]*tmp[2], ax[0]*tmp[1]-ax[1]*tmp[0]]
    n = math.sqrt(sum(c * c for c in e1)); e1 = [c / n for c in e1]
    e2 = [e1[1]*ax[2]-e1[2]*ax[1], e1[2]*ax[0]-e1[0]*ax[2], e1[0]*ax[1]-e1[1]*ax[0]]  # e1 x axe : normales vers l'extérieur
    def pt(u, v):
        a = 2 * math.pi * u
        # v : 0..1 le long de l'axe, extrémités arrondies
        if v < 0.2:   s, rr, off = -1, r0, (0.2 - v) / 0.2
        elif v > 0.8: s, rr, off = 1, r1, (v - 0.8) / 0.2
        else:         s, rr, off = 0, r0 + (r1 - r0) * (v - 0.2) / 0.6, 0
        t = (v - 0.2) / 0.6 if s == 0 else (0 if s < 0 else 1)
        ring = rr * math.cos(off * math.pi / 2)
        along = t * L + s * rr * math.sin(off * math.pi / 2)
        return tuple(p0[k] + ax[k] * along + ring * (math.cos(a) * e1[k] + math.sin(a) * e2[k]) for k in range(3))
    add_grid(pt, 20, 24)

ellipsoid(0, 163, 1, 8.5, 11, 9.5)                       # tête
for sx in (-1, 1):
    sh = (sx * 17.5, 139, 0)
    el = (sx * 33, 116, 0)
    wr = (sx * 46, 96, 3)
    capsule(sh, el, 5.0, 4.0)                            # bras
    capsule(el, wr, 4.0, 3.0)                            # avant-bras
    capsule((sx * 9, 88, 0), (sx * 10, 48, 0), 8.0, 5.5) # cuisse
    capsule((sx * 10, 48, 0), (sx * 10, 6, 0), 5.5, 4.0) # mollet

out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "assets", "mannequin.obj")
with open(out, "w") as f:
    f.write("# Drapeo - mannequin de test (cm). Genere par tools/make_mannequin.py\no mannequin\n")
    for v in verts: f.write("v %.3f %.3f %.3f\n" % v)
    for fc in faces: f.write("f " + " ".join(str(i + 1) for i in fc) + "\n")
print(out, len(verts), "sommets", len(faces), "faces")
