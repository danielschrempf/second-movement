#!/usr/bin/env python3
"""Checks that every sound cue describes a moment the art actually reaches.

Sounds are attached to animations as cues -- "when cell 9's bottom edge lights"
rather than "1.125 seconds in" -- so redrawing an animation moves the sound with
it instead of leaving it stranded. That removes the drift problem, but it
introduces a quieter one: a cue whose condition never comes true simply never
fires, and silence is hard to notice.

So this reads the cue tables, the animation tables and the sound sequences
straight out of pet_face.c -- no copied constants -- replays each animation, and
reports where every cue lands.

    python3 check_sounds.py [path/to/pet_face.c]

Exits non-zero if a cue is unreachable, points at a missing sound, fires faster
than its own sound can play, or lands on the same tick as another cue.
"""
import re
import sys
import os

HZ = 8                  # PET_ANIM_HZ
PER_TICK = 64 // HZ     # sound duration units per animation tick
SEGS = "ABCDEFGH"

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT = os.path.join(HERE, "..", "..", "watch-faces", "complication", "pet_face.c")


# ---- reading pet_face.c -----------------------------------------------------

def seg_value(text):
    v = 0
    for letter in re.findall(r"SEG_([A-H])\b", text):
        v |= 1 << SEGS.index(letter)
    for hexval in re.findall(r"0[xX]([0-9a-fA-F]+)", text):
        v |= int(hexval, 16)
    return v


def read_frames(src):
    """{name: [(seg[10], hold)]}"""
    out = {}
    for m in re.finditer(r"static const pet_frame_t _pet_frames_(\w+)\[\]\s*=\s*\{(.*?)\n\};",
                         src, re.S):
        poses = []
        # The hold is usually a literal, but a single-frame table that just sits
        # there writes PET_ANIM_HZ. Missing that meant the pile and the puddle
        # parsed as having no frames at all, and went unchecked.
        for row in re.finditer(r"\{\s*\{(.*?)\}\s*,\s*([^,]+?)\s*,\s*(\d+|PET_ANIM_HZ)\s*\}",
                               m.group(2), re.S):
            seg = [seg_value(c) for c in row.group(1).split(",")]
            if len(seg) == 10:
                hold = HZ if row.group(3) == "PET_ANIM_HZ" else int(row.group(3))
                poses.append((seg, hold))
        if poses:
            out[m.group(1)] = poses
    return out


def read_cues(src):
    """{name: [(sound, position, mask, on_clear)]}"""
    out = {}
    for m in re.finditer(r"static const pet_cue_t _pet_cues_(\w+)\[\]\s*=\s*\{(.*?)\n\};",
                         src, re.S):
        body = re.sub(r"//[^\n]*", "", m.group(2))
        cues = []
        for row in re.finditer(r"\{\s*(PET_SOUND_\w+)\s*,\s*(\d+)\s*,\s*([^,]+?)\s*,"
                               r"\s*(true|false)\s*,\s*(true|false)\s*\}", body):
            cues.append((row.group(1), int(row.group(2)), seg_value(row.group(3)),
                         row.group(4) == "true", row.group(5) == "true"))
        out[m.group(1)] = cues
    return out


def read_sounds(src):
    """{PET_SOUND_X: total duration in 1/64 s}, repeats expanded."""
    lengths = {}
    for m in re.finditer(r"static int8_t _pet_sound_(\w+)\[\]\s*=\s*\{(.*?)\n?\};", src, re.S):
        body = re.sub(r"//[^\n]*", "", m.group(2))
        toks = [t.strip() for t in body.split(",") if t.strip()]
        seq, i = [], 0
        while i < len(toks) - 1:
            if toks[i] == "0":
                break
            if toks[i].startswith("-"):
                back, count = int(toks[i][1:]), int(toks[i + 1])
                seq += seq[len(seq) - back:] * count
            else:
                seq.append((toks[i], int(toks[i + 1])))
            i += 2
        lengths["_pet_sound_" + m.group(1)] = sum(d for _, d in seq)
    # map the enum name onto the array via the dispatch table
    out = {}
    for row in re.finditer(r"\[(PET_SOUND_\w+)\]\s*=\s*(_pet_sound_\w+)", src):
        out[row.group(1)] = lengths.get(row.group(2))
    return out


