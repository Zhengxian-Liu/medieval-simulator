"""Thatch roofs: layered courses, ragged eaves, sagging slopes and a lumpy ridge cap. Course-staggered UVs keep the
texture period from lining up into bands (art review 2/3)."""
import math

import bmesh
from mathutils import Euler, Vector, noise

from kit import RNG, new_object


def stagger_courses(obj, course=0.28):
    """Each thatch course (a row of bundles laid along the ridge) gets its own texture offset and slight scale, so the
    texture period never lines up into diagonal bands across the roof (art review 2026-10-16)."""
    me = obj.data
    uv = me.uv_layers.active.data
    for poly in me.polygons:
        k = int(poly.center.y // course)
        b = int((poly.center.x + 20.0) // 0.5)  # 0.5 m bundles along the course, each its own offset
        # offsets only, no per-course scale: scaling v per course read as corduroy stripes (art review 2)
        du, dv = (k * 0.37 + b * 0.53) % 1.0, (k * 0.11 + b * 0.29) % 1.0
        for li in poly.loop_indices:
            u, v = uv[li].uv
            uv[li].uv = (u + du, v + dv)


def thatch_roof(W, D, TOP, PITCH_RISE):
    """Layered thatch over a W x D wall plate at height TOP, ridge PITCH_RISE above it. Silhouette rules: no straight
    line over 2 m (sag between gables, wavy ridge, ragged eave); courses staggered (stagger_courses)."""
    """Two slopes of layered thatch: courses step out every ~0.28 m, ragged eaves, plus a rounded ridge cap."""
    half = D / 2
    pitch = math.atan2(PITCH_RISE, half)
    over_eave, over_gable, thick = 0.55, 0.45, 0.4
    slope = math.hypot(half, PITCH_RISE) + over_eave
    length = W + 2 * over_gable
    nu, nv = 56, 26
    for side in (-1, 1):
        bm = bmesh.new()
        grid = []
        for j in range(nv + 1):
            row = []
            s = j / nv * slope                     # distance down-slope from the ridge
            course = (s / 0.28) % 1.0
            for i in range(nu + 1):
                x = -length / 2 + i / nu * length
                n = noise.noise(Vector((x * 1.7, s * 2.3, side * 5.0)))
                lift = thick * 0.55 + 0.03 * course + 0.05 * n  # shallow course steps: deep ones shaded into stripes
                # no straight line over 2 m: the whole roof sags between the gables (more toward the eave), with a
                # slow wave along the ridge, so ridge and eave silhouettes read as old, settled thatch
                along = (x + length / 2) / length
                sag = (0.05 + 0.13 * (j / nv)) * math.sin(math.pi * along) + 0.04 * math.sin(along * 7.0 + side)
                droop = sag
                if j == nv:                         # ragged eave
                    droop += RNG.uniform(0.0, 0.12)
                if i in (0, nu):                    # rounded gable edge
                    lift *= 0.55
                row.append(bm.verts.new((x, s + droop * 0.3, lift - droop)))
            grid.append(row)
        for j in range(nv):
            for i in range(nu):
                bm.faces.new([grid[j][i], grid[j + 1][i], grid[j + 1][i + 1], grid[j][i + 1]])
        # thickness fringe at the eave and both gable edges, so the roof reads as a thick mat
        for i in range(nu):
            a, b = grid[nv][i], grid[nv][i + 1]
            a2 = bm.verts.new(a.co + Vector((0, -0.05, -thick)))
            b2 = bm.verts.new(b.co + Vector((0, -0.05, -thick)))
            bm.faces.new([a, a2, b2, b])
        for i in (0, nu):
            for j in range(nv):
                a, b = grid[j][i], grid[j + 1][i]
                a2 = bm.verts.new(a.co + Vector((0, 0, -thick)))
                b2 = bm.verts.new(b.co + Vector((0, 0, -thick)))
                bm.faces.new([a, b, b2, a2] if i == 0 else [a, a2, b2, b])
        bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
        for f in bm.faces:  # all normals should face up/out
            if f.normal.z < -0.2:
                f.normal_flip()
        # local frame: x along ridge, y down-slope, z out of the roof; strands follow y (down-slope)
        rot = Euler((-pitch, 0, 0 if side > 0 else math.pi))  # tilt local +y down-slope, then face it to its side
        obj = new_object(f"thatch_{side}", bm, "thatch", grain_axis=1, location=(0, 0, TOP + PITCH_RISE + 0.05), rotation=rot)
        stagger_courses(obj)
    # ridge cap: a lumpy half-tube along the ridge
    bm = bmesh.new()
    seg, ring = 40, 10
    rows = []
    for i in range(seg + 1):
        x = -length / 2 - 0.05 + i / seg * (length + 0.1)
        row = []
        for k in range(ring + 1):
            a = math.pi * k / ring
            r = 0.32 + 0.04 * noise.noise(Vector((x * 2.0, k * 0.5, 9.0)))
            row.append(bm.verts.new((x, math.cos(a) * r * 1.2, math.sin(a) * r)))
        rows.append(row)
    for i in range(seg):
        for k in range(ring):
            bm.faces.new([rows[i][k], rows[i][k + 1], rows[i + 1][k + 1], rows[i + 1][k]])
    new_object("ridge", bm, "thatch", grain_axis=0, location=(0, 0, TOP + PITCH_RISE + thick * 0.6))
