#!/usr/bin/env python3
"""Proves the reel plays in order, and that you can always get back to the pet.

The showcase is a reel: it holds each animation on screen by forcing it to loop,
counts the passes, and moves to the next entry after PET_SHOWCASE_PLAYS of them.
Two things can go wrong, and neither goes wrong loudly.

Leaving is the first. Clearing `showcase_on` only stops that forcing, so a
one-shot ends and reaches `_pet_rest` on its own -- but a looping animation keeps
looping, and a status animation leaves the character layer on PET_ANIM_NONE,
which `_pet_layer_tick` skips entirely. Neither reaches `_pet_rest`, and the
animation on screen stays up for good.

Advancing is the second. The pass is counted at the one moment an animation ends,
which for a loop only exists because the showcase forces it -- so an entry whose
art never reaches that point would hold the reel there for ever.

So this replays the layer engine and asserts both: the reel visits every entry in
order, each for exactly its allotted passes, and from every position in it,
leaving by every route ends with the pet back on screen showing its real mood and
nothing on the floor it did not put there.

The animation table and the reel are parsed out of pet_face.c, so the art cannot
go stale, and so is the one behaviour under test: whether leaving hands the
screen back (`read_exit_rests`). The rest of the engine below --
`_pet_layer_tick`, `_pet_rest` -- is a transcription and IS a copy. If those
change, change them here too.

    python3 check_showcase.py
"""
import os
import re
import sys

import check_sounds

HZ = 8
CHARACTER, STATUS = 0, 1
PLAYS = 2                       # PET_SHOWCASE_PLAYS

MOODS = ("HAPPY", "CONFUSED", "UPSET", "ANGRY", "DEAD")


def read_exit_rests(src):
    """Does leaving the showcase hand the screen back to the live pet?

    This is the behaviour under test, so it is read from the source rather than
    assumed: the escape cases at the top of pet_face_loop must route through a
    helper that calls _pet_rest. Setting showcase_on = false inline does not
    count -- that is the bug this check exists to catch.

    The escape switch is the first of the two in pet_face_loop, so the second
    one bounds it. Matched loosely: brace style is not what this is testing, and
    a reformat that moves the brace to its own line should not read as a bug.
    """
    loop = src[src.index("bool pet_face_loop"):]
    switches = [m.start() for m in
                re.finditer(r"switch\s*\(\s*event\.event_type\s*\)\s*\{", loop)]
    if len(switches) < 2:
        raise SystemExit("check_showcase: expected two switches in "
                         "pet_face_loop, found %d -- has it been "
                         "restructured?" % len(switches))
    escape = loop[:switches[1]]
    if "_pet_showcase_exit" not in escape:
        return False
    helper = re.search(r"static void _pet_showcase_exit\(.*?\n\}", src, re.S)
    return bool(helper) and "_pet_rest" in helper.group(0)


def read_chord_exits(src):
    """Does the hug hand the screen back before it kisses?

    The hug is the one route out that is not in the escape switch -- the chord
    fires on a BUTTON_DOWN, well before either release reaches it -- so
    _pet_chord has to do it itself. Drop that call and the kiss plays underneath
    a running reel, which no other check here would see.
    """
    helper = re.search(r"static void _pet_chord\(.*?\n\}", src, re.S)
    if not helper:
        return False
    body = helper.group(0)
    return "_pet_showcase_exit" in body and "_pet_hug" in body


def read_reel(src):
    """The running order, out of _pet_showcase_reel."""
    table = src[src.index("static const uint8_t _pet_showcase_reel"):]
    return re.findall(r"PET_ANIM_(\w+)", table[:table.index("};")])


def read_plays(src):
    """PET_SHOWCASE_PLAYS, out of pet_face.h."""
    here = os.path.dirname(os.path.abspath(__file__))
    path = os.path.join(here, "..", "..", "watch-faces", "complication",
                        "pet_face.h")
    m = re.search(r"#define\s+PET_SHOWCASE_PLAYS\s+(\d+)",
                  open(path, encoding="utf-8").read())
    return int(m.group(1)) if m else PLAYS


def read_anims(src):
    """{anim: {frames, loop, layer}}, straight out of _pet_anims."""
    frames = check_sounds.read_frames(src)
    table = src[src.index("static const pet_anim_t _pet_anims"):]
    table = table[:table.index("\n};")]
    # Loosely, so the brace may sit on its own line: see read_exit_rests.
    table = table[re.search(r"=\s*\{", table).end():]
    out = {}
    for row in re.finditer(
            r"\[PET_ANIM_(\w+)\]\s*=\s*\{.*?(PET_NO_FRAMES|PET_FRAMES\((\w+)\))\s*,"
            r"\s*(true|false)\s*,\s*PET_LAYER_(\w+)\s*,", table, re.S):
        name = row.group(3).replace("_pet_frames_", "") if row.group(3) else None
        out[row.group(1)] = {
            "frames": frames.get(name) if name else None,
            "loop": row.group(4) == "true",
            "layer": CHARACTER if row.group(5) == "CHARACTER" else STATUS,
        }
    return out


