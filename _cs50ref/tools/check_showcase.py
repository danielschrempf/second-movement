#!/usr/bin/env python3
"""Proves you can always get back to the live pet.

The showcase holds an animation on screen by forcing it to loop. Leaving it is
the interesting half: clearing `showcase_on` only stops that forcing, so a
one-shot ends and reaches `_pet_rest` on its own -- but a looping animation
keeps looping, and a status animation leaves the character layer on
PET_ANIM_NONE, which `_pet_layer_tick` skips entirely. Neither reaches
`_pet_rest`, and the showcased animation stays up for good.

So this replays the layer engine and asserts one invariant: from every position
in the walk, leaving the showcase by every route ends with the pet back on
screen showing its real mood, and nothing on the floor it did not put there.

The animation table is parsed out of pet_face.c, so the art cannot go stale,
and so is the one thing under test: whether leaving the showcase hands the
screen back (`read_exit_rests`). The rest of the engine below -- `_pet_layer_tick`,
`_pet_rest` -- is a transcription and IS a copy. If those change, change them
here too.

    python3 check_showcase.py
"""
import os
import re
import sys

import check_sounds

HZ = 8
CHARACTER, STATUS = 0, 1

# The walk order is the enum order in pet_face.h.
ORDER = ["NONE", "HAPPY", "CONFUSED", "UPSET", "ANGRY", "DEAD", "RESURRECT",
         "POO", "PLAY_SMALL", "PLAY_BIG", "BARF", "EAT", "KISS", "SNORE",
         "WAKE", "PILE", "PUDDLE"]
INDEX = {name: i for i, name in enumerate(ORDER)}

MOODS = ("HAPPY", "CONFUSED", "UPSET", "ANGRY", "DEAD")


def read_exit_rests(src):
    """Does leaving the showcase hand the screen back to the live pet?

    This is the behaviour under test, so it is read from the source rather than
    assumed: the escape cases at the top of pet_face_loop must route through a
    helper that calls _pet_rest. Setting showcase_on = false inline does not
    count -- that is the bug this check exists to catch.
    """
    loop = src[src.index("bool pet_face_loop"):]
    escape = loop[:loop.index("switch (event.event_type) {",
                              loop.index("switch (event.event_type) {") + 1)]
    if "_pet_showcase_exit" not in escape:
        return False
    helper = re.search(r"static void _pet_showcase_exit\(.*?\n\}", src, re.S)
    return bool(helper) and "_pet_rest" in helper.group(0)


def read_anims(src):
    """{anim: {frames, loop, layer}}, straight out of _pet_anims."""
    frames = check_sounds.read_frames(src)
    table = src[src.index("static const pet_anim_t _pet_anims"):]
    table = table[:table.index("\n};")]
    table = table[table.index("= {") + 3:]
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

    def __init__(self, anims, mood, scene, has_poo=False, has_barf=False,
                 exit_rests=True):
        self.anims = anims
        self.exit_rests = exit_rests
        self.layer = [{"anim": "NONE", "idle": "NONE", "frame": 0, "hold": 0}
                      for _ in range(2)]
        self.mood, self.scene = mood, scene
        self.has_poo, self.has_barf, self.poo_pending = has_poo, has_barf, False
        self.showcase_on, self.showcase_anim = False, "NONE"
        self.queue = []

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
        if self.showcase_on:
            loop_here, queued = True, False
        if L["hold"] > 1:
            L["hold"] -= 1
            return
        L["frame"] += 1
        if L["frame"] < count:
            L["hold"] = self.hold_of(L["anim"], L["frame"])
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

    def showcase_next(self):
        nxt = INDEX[self.showcase_anim] + 1 if self.showcase_anim != "NONE" else INDEX["HAPPY"]
        if nxt >= len(ORDER):
            self.showcase_on, self.showcase_anim = False, "NONE"
            self.rest()
            return
        self.showcase_on, self.showcase_anim = True, ORDER[nxt]
        self.queue = []
        self.layer_play(CHARACTER, "NONE")
        self.layer_play(STATUS, "NONE")
        self.start_anim(ORDER[nxt])

    def showcase_exit(self, keep_cursor):
        had_screen = self.showcase_on
        self.showcase_on = False
        if not keep_cursor:
            self.showcase_anim = "NONE"
        if had_screen and self.exit_rests:
            self.rest()

    def escape(self, how):
        """The escape switch at the top of pet_face_loop, then the action."""
        self.showcase_exit(keep_cursor=how.endswith("hold"))
        if how == "light_tap" and not self.blocked():          # feed
            self.scene = "FEEDING"
        elif how == "alarm_tap" and self.scene != "DEAD":      # sweep
            self.has_poo = self.has_barf = self.poo_pending = False
            self.set_status()
        elif how == "light_hold" and not self.blocked():       # hug
            self.start_anim("KISS")
        # alarm_hold on a live pet does nothing on its own


ESCAPES = ("light_tap", "alarm_tap", "light_hold", "alarm_hold")
STARTS = [("HAPPY", "IDLE"), ("CONFUSED", "IDLE"), ("ANGRY", "IDLE"),
          ("DEAD", "DEAD"), ("HAPPY", "ASLEEP"), ("HAPPY", "NIGHT_AWAKE"),
          ("UPSET", "FEEDING"), ("HAPPY", "PLAYING")]


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        here, "..", "..", "watch-faces", "complication", "pet_face.c")
    src = open(path, encoding="utf-8").read()
    anims = read_anims(src)
    exit_rests = read_exit_rests(src)
    print("leaving the showcase rests: %s\n" % ("yes" if exit_rests else "NO"))

    missing = [a for a in ORDER if a not in anims]
    if missing:
        print("!! not in _pet_anims: %s" % ", ".join(missing))
        return 1

    failures = []
    checked = 0
    for mood, scene in STARTS:
        for steps in range(1, len(ORDER)):
            for how in ESCAPES:
                pet = Pet(anims, mood, scene, exit_rests=exit_rests)
                for _ in range(steps):
                    pet.showcase_next()
                    for _ in range(4):
                        pet.tick()
                held = pet.showcase_anim
                pet.escape(how)
                for _ in range(600):        # 75 s with no further input
                    pet.tick()
                checked += 1

                character = pet.layer[CHARACTER]["anim"]
                status = pet.layer[STATUS]["anim"]
                why = None
                if character == "NONE":
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
        print("  %-8s %-12s showcasing %-*s leave by %-11s -> %s"
              % (mood, scene, width, held, how, why))

    print("\n%d showcase positions x escapes checked, %d stuck"
          % (checked, len(failures)))
    print("all checks passed" if not failures else "FAILED")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
