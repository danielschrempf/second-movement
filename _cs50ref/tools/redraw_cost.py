#!/usr/bin/env python3
"""How much display work a redraw actually is.

`_pet_draw` keeps a shadow of what it last wrote to the LCD and pushes only the
cells that changed, because repainting one cell costs up to sixteen SLCD
register read-modify-writes and a frame of animation usually moves one cell out
of ten. This replays every animation out of pet_face.c and counts what that
saves, so the claim in MANUAL.md section 16 is a measurement rather than a
guess.

    python3 redraw_cost.py [path/to/pet_face.c]

Nothing here can fail; it prints numbers. Re-run it when the art changes or the
compositor does.
"""
import os
import re
import sys

import check_sounds as cs

# Cells the character layer owns, from _pet_layers. Anything outside this is
# masked off before it reaches the LCD, so it must not be counted.
CHARACTER = [0x00, 0xFF, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF]


def read_frame_flags(src):
    """{name: [flags per pose]} -- the colon and the indicators.

    check_sounds.read_frames drops the flags field, and the blink lives in it:
    the eyes are the colon, which is a flag rather than a cell, so counting only
    cells would say the resting animations never change at all.
    """
    out = {}
    for m in re.finditer(r"static const pet_frame_t _pet_frames_(\w+)\[\]\s*=\s*\{(.*?)\n\};",
                         src, re.S):
        poses = [0 if row.group(2).strip() == "0" else 1
                 for row in re.finditer(
                     r"\{\s*\{(.*?)\}\s*,\s*([^,]+?)\s*,\s*(\d+|PET_ANIM_HZ)\s*\}",
                     m.group(2), re.S)]
        if poses:
            out[m.group(1)] = poses
    return out


def transitions(poses, flags, loops):
    """Per frame change: how many of the ten cells move, and does a flag move."""
    seq = list(range(len(poses))) + ([0] if loops else [])
    cells = flagged = 0
    for i, j in zip(seq, seq[1:]):
        a, b = poses[i][0], poses[j][0]
        cells += sum(1 for p in range(10)
                     if (a[p] & CHARACTER[p]) != (b[p] & CHARACTER[p]))
        if flags[i] != flags[j]:
            flagged += 1
    return len(seq) - 1, cells, flagged


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else cs.DEFAULT
    src = open(path, encoding="utf-8", errors="replace").read()
    frames = cs.read_frames(src)
    flags = read_frame_flags(src)
    anims = cs.read_anims(src)

    print("cells repainted per redraw, before and after the shadow buffer\n")
    print("%-12s %6s %6s %8s %10s %10s" % (
        "animation", "poses", "ticks", "redraws", "before", "after"))
    print("-" * 56)

    total_redraws = total_cells = 0
    for anim, fname, loops, _cues in anims:
        if not fname or fname not in frames:
            continue
        poses = frames[fname]
        redraws, cells, _fl = transitions(poses, flags[fname], loops)
        if redraws == 0:
            continue
        total_redraws += redraws
        total_cells += cells
        print("%-12s %6d %6d %8d %10.1f %10.2f" % (
            anim.lower(), len(poses), sum(h for _, h in poses),
            redraws, 10.0, cells / float(redraws)))

    print("-" * 56)
    print("%-12s %6s %6s %8d %10.1f %10.2f   %.0f%% fewer" % (
        "TOTAL", "", "", total_redraws, 10.0,
        total_cells / float(total_redraws),
        100.0 * (1 - total_cells / float(total_redraws * 10))))

    # The resting loops are what the face actually spends its time in, so they
    # are the number that matters. The blink is a flag, not a cell.
    print("\nresting: what the LCD sees per second while the pet just sits there\n")
    print("%-12s %8s %14s %14s %12s" % (
        "mood", "loop s", "before /s", "after cells/s", "flags/s"))
    print("-" * 56)
    for mood in ("HAPPY", "CONFUSED", "UPSET", "ANGRY", "DEAD"):
        row = [a for a in anims if a[0] == mood][0]
        poses = frames[row[1]]
        redraws, cells, flagged = transitions(poses, flags[row[1]], True)
        secs = sum(h for _, h in poses) / float(cs.HZ)
        print("%-12s %8.2f %14.1f %14.1f %12.1f" % (
            mood.lower(), secs, redraws * 10 / secs, cells / secs, flagged / secs))
    return 0


if __name__ == "__main__":
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    sys.exit(main())