class Pet:
    """_pet_layer_tick, _pet_rest and the showcase, transcribed."""

    def __init__(self, anims, reel, plays, mood, scene, has_poo=False,
                 has_barf=False, exit_rests=True):
        self.anims, self.reel, self.plays = anims, reel, plays
        self.exit_rests = exit_rests
        self.layer = [{"anim": "NONE", "idle": "NONE", "frame": 0, "hold": 0}
                      for _ in range(2)]
        self.mood, self.scene = mood, scene
        self.has_poo, self.has_barf, self.poo_pending = has_poo, has_barf, False
        self.showcase_on = False
        self.showcase_step, self.showcase_plays = 0, 0
        self.queue = []
        self.seen = []          # every entry the reel actually started, in order

    # -- playback ---------------------------------------------------------
    def hold_of(self, anim, frame):
        art = self.anims[anim]["frames"]
        return art[frame][1] if art else HZ

    def layer_play(self, layer, anim):
        L = self.layer[layer]
        L["anim"], L["frame"] = anim, 0
        L["hold"] = self.hold_of(anim, 0)

    def start_anim(self, anim):
        self.layer_play(self.anims[anim]["layer"], anim)

    def set_status(self):
        want = "NONE"
        if self.mood != "DEAD":
            if self.has_poo and not self.poo_pending:
                want = "PILE"
            elif self.has_barf:
                want = "PUDDLE"
        self.layer[STATUS]["idle"] = want
        self.layer_play(STATUS, want)

    def rest(self):
        self.queue = []
        self.set_status()
        if self.mood == "DEAD":
            self.scene = "DEAD"
            self.start_anim("DEAD")
            return
        if self.scene == "ASLEEP":
            self.start_anim("SNORE")
            return
        if self.scene not in ("FEEDING", "PLAYING", "NIGHT_AWAKE"):
            self.scene = "IDLE"
        self.start_anim(self.mood)

    def layer_tick(self, layer):
        L = self.layer[layer]
        if L["anim"] == "NONE":
            return
        anim = self.anims[L["anim"]]
        count = len(anim["frames"]) if anim["frames"] else 1
        loop_here = anim["loop"]
        queued = layer == CHARACTER and bool(self.queue)
        reeling = self.showcase_on and layer == CHARACTER
        if reeling:
            loop_here, queued = True, False
        if L["hold"] > 1:
            L["hold"] -= 1
            return
        L["frame"] += 1
        if L["frame"] < count:
            L["hold"] = self.hold_of(L["anim"], L["frame"])
            return
        if reeling:
            self.showcase_plays += 1
            if self.showcase_plays >= self.plays:
                self.showcase_advance()
                return
        if loop_here and not queued:
            L["frame"] = 0
            L["hold"] = self.hold_of(L["anim"], 0)
            return
        if queued:
            self.start_anim(self.queue.pop(0))
            return
        if layer == CHARACTER:
            self.rest()
            return
        self.layer_play(layer, L["idle"])

    def tick(self):
        for layer in (CHARACTER, STATUS):
            self.layer_tick(layer)

    # -- interactions -----------------------------------------------------
    def blocked(self):
        return self.scene in ("DEAD", "ASLEEP", "NIGHT_AWAKE")

    def showcase_play(self):
        self.showcase_on, self.showcase_plays = True, 0
        self.queue = []
        self.layer_play(CHARACTER, "NONE")
        self.layer_play(STATUS, "NONE")
        self.start_anim(self.reel[self.showcase_step])
        self.seen.append(self.reel[self.showcase_step])

    def showcase_advance(self):
        self.showcase_step = (self.showcase_step + 1) % len(self.reel)
        self.showcase_play()

    def showcase_exit(self):
        if not self.showcase_on:
            return
        self.showcase_on = False
        if self.exit_rests:
            self.rest()

    def showcase_toggle(self):
        if self.showcase_on:
            self.showcase_exit()
            return
        self.showcase_step = 0
        self.showcase_play()

    def escape(self, how):
        """The escape switch at the top of pet_face_loop, then the action."""
        if how == "chord":                                     # hug: both buttons
            # Not in the escape switch -- _pet_chord hands the screen back
            # itself, on the BUTTON_DOWN, before the kiss needs it.
            self.showcase_exit()
            if not self.blocked():
                self.start_anim("KISS")
            return
        # Feed, sweep, and ALARM's 0.5 s on its way to the mood step.
        if how in ("light_tap", "alarm_tap", "alarm_hold"):
            self.showcase_exit()
        if how == "light_tap" and not self.blocked():          # feed
            self.scene = "FEEDING"
        elif how == "alarm_tap" and self.scene != "DEAD":      # sweep
            self.has_poo = self.has_barf = self.poo_pending = False
            self.set_status()
        elif how == "light_cancel":                            # the 1.5 s toggle
            self.showcase_toggle()
        # alarm_hold on a live pet does nothing on its own


