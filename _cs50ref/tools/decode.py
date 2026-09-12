#!/usr/bin/env python3
"""Turn an exported animation into pet_frame_t tables.

The animations are drawn as segment art on a faithful F-91W template, rotated
90 degrees clockwise for the sideways face. Rotating back counter-clockwise
puts the watch in its native layout, where every cell sits in a known place --
so segments can be located geometrically rather than guessed at, and each frame
falls out as a set of bitmasks.

Run decode.sh rather than this directly; it does the ffmpeg work first.

    decode.sh happy.gif            # prints the C table
    decode.sh happy.gif --check    # ... and the tied-segment report

Assumes the canvas matches the export the calibration was built from: 480x480,
template in the same place. If the art shifts, CELLS below needs redoing --
segmap.c prints the component boxes to rebuild it from.
"""
import sys
import collections

W = H = 480
NPX = W * H
DARK = 128              # below this counts as a lit segment

# Cell boxes (x0, x1, y0, y1) in native orientation, derived by clustering the
# components segmap.c finds. The top row is 0-3 and the main line is 4-9.
CELLS = {
    0: (185, 218, 168, 215),   1: (225, 258, 168, 215),
    2: (330, 358, 168, 215),   3: (360, 398, 168, 215),
    4: ( 78, 122, 225, 310),   5: (134, 186, 225, 310),
    6: (206, 258, 225, 310),   7: (263, 313, 225, 310),
    8: (318, 357, 225, 310),   9: (359, 398, 225, 310),
}
COLON = (186, 205, 225, 310)

BIT = dict(A=0, B=1, C=2, D=3, E=4, F=5, G=6, H=7)

# Segments sharing one electrical address on the classic LCD. They light
# together whether the art wants it or not, so a frame that lights one half
# alone cannot be rendered as drawn.
TIES = [("1B", "1C"), ("1E", "1F"), ("2A", "2D"), ("2A", "2G"),
        ("4A", "4D"), ("6A", "6D")]


def load_components(path):
    """Read segmap.c's component table."""
    comps = []
    for line in open(path):
        if line.startswith('#'):
            continue
        p = line.split()
        if len(p) < 10:
            continue
        comps.append(dict(id=int(p[0]), x0=int(p[1]), y0=int(p[2]),
                          x1=int(p[3]), y1=int(p[4]), w=int(p[5]), h=int(p[6]),
                          area=int(p[7]), cx=int(p[8]), cy=int(p[9])))
    return comps


def assign(comps):
    """Map each component to (position, segment letter) by where it sits.

    Within a cell the horizontals are A, G, D top to bottom, and the verticals
    are F/E on the left, B/C on the right, H up the middle. H comes back as two
    components rather than one, because G crosses and splits it.
    """
    cells = collections.defaultdict(list)
    colon = []
    for c in comps:
        if COLON[0] <= c['cx'] <= COLON[1] and COLON[2] <= c['cy'] <= COLON[3]:
            colon.append(c)
            continue
        for pos, (x0, x1, y0, y1) in CELLS.items():
            if x0 <= c['cx'] <= x1 and y0 <= c['cy'] <= y1:
                cells[pos].append(c)
                break

    out = {}
    for pos, cs in cells.items():
        # The cell's own extent, from the segments actually present in the art.
        X0 = min(c['x0'] for c in cs); X1 = max(c['x1'] for c in cs)
        Y0 = min(c['y0'] for c in cs); Y1 = max(c['y1'] for c in cs)
        midy = (Y0 + Y1) / 2
        for c in cs:
            if c['w'] > c['h']:
                letter = ('A' if c['cy'] < Y0 + (Y1 - Y0) * 0.3 else
                          'D' if c['cy'] > Y0 + (Y1 - Y0) * 0.7 else 'G')
            else:
                third = (X1 - X0) / 3
                if c['cx'] < X0 + third:
                    side = 'left'
                elif c['cx'] > X1 - third:
                    side = 'right'
                else:
                    side = 'mid'
                upper = c['cy'] < midy
                letter = ('H' if side == 'mid' else
                          ('F' if upper else 'E') if side == 'left' else
                          ('B' if upper else 'C'))
            out.setdefault((pos, letter), []).append(c)
    return out, colon


def sample_points(c):
    """Interior points of a component, biased to its middle."""
    pts = [(c['cx'], c['cy'])]
    for f in (0.35, 0.65):
        pts.append((int(c['x0'] + (c['x1'] - c['x0']) * f),
                    int(c['y0'] + (c['y1'] - c['y0']) * f)))
    return pts


def decode(raw_path, mapping, colon, nframes):
    """Read the raw frames and resolve each to (seg[10], colon_on)."""
    frames = []
    with open(raw_path, 'rb') as raw:
        for _ in range(nframes):
            buf = raw.read(NPX)
            if len(buf) < NPX:
                break
            seg = [0] * 10
            for (pos, letter), cs in mapping.items():
                if any(buf[y * W + x] < DARK
                       for c in cs for (x, y) in sample_points(c)):
                    seg[pos] |= 1 << BIT[letter]
            colon_on = any(buf[y * W + x] < DARK
                           for c in colon for (x, y) in sample_points(c))
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


def validate(frames):
    """Report any frame that lights one half of a tied pair on its own."""
    def on(seg, spec):
        return bool(seg[int(spec[0])] & (1 << BIT[spec[1]]))
    bad = collections.Counter()
    for seg, _ in frames:
        for a, b in TIES:
            if on(seg, a) != on(seg, b):
                bad[(a, b)] += 1
    print("\n// tied-segment check (%d frames):" % len(frames))
    for a, b in TIES:
        n = bad[(a, b)]
        print("//   %s == %s   %s" %
              (a, b, "ok" if n == 0 else "MISMATCH in %d frames" % n))
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


def main():
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    comp_path, raw_path, nframes = sys.argv[1], sys.argv[2], int(sys.argv[3])
    name = sys.argv[4] if len(sys.argv) > 4 else "anim"
    check = "--check" in sys.argv

    comps = load_components(comp_path)
    mapping, colon = assign(comps)
    print("// calibration: %d components -> %d segments, %d colon dots"
          % (len(comps), len(mapping), len(colon)), file=sys.stderr)

    frames = decode(raw_path, mapping, colon, nframes)
    emit_c(run_length(frames), name)
    if check:
        validate(frames)


if __name__ == '__main__':
    main()
