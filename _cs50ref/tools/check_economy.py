#!/usr/bin/env python3
"""Checks the manual's economy tables against the tunables they describe.

MANUAL.md section 3 is the direct answer to "what gains and loses time": every
action's value, every daily ceiling, and the arithmetic those add up to. All of
it is derived from the #defines in pet_face.h -- and all of it was worked out by
hand, which is exactly the kind of thing that goes quietly wrong the first time a
buff is retuned. It already had: an earlier draft put the over-shake barf at
+1.0 when PET_BUFF_PLAY makes it +0.75.

So this reads the tunables straight out of pet_face.h -- no copied constants --
recomputes the economy, and then checks that every number the manual prints
still matches. Retune a buff without touching the docs and this fails.

    python3 check_economy.py [path/to/pet_face.h] [path/to/MANUAL.md]

Exits non-zero if the manual and the header disagree, or if a row it expects to
find has been reworded out of existence.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_H = os.path.join(HERE, "..", "..", "watch-faces", "complication", "pet_face.h")
DEFAULT_MAN = os.path.join(HERE, "..", "MANUAL.md")

# The manual writes minus signs as U+2212, not as a hyphen.
MINUS = "−"

WANTED = """
    PET_QT_PER_TIC PET_SECONDS_PER_TIC PET_SECONDS_PER_QT PET_POO_SECONDS_PER_QT
    PET_MISSED_FEED_SECONDS PET_BUFF_PLAY PET_BUFF_HUG PET_HUG_CAP PET_BUFF_EAT
    PET_DEBUFF_DISTURB PET_DEBUFF_BARF PET_HOUR_WAKE PET_HOUR_SLEEP
    PET_FEED_SEGMENTS PET_FEED_SEGMENT_CAP PET_FOOD_MAX PET_PLAY_COOLDOWN_SECONDS
    PET_BARF_SETTLE_SECONDS PET_QT_DEAD
