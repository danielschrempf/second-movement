#!/usr/bin/env python3
"""Turn an exported animation into pet_frame_t tables.

The animations are drawn as segment art on a faithful F-91W template, rotated
90 degrees clockwise for the sideways face. Rotating back counter-clockwise
puts the watch in its native layout, where every cell sits in a known place --
so each segment can be read straight out of the pixels, and a frame falls out
as a set of bitmasks.

Run decode.sh rather than this directly; it does the ffmpeg work first.

    decode.sh happy.gif happy            # prints the C table
    decode.sh happy.gif happy --check    # ... plus the render check

Segments are located from the LCD's own geometry, not by looking for what moves
in the art. An earlier version grew the map out of connected components found
per file, which fails two ways: a segment held lit for a whole animation never
changes and so was invisible to it, and neighbouring segments that touch merge
into one blob and get a single label. Both failed silently, dropping parts of
the drawing. The cell boxes below are the only measured input; everything
inside a cell follows from the fixed seven-segment layout.

Assumes the canvas matches the export the boxes were measured from: 480x480,
template in the same place. If the art shifts, see README.md -- segmap.c prints
component boxes to re-measure CELLS from.
"""
import sys
import collections

W = H = 480
NPX = W * H
DARK = 128              # below this counts as a lit segment
COVERAGE = 0.5          # fraction of a segment's box that must be dark

# Cell boxes (x0, x1, y0, y1) in native orientation. The top row is 0-3; the
# main line is 4-9, with 8 and 9 the smaller seconds digits, sitting lower.
CELLS = {
    0: (185, 218, 168, 215),   1: (225, 247, 173, 208),
    2: (330, 358, 168, 215),   3: (360, 398, 168, 215),
    4: ( 84, 126, 229, 305),   5: (140, 181, 229, 305),
    6: (212, 253, 229, 305),   7: (267, 308, 229, 305),
    8: (322, 354, 248, 306),   9: (362, 394, 248, 306),
}
# The two colon dots, which carry the pet's eyes.
COLON = [(191, 199, 247, 256), (191, 199, 278, 287)]

BIT = dict(A=0, B=1, C=2, D=3, E=4, F=5, G=6, H=7)

# Only the two weekday cells carry the centre vertical H; 2-9 are plain
# seven-segment. Straight out of Classic_LCD_Display_Mapping in
# watch-library/shared/watch/watch_common_display.h.
HAS_H = {0, 1}

# Segments sharing one electrical address on the classic LCD, from that same
# table. They light together whether the art wants it or not, so a frame that
# lights one member alone cannot be rendered as drawn.
TIES = [("1B", "1C"), ("1E", "1F"), ("2A", "2D", "2G"),
        ("4A", "4D"), ("6A", "6D")]


def segment_boxes(pos):
    """Where each segment of a cell sits, as a fraction of the cell box.

    The horizontals A, G, D run across the top, middle and bottom, inset at
    each end where the verticals meet them; F/B and E/C are the upper and lower
    verticals on the left and right. H, where it exists, is the centre vertical
    that G crosses.
    """
    x0, x1, y0, y1 = CELLS[pos]
    w, h = x1 - x0, y1 - y0
    cx = (x0 + x1) // 2

    def box(ax, bx, ay, by):
        return (x0 + int(ax * w), x0 + int(bx * w),
                y0 + int(ay * h), y0 + int(by * h))

    boxes = {
        'A': box(.18, .82, .00, .12),
        'G': box(.18, .82, .44, .56),
        'D': box(.18, .82, .88, 1.0),
        'F': box(.00, .16, .06, .46),
        'B': box(.84, 1.0, .06, .46),
        'E': box(.00, .16, .54, .94),
        'C': box(.84, 1.0, .54, .94),
    }
    if pos in HAS_H:
        boxes['H'] = (cx - int(.08 * w), cx + int(.08 * w),
                      y0 + int(.06 * h), y0 + int(.94 * h))
    return boxes


BOXES = {pos: segment_boxes(pos) for pos in CELLS}


def coverage(buf, box):
    """Fraction of a box that is lit."""
    x0, x1, y0, y1 = box
    lit = total = 0
    for y in range(y0, y1 + 1):
        row = y * W
        for x in range(x0, x1 + 1):
            total += 1
            if buf[row + x] < DARK:
                lit += 1
    return lit / total if total else 0.0