def read_direct(src):
    """{PET_SOUND_X: [functions that play it]} for sounds played without a cue.

    Not every sound has art to attach to: a button that only moves a counter has
    no animation at all, and a looping one -- a mood, the tombstone -- would
    re-cue on every turn. Those are played straight from the code that causes
    them, and still have to be reachable.
    """
    heads = [(m.start(), m.group(1))
             for m in re.finditer(r"^static [\w ]+?\*?(_pet_\w+)\s*\(", src, re.M)]
    out = {}
    for m in re.finditer(r"_pet_play_sound\(\s*s\s*,(.*?)\);", src, re.S):
        where = "?"
        for at, name in heads:
            if at < m.start():
                where = name
        # The ternary in _pet_rest picks between two, so take every name in the
        # argument; the cue path passes a variable and contributes none.
        for sound in re.findall(r"PET_SOUND_\w+", m.group(1)):
            out.setdefault(sound, []).append(where)
    return out


def read_anims(src):
    """[(anim, frames_name, loops, cues_name)]"""
    table = src[src.index("static const pet_anim_t _pet_anims"):]
    table = table[:table.index("\n};")]
    # Start after the "= {", or the array's own [PET_ANIM_COUNT] dimension reads
    # as a row whose frames are the first PET_NO_FRAMES it can find.
    table = table[table.index("= {") + 3:]
    out = []
    for row in re.finditer(
            r"\[PET_ANIM_(\w+)\]\s*=\s*\{.*?(PET_NO_FRAMES|PET_FRAMES\((\w+)\))\s*,"
            r"\s*(true|false)\s*,\s*PET_LAYER_\w+\s*,\s*(PET_NO_CUES|PET_CUES\((\w+)\))",
            table, re.S):
        frames = row.group(3).replace("_pet_frames_", "") if row.group(3) else None
        cues = row.group(6).replace("_pet_cues_", "") if row.group(6) else None
        out.append((row.group(1), frames, row.group(4) == "true", cues))
    return out


# ---- replaying --------------------------------------------------------------

def fires(cue, poses, loops):
    """Ticks at which this cue fires over one pass, `once` honoured."""
    sound, pos, mask, on_clear, once = cue
    at, tick = [], 0
    prev = [0] * 10
    begun = True
    for seg, hold in poses:
        if mask == 0:
            if begun:
                at.append(tick)
        elif on_clear:
            if (prev[pos] & mask) and not (seg[pos] & mask):
                at.append(tick)
        else:
            if not (prev[pos] & mask) and (seg[pos] & mask):
                at.append(tick)
        prev, begun = seg, False
        tick += hold
    if loops and mask != 0:
        # the wrap back to frame 0 is a transition too
        first = poses[0][0]
        if on_clear:
            if (prev[pos] & mask) and not (first[pos] & mask):
                at.append(tick)
        elif not (prev[pos] & mask) and (first[pos] & mask):
            at.append(tick)
    return at[:1] if once else at


def conditions(cue, poses, loops):
    """Every tick the condition comes true, whether or not `once` suppresses it."""
    sound, pos, mask, on_clear, _ = cue
    return fires((sound, pos, mask, on_clear, False), poses, loops)


fails = []