""".split()

fails = []


def check(what, ok, detail=""):
    print("  %-56s %s" % (what, "ok" if ok else "FAIL"))
    if detail:
        print("      %s" % detail)
    if not ok:
        fails.append(what)


def read_tunables(path):
    """Evaluate the #defines we care about, in the order the header sets them.

    They are plain integer arithmetic over each other -- PET_TIC(n) and the rest
    -- so evaluating each in file order against what has been read so far is
    enough, and means the header stays the only place the numbers live.
    """
    src = open(path, encoding="utf-8", errors="replace").read()
    # PET_TIC(n) is a function-like macro; the rest are plain values.
    tic_mul = int(re.search(r"^#define\s+PET_QT_PER_TIC\s+(\d+)", src, re.M).group(1))
    env = {}
    for m in re.finditer(r"^#define\s+([A-Z_0-9]+)\s+(.+?)\s*(?://.*)?$", src, re.M):
        name, body = m.group(1), m.group(2).strip()
        if name not in WANTED:
            continue
        body = re.sub(r"PET_TIC\(\s*([^)]+?)\s*\)", r"((\1) * %d)" % tic_mul, body)
        try:
            env[name] = eval(body, {"__builtins__": {}}, dict(env))
        except Exception:
            pass    # not an arithmetic constant; nothing here needs it
    missing = [n for n in WANTED if n not in env]
    if missing:
        sys.exit("could not read from %s: %s" % (path, ", ".join(missing)))
    return env


def economy(t):
    """Everything MANUAL.md section 3 states, in tics."""
    qt = t["PET_QT_PER_TIC"]
    waking_h = t["PET_HOUR_SLEEP"] - t["PET_HOUR_WAKE"]
    hours_per_tic = t["PET_SECONDS_PER_TIC"] / 3600.0
    e = {
        "waking_hours": waking_h,
        "passive_per_day": waking_h / hours_per_tic,
        "poo_matches_passive": t["PET_POO_SECONDS_PER_QT"] == t["PET_SECONDS_PER_QT"],
        "missed_feed_hours": t["PET_MISSED_FEED_SECONDS"] / 3600.0,
        "eat": t["PET_BUFF_EAT"] / qt,
        "hug": t["PET_BUFF_HUG"] / qt,
        "play": t["PET_BUFF_PLAY"] / qt,
        "disturb": t["PET_DEBUFF_DISTURB"] / qt,
        "settle_minutes": t["PET_BARF_SETTLE_SECONDS"] / 60.0,
        "fatal_tics": t["PET_QT_DEAD"] / qt,
    }
    e["pips_per_day"] = t["PET_FEED_SEGMENT_CAP"] * t["PET_FEED_SEGMENTS"]
    e["eat_per_day"] = e["pips_per_day"] * e["eat"]
    e["hug_per_day"] = t["PET_HUG_CAP"] * e["hug"]
    # The cooldown is wall clock, so the buff can be collected from the first
    # waking second and every PET_PLAY_COOLDOWN_SECONDS after, while still awake.
    e["plays_per_day"] = waking_h * 3600 // t["PET_PLAY_COOLDOWN_SECONDS"] + 1
    e["play_per_day"] = e["plays_per_day"] * e["play"]
    # A barf hands back what it was given, and charges the penalty on top.
    e["play_barf"] = (t["PET_BUFF_PLAY"] + t["PET_DEBUFF_BARF"]) / qt
    e["play_barf_cold"] = t["PET_DEBUFF_BARF"] / qt
    e["feed_barf"] = (t["PET_FEED_SEGMENT_CAP"] * t["PET_BUFF_EAT"]
                      + t["PET_DEBUFF_BARF"]) / qt
    # One complete visit: a full plate, the whole hug allowance, one play.
    e["visit"] = (t["PET_FOOD_MAX"] * t["PET_BUFF_EAT"]
                  + t["PET_HUG_CAP"] * t["PET_BUFF_HUG"]
                  + t["PET_BUFF_PLAY"]) / qt
    e["visits"] = []
    for n in (1, 2, 3):
        pips = min(n, t["PET_FEED_SEGMENTS"]) * t["PET_FOOD_MAX"]
        e["visits"].append({
            "n": n, "pips": pips, "hugs": t["PET_HUG_CAP"], "plays": n,
            "gain": (pips * t["PET_BUFF_EAT"] + t["PET_HUG_CAP"] * t["PET_BUFF_HUG"]
                     + n * t["PET_BUFF_PLAY"]) / qt,
        })
    return e


def report(e):
    print("the economy, computed from pet_face.h\n")
    print("  waking day                %d h" % e["waking_hours"])
    print("  passive decay             +%s tics/day, and the poo rate %s it"
          % (num(e["passive_per_day"]),
             "matches" if e["poo_matches_passive"] else "DIFFERS FROM"))
    print("  missed feed               +1.0 tic per %d h" % e["missed_feed_hours"])
    print("  fatal at                  %s tics" % num(e["fatal_tics"]))
    print()
    print("  eat   %2d pips/day         -%s tics/day (-%s each)"
          % (e["pips_per_day"], num(e["eat_per_day"]), num(e["eat"])))
    print("  hug    %d hugs/day         -%s tics/day (-%s each)"
          % (round(e["hug_per_day"] / e["hug"]), num(e["hug_per_day"]), num(e["hug"])))
    print("  play   %d plays/day        -%s tics/day (-%s each)"
          % (e["plays_per_day"], num(e["play_per_day"]), num(e["play"])))
    print()
    print("  waking it at night        +%s" % num(e["disturb"]))
    print("  over-shaking to rung 3    +%s, or +%s on cooldown"
          % (num(e["play_barf"]), num(e["play_barf_cold"])))
    print("  a fifth pip in a sitting  +%s at most" % num(e["feed_barf"]))
    print("  settling after a barf     %d min" % e["settle_minutes"])
    print()
    print("  one complete visit        -%s tics, against +%s a day of decay"
          % (num(e["visit"]), num(e["passive_per_day"])))
    for v in e["visits"]:
        print("    %d visit(s): %2d pips, %d hugs, %d plays = -%s"
              % (v["n"], v["pips"], v["hugs"], v["plays"], num(v["gain"])))
    print()


def num(x):
    """The way the manual writes these.

    Everything here is a whole number of quarter tics, so two decimals is always
    enough and often one too many: 2.50 is written 2.5, but 0.25 stays 0.25.
    """
    text = "%.2f" % x
    return text[:-1] if text.endswith("0") else text


def find(man, pattern, what):
    """Pull one number out of the manual, or fail saying the row is gone."""
    m = re.search(pattern, man)
    if not m:
        check(what, False, "no row in MANUAL.md matched /%s/" % pattern)
        return None
    return m.group(1)


def compare(man, e):
    print("MANUAL.md section 3 against those figures\n")

    rows = [
        ("passive decay, tics a day",
         r"\*\*\+([0-9.]+) tics every day\*\*", num(e["passive_per_day"])),
        ("eat, daily ceiling",
         r"\| Eat one pip \|.*?=\s*\*\*" + MINUS + r"([0-9.]+)\*\*", num(e["eat_per_day"])),
        ("hug, daily ceiling",
         r"\| Hug \|.*?=\s*\*\*" + MINUS + r"([0-9.]+)\*\*", num(e["hug_per_day"])),
        ("play, daily ceiling",
         r"\| Play \|.*?=\s*\*\*" + MINUS + r"([0-9.]+)\*\*", num(e["play_per_day"])),
        ("play, plays a day",
         r"\| Play \|.*?so (\d+) in a waking day", str(e["plays_per_day"])),
        ("over-shaking, total",
         r"Over-shaking to rung 3 \|.*?\*\*\+([0-9.]+)\*\* in total", num(e["play_barf"])),
        ("over-shaking, on cooldown",
         r"Over-shaking to rung 3 \|.*?or \+([0-9.]+) if the cooldown",
         num(e["play_barf_cold"])),
        ("a fifth pip, total",
         r"A fifth pip in one sitting \|.*?up to \+([0-9.]+) in total", num(e["feed_barf"])),
        ("waking it at night",
         r"\| Waking it at night \| \+([0-9.]+) \|", num(e["disturb"])),
        ("one complete visit",
         r"One complete visit is worth exactly " + MINUS + r"([0-9.]+) tics",
         num(e["visit"])),
        ("passive decay, restated",
         r"Passive decay is exactly \+([0-9.]+) tics a day", num(e["passive_per_day"])),
    ]
    for what, pattern, want in rows:
        got = find(man, pattern, what)
        if got is None:
            continue
        check(what, got == want, None if got == want else "manual says %s, header says %s" % (got, want))

    # The visits table, row by row.
    for v in e["visits"]:
        what = "visits table, %d visit(s)" % v["n"]
        pattern = (r"\|\s*%d\s*\|\s*(\d+)\s*\|\s*(\d+)[^|]*\|\s*(\d+)\s*\|\s*\*\*"
                   % v["n"]) + MINUS + r"([0-9.]+)\*\*"
        m = re.search(pattern, man)
        if not m:
            check(what, False, "no row in MANUAL.md matched /%s/" % pattern)
            continue
        got = (m.group(1), m.group(2), m.group(3), m.group(4))
        want = (str(v["pips"]), str(v["hugs"]), str(v["plays"]), num(v["gain"]))
        check(what, got == want,
              None if got == want else "manual says %s, header says %s" % (got, want))

    # Section 15's specification table restates two of these.
    print()
    for what, pattern, want in [
        ("spec: passive decay",
         r"so ([0-9.]+) tics a day \|", num(e["passive_per_day"])),
        ("spec: settling",
         r"\| Settling after a barf \| (\d+) min", "%d" % e["settle_minutes"]),
    ]:
        got = find(man, pattern, what)
        if got is None:
            continue
        check(what, got == want,
              None if got == want else "manual says %s, header says %s" % (got, want))


def main():
    h = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_H
    man = sys.argv[2] if len(sys.argv) > 2 else DEFAULT_MAN
    t = read_tunables(h)
    e = economy(t)
    report(e)
    check("the poo charges the same rate as passive decay", e["poo_matches_passive"],
          None if e["poo_matches_passive"]
          else "PET_POO_SECONDS_PER_QT no longer equals PET_SECONDS_PER_QT, "
               "so the manual's \"doubles the rate\" is wrong")
    print()
    compare(open(man, encoding="utf-8", errors="replace").read(), e)
    print("\n%s" % ("FAILURES ABOVE" if fails else "all checks passed"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