def decode(raw_path, nframes):
    """Read the raw frames and resolve each to (seg[10], colon_on)."""
    frames = []
    with open(raw_path, 'rb') as raw:
        for _ in range(nframes):
            buf = raw.read(NPX)
            if len(buf) < NPX:
                break
            seg = [0] * 10
            for pos, boxes in BOXES.items():
                for letter, box in boxes.items():
                    if coverage(buf, box) > COVERAGE:
                        seg[pos] |= 1 << BIT[letter]
            colon_on = all(coverage(buf, c) > COVERAGE for c in COLON)
            frames.append((tuple(seg), colon_on))
    return frames


def run_length(frames):
    """Collapse runs of identical poses into (pose, hold) -- the frame format."""
    runs = []
    for f in frames:
        if runs and runs[-1][0] == f:
            runs[-1][1] += 1
        else:
            runs.append([f, 1])
    return runs


def blank(frame):
    seg, flags = frame
    return not any(seg) and not flags


def trim_blanks(frames, keep):
    """Drop blank frames from each end, reporting what went.

    A blank run at the end of an export is an artefact of the drawing program,
    not a pause the animation asked for: on a looping mood it reads as the pet
    vanishing for half a second every cycle. Interior blanks are left alone --
    those are deliberate, and they are how a flash or a fade-out is drawn.
    """
    lead = 0
    while lead < len(frames) and blank(frames[lead]):
        lead += 1
    tail = 0
    while tail < len(frames) - lead and blank(frames[len(frames) - 1 - tail]):
        tail += 1
    inner = sum(1 for f in frames[lead:len(frames) - tail] if blank(f))

    note = []
    if lead or tail:
        note.append("%d blank frame(s) at the ends" % (lead + tail)
                    + (" -- kept" if keep else " -- trimmed"))
    if inner:
        note.append("%d interior blank frame(s), kept" % inner)
    if note:
        print("// NOTE: " + "; ".join(note), file=sys.stderr)
    if keep or not (lead or tail):
        return frames
    return frames[lead:len(frames) - tail]


def validate(frames):
    """Report anything in the art the hardware cannot render as drawn."""
    def on(seg, spec):
        return bool(seg[int(spec[0])] & (1 << BIT[spec[1]]))

    bad = collections.Counter()
    for seg, _ in frames:
        for tie in TIES:
            states = [on(seg, s) for s in tie]
            if any(states) and not all(states):
                bad[tie] += 1
    print("\n// tied-segment check (%d frames):" % len(frames))
    for tie in TIES:
        n = bad[tie]
        print("//   %s   %s" % (" == ".join(tie),
                                "ok" if n == 0 else "MISMATCH in %d frames" % n))

    # Which cells the art actually touches, so a drawing that strays outside
    # its layer's region shows up here rather than being silently masked off.
    used = collections.defaultdict(set)
    for seg, _ in frames:
        for pos in range(10):
            for letter, bit in BIT.items():
                if seg[pos] & (1 << bit):
                    used[pos].add(letter)
    print("// cells used: %s" % ("  ".join(
        "%d:%s" % (p, "".join(sorted(used[p]))) for p in sorted(used)) or "none"))
    return sum(bad.values())


def emit_c(runs, name):
    """Print a pet_frame_t table ready to paste into pet_face.c."""
    def cell(v):
        if not v:
            return "SEG_NONE"
        return "|".join("SEG_" + "ABCDEFGH"[b] for b in range(8) if v & (1 << b))

    print("static const pet_frame_t _pet_frames_%s[] = {" % name)
    print("    //  0         1         2         3         4         5"
          "         6         7         8         9            flags            hold")
    for (seg, colon_on), hold in runs:
        cells = ", ".join("%-9s" % cell(seg[p]) for p in range(10))
        flags = "PET_FRAME_COLON" if colon_on else "0              "
        print("    { { %s }, %s, %2d }," % (cells, flags, min(hold, 255)))
    print("};")
    print("// %d frames -> %d poses" % (sum(h for _, h in runs), len(runs)))


def describe():
    """Print the geometry, for checking after the boxes are re-measured."""
    for pos in sorted(BOXES):
        print("// cell %d %s: %s" % (pos, CELLS[pos], " ".join(
            "%s(%d,%d,%d,%d)" % ((letter,) + box)
            for letter, box in sorted(BOXES[pos].items()))), file=sys.stderr)


def main():
    if len(sys.argv) > 1 and sys.argv[1] == "--describe":
        describe()
        return
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    raw_path, nframes = sys.argv[1], int(sys.argv[2])
    name = sys.argv[3] if len(sys.argv) > 3 else "anim"
    check = "--check" in sys.argv

    frames = trim_blanks(decode(raw_path, nframes), "--keep-blanks" in sys.argv)
    emit_c(run_length(frames), name)
    if check:
        validate(frames)


if __name__ == '__main__':
    main()