def check(what, ok, detail=""):
    print("  %-56s %s" % (what, "ok" if ok else "FAIL"))
    if detail:
        print("      %s" % detail)
    if not ok:
        fails.append(what)


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else DEFAULT
    src = open(path, encoding="utf-8", errors="replace").read()
    frames, cues, sounds = read_frames(src), read_cues(src), read_sounds(src)
    anims = read_anims(src)
    direct = read_direct(src)

    print("sound cues against the art they are attached to\n")

    used = set()
    for anim, fname, loops, cname in anims:
        if not cname:
            continue
        if cname not in cues:
            check("%s: cue table exists" % anim, False, "no _pet_cues_%s" % cname)
            continue
        if not fname or fname not in frames:
            check("%s: has art to cue against" % anim, False)
            continue
        poses = frames[fname]
        total = sum(h for _, h in poses)
        for cue in cues[cname]:
            sound, pos, mask, on_clear, once = cue
            used.add(sound)
            label = sound.replace("PET_SOUND_", "").lower()
            where = ("as %s begins" % anim.lower() if mask == 0 else
                     "cell %d %s %s" % (pos,
                                        "".join(L for i, L in enumerate(SEGS) if mask >> i & 1)
                                        or "0x%02x" % mask,
                                        "clears" if on_clear else "lights"))
            at = fires(cue, poses, loops)
            raw = conditions(cue, poses, loops)
            note = "fires at tick(s) %s of %d" % (at, total)
            if once and len(raw) > len(at):
                note += "; condition also true at %s, suppressed by `once`" % raw[len(at):]
            check("%s: %s" % (label, where), bool(at),
                  note if at else "never fires -- this sound is silent")

            if sound not in sounds or sounds[sound] is None:
                check("%s: sound is defined" % label, False)
                continue
            # A cue that repeats faster than its own sound cuts itself off.
            if len(at) > 1:
                gap = min(b - a for a, b in zip(at, at[1:]))
                need = sounds[sound] / PER_TICK
                check("%s: repeats leave room for the sound" % label, gap >= need,
                      "%d fires, closest %d ticks apart, sound is %.1f ticks"
                      % (len(at), gap, need))

    print()
    # Cues that share an animation share one buzzer. Starting a sound aborts
    # whatever is playing -- watch_buzzer_play_sequence_with_volume calls
    # watch_buzzer_abort_sequence as its first act, and every pet sound carries
    # BUZZER_PRIORITY_BUTTON, so no pet sound ever loses to another. Cutting one
    # short is fine and deliberate: a sound only has to *start* on its visual
    # moment, and handing over mid-phrase is how a scene changes gear.
    #
    # Two cues on the SAME tick is not fine. _pet_fire_cues walks the table in
    # order within a single frame, so both fire in one tick and the earlier is
    # aborted before it has sounded at all. It does not play shortened; it
    # vanishes. Nothing about the art looks wrong when this happens, which is
    # why it is checked here rather than noticed on the wrist.
    for anim, fname, loops, cname in anims:
        if not cname or cname not in cues or not fname or fname not in frames:
            continue
        if len(cues[cname]) < 2:
            continue
        poses = frames[fname]
        total = sum(h for _, h in poses)

        timeline = []
        for cue in cues[cname]:
            label = cue[0].replace("PET_SOUND_", "").lower()
            for tick in fires(cue, poses, loops):
                timeline.append((tick, label, sounds.get(cue[0])))
        timeline.sort(key=lambda row: row[0])

        ticks = [row[0] for row in timeline]
        shared = sorted({t for t in ticks if ticks.count(t) > 1})

        # How long each sound holds the buzzer before the next cue takes it.
        clipped = []
        for i, (tick, label, dur) in enumerate(timeline):
            if i + 1 < len(timeline):
                room = timeline[i + 1][0] - tick
            elif loops:
                room = total - tick + timeline[0][0]
            else:
                continue            # a one-shot's last sound runs past the art
            if dur and room < dur / PER_TICK:
                clipped.append("%s gets %d of its %.1f ticks"
                               % (label, room, dur / PER_TICK))

        order = list(dict.fromkeys(label for _, label, _ in timeline))
        where = ", ".join("%s %s" % (label, [t for t, l, _ in timeline if l == label])
                          for label in order)
        if shared:
            detail = "%s -- two land on tick %s, and the earlier never sounds" % (where, shared)
        elif clipped:
            detail = "%s; %s, handing over on the beat" % (where, "; ".join(clipped))
        else:
            detail = where
        check("%s: no two cues share a tick" % anim.lower(), not shared, detail)

    print()
    # Every animation but NONE must have art. pet_face.c used to carry a name
    # string per animation and draw that when frames were missing, which cost
    # around 200 bytes of flash for a fallback that could never fire in a
    # finished build. This is the same guarantee, checked here instead.
    for anim, fname, _loops, _cname in anims:
        if anim == "NONE":
            continue
        check("%s: has art" % anim.lower(), bool(fname) and fname in frames,
              "" if fname else "PET_NO_FRAMES -- this animation draws nothing")

    print()
    # Sounds with no art to cue against. There is no frame to check them
    # concerning, but they still have to exist and still have to be played.
    for name in sorted(direct):
        used.add(name)
        label = name.replace("PET_SOUND_", "").lower()
        callers = ", ".join(sorted(set(direct[name])))
        check("%s: sound is defined" % label,
              name in sounds and sounds[name] is not None,
              "played directly by %s" % callers)

    print()
    # A sound nothing reaches is dead weight, and usually means a cue was
    # dropped.
    for name in sorted(sounds):
        if name not in used:
            check("%s is reachable" % name.replace("PET_SOUND_", "").lower(), False,
                  "defined but nothing cues it or plays it")

    print("\n%s" % ("FAILURES ABOVE" if fails else "all checks passed"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