# light_cancel is LIGHT's 1.5 s hold, which now reaches the toggle clean: its
# 0.5 s press on the way past does nothing at all since the hug became a chord.
# That is also why "light_hold" is no longer a route out -- it is not one.
ESCAPES = ("light_cancel", "light_tap", "alarm_tap", "alarm_hold", "chord")
STARTS = [("HAPPY", "IDLE"), ("CONFUSED", "IDLE"), ("ANGRY", "IDLE"),
          ("DEAD", "DEAD"), ("HAPPY", "ASLEEP"), ("HAPPY", "NIGHT_AWAKE"),
          ("UPSET", "FEEDING"), ("HAPPY", "PLAYING")]


def check_running_order(anims, reel, plays):
    """The reel visits every entry in order, and holds none of them for ever."""
    pet = Pet(anims, reel, plays, "HAPPY", "IDLE")
    pet.showcase_toggle()
    ticks, wanted = 0, len(reel) * 2 + 1      # two laps, to prove it wraps
    limit = 200 * HZ
    while len(pet.seen) < wanted and ticks < limit:
        pet.tick()
        ticks += 1
    print("running order (%d entries, %d passes each):" % (len(reel), plays))
    problems = []
    if len(pet.seen) < wanted:
        stuck = pet.seen[-1] if pet.seen else "nothing"
        problems.append("the reel stopped advancing at %s" % stuck)
    else:
        for i, name in enumerate(pet.seen[:wanted]):
            want = reel[i % len(reel)]
            if name != want:
                problems.append("entry %d played %s, expected %s"
                                % (i, name, want))
    for name in reel:
        art = anims[name]["frames"]
        span = sum(h for _, h in art) if art else HZ
        print("  %-12s %2d poses  %4.1f s  x%d = %4.1f s%s"
              % (name.lower(), len(art) if art else 1, span / HZ, plays,
                 span * plays / HZ,
                 "" if anims[name]["layer"] == CHARACTER else "   !! not on the character layer"))
        if anims[name]["layer"] != CHARACTER:
            problems.append("%s is not a character-layer animation" % name)
    lap = sum((sum(h for _, h in anims[n]["frames"]) if anims[n]["frames"] else HZ)
              for n in reel) * plays / HZ
    print("  one lap: %.1f s" % lap)
    for why in problems:
        print("  !! %s" % why)
    return problems


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        here, "..", "..", "watch-faces", "complication", "pet_face.c")
    src = open(path, encoding="utf-8").read()
    anims = read_anims(src)
    reel = read_reel(src)
    plays = read_plays(src)
    exit_rests = read_exit_rests(src)
    chord_exits = read_chord_exits(src)
    print("leaving the showcase rests: %s" % ("yes" if exit_rests else "NO"))
    print("the hug takes the screen back first: %s\n"
          % ("yes" if chord_exits else "NO"))

    missing = [a for a in reel if a not in anims]
    if missing:
        print("!! not in _pet_anims: %s" % ", ".join(missing))
        return 1

    problems = check_running_order(anims, reel, plays)
    if not chord_exits:
        problems.append("_pet_chord does not hand the screen back before hugging")
        print("  _pet_chord does not hand the screen back before hugging")
    print()

    failures = []
    checked = 0
    for mood, scene in STARTS:
        for steps in range(len(reel)):
            for how in ESCAPES:
                pet = Pet(anims, reel, plays, mood, scene, exit_rests=exit_rests)
                pet.showcase_toggle()
                # Run the reel forward to the entry under test.
                for _ in range(20 * HZ * 60):
                    if pet.showcase_step == steps:
                        break
                    pet.tick()
                for _ in range(4):          # and a moment into it
                    pet.tick()
                held = reel[pet.showcase_step]
                pet.escape(how)
                for _ in range(600):        # 75 s with no further input
                    pet.tick()
                checked += 1

                character = pet.layer[CHARACTER]["anim"]
                status = pet.layer[STATUS]["anim"]
                why = None
                if pet.showcase_on:
                    why = "still reeling"
                elif character == "NONE":
                    why = "pet gone from the screen"
                elif character in MOODS and character != pet.mood:
                    why = "stuck showing %s" % character
                elif character == "SNORE" and pet.scene != "ASLEEP":
                    why = "stuck snoring while awake"
                elif status != "NONE" and not (pet.has_poo or pet.has_barf):
                    why = "phantom %s on a clean floor" % status.lower()
                if why:
                    failures.append((mood, scene, held, how, why))

    width = max(len(f[2]) for f in failures) if failures else 10
    for mood, scene, held, how, why in failures:
        print("  %-8s %-12s showing %-*s leave by %-12s -> %s"
              % (mood, scene, width, held, how, why))

    print("\n%d reel positions x escapes checked, %d stuck"
          % (checked, len(failures)))
    ok = not failures and not problems
    print("all checks passed" if ok else "FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
