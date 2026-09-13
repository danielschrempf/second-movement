# Casio Pet — Owner's Manual

A virtual pet that lives in your watch. It gets hungry, bored and filthy on its
own schedule, and it will die if you ignore it for two days.

**Read the watch sideways** — turn your wrist so the display runs top to bottom.

---

## 1. Reading the display

The pet is built from the segments that normally show the time. The colon is its
eyes; the minutes-tens digit is its mouth. Everything else on screen is status.

```
      as worn, rotated 90° clockwise

      ┌───────────┐
      │     4     │   ┌─────┐
      │     5     │   │  0  │  effect
      │   :   :   │   │  1  │  snore / kiss
      │     6     │   └─────┘
      │     7     │   ┌─────┐
      │   8   9   │   │  3  │  food
      └───────────┘   └─────┘
```

| Cell | Is | Notes |
| --- | --- | --- |
| `:` | Eyes | Both dots move together — that is the blink |
| `6` | Mouth | Carries the whole expression |
| `4`–`8` | Body | Yawns, play, the barf on its way out |
| `9` | The floor | Where the mess lands, and stays |
| `1` | Off the mouth | Snores and kisses drift out here |
| `3` | Food | One pip per queued meal, up to four |
| `0` | Effect | A plus when something helped, a minus when it hurt |
| `BELL` | Dinner bell | Flashes as food is added |
| `SIGNAL` | Sound | Flashes on every noise, so a silent watch still reads |

---

## 2. Controls

Two buttons and the accelerometer. A long press is about half a second.

| Press | Does | Notes |
| --- | --- | --- |
| `LIGHT` | Queue a meal | Up to four. Eating starts 3 s after the last press |
| `LIGHT` hold | Hug | Four a day help; after that it is still hugged, it just gains nothing |
| `ALARM` | Sweep | Clears the floor, and any mess already on its way |
| `ALARM` hold | Resurrect | Only while dead. Starts a fresh pet at zero |
| Shake | Play | Keep shaking up to 5 s. Shake too hard and it is sick |
| `MODE` | Leave | Nothing runs while you are away |

**The pet does not run in the background.** It only moves, sounds and reacts
while its face is on screen. Neglect still accrues — the watch works out what
should have happened the moment you come back.

---

## 3. Mood

One number drives everything, measured in **tics**. It climbs when the pet is
left alone and falls when you look after it. Six tics is fatal.

| Mood | Tics | Face |
| --- | --- | --- |
| Happy | 0 – 1.75 | Smile, eyes blinking |
| Confused | 2 – 2.75 | Flat mouth, a brow that shifts |
| Upset | 3 – 3.75 | Brow down, mouth turned down |
| Angry | 4 – 5.75 | The same frown under a heavy scowl |
| Dead | 6+ | A grave |

### What moves the number

| Event | Tics | Limit |
| --- | --- | --- |
| Every 6 waking hours | +1 | Never stops |
| A day without eating | +1 | Stacks on the above |
| Mess left on the floor | +1 / 6 h | Doubles the rate until swept |
| Disturbed at night | +0.25 | Every time |
| Made sick by over-shaking | +0.25 | And the play reward is taken back |
| Eats one pip | −0.25 | Four pips per feeding |
| Hug | −0.25 | Four per calendar day |
| Play | −0.5 | Once every 2 hours |

**Sleep is free.** Nothing decays between 21:00 and 05:00 — the clock only counts
the pet's waking hours against it.

---

## 4. Feeding

Each press of `LIGHT` queues one pip, shown in cell 3, to a maximum of four.
Three seconds after you stop pressing, the pet starts eating — one pip at a time,
with a chew for each. Pressing again during the meal puts more on the plate and
restarts the wait.

**What goes in must come out.** Twelve hours after a meal the pet leaves a mess
in cell 9, and squats to do it. Until you sweep it with `ALARM`, it decays twice
as fast as normal.

---

## 5. Playing

Shake the watch. The pet plays for as long as you keep shaking, up to five
seconds, then reacts to how enthusiastic you were — a small routine for a gentle
session, a bigger one if you really committed.

Shake harder than that and it is sick: the reward is taken back and a quarter tic
added on top, so over-doing it is worse than never having played.

**The reward lands once every two hours**, no matter how often you play. Shaking
every few seconds does nothing but make the pet dizzy.

---

## 6. Night

The pet sleeps from **21:00 to 05:00**, snoring every third breath, and decays
not at all while it does.

Feeding, hugging or shaking a sleeping pet wakes it instead: a quarter tic added,
no benefit, and it stays up for thirty seconds before settling.

You can **sweep at night without waking it** — cleaning up is the one thing it
doesn't mind.

---

## 7. Death

At six tics the pet dies and a grave appears. Nothing you do reaches it; feeding,
hugging and shaking are all ignored.

**Hold `ALARM` to resurrect.** The grave fades, a spirit rises up the display, and
a new pet forms at zero tics with a clean floor and a fresh day of hugs.

> Ignored completely, a pet dies in **38 to 46 hours**. The spread depends on how
> much of that is night, which costs it nothing. Practically: leave it on a Friday
> evening and it will not see Sunday.

---

## 8. Keeping one alive

Passive decay alone is one tic every six waking hours, so the pet needs roughly
two and a half tics of care a day just to hold level. Simulated over a week:

| Routine | After a week | Verdict |
| --- | --- | --- |
| Three visits a day — feed, hug, play | ≈2 tics | Stable, settles around Confused |
| Two visits a day | Dead | Within the week |
| One visit a day | Dead | By day three |

A visit worth making is all three things: feed a full plate of four, take all four
hugs, and play once if the two-hour cooldown is up. That is about 2.5 tics —
enough to hold the line, with the surplus going to whatever you missed yesterday.

**A happy pet is not the goal.** Holding it at Confused or Upset is a realistic
week. Happy takes real attention, and that is the point: you are meant to see the
whole range of its moods, not one contented face.

---

## 9. Sounds

Every sound also flashes `SIGNAL`, so a watch kept silent still shows you what the
pet is doing.

| Sound | When |
| --- | --- |
| Snore | High then low, the low landing as the puff leaves its mouth. Two breaths in three |
| Kiss | A quick chromatic trill up, as the pucker completes |
| Eat | A chirp on the swallow, then one thud per chew |
| Barf | "Uh oh" over the wobbling mouth, then a chromatic slide down |
| Drop | A single low knock, as it hits the floor |
| Play | An arpeggio up and back; the bigger routine starts a tone higher |

---

## 10. Preview controls

Most of the pet's animations need the clock to cooperate — anger takes most of a
day of neglect, death a day and a half, snoring waits for 21:00. Two controls walk
through them on demand.

| Press | Does |
| --- | --- |
| `LIGHT` hold 1.5 s | Hold the next animation on screen. Keep going to walk the whole list and hand the screen back |
| `ALARM` hold 1.5 s | Push the mood up one tic, wrapping past dead back to blissful |

> **These are development controls.** Both fire their normal short action on the
> way past — you will feed or sweep the pet as you reach for them. Set
> `PET_DEBUG_CONTROLS` to `0` in `pet_face.h` to compile them out.

---

## 11. Specifications

| Item | Value |
| --- | --- |
| Animation rate | 8 frames / second |
| Waking hours | 05:00 – 21:00 |
| Passive decay | 1 tic / 6 waking hours |
| Food queue | 4 pips |
| Hug allowance | 4 / calendar day |
| Play reward cooldown | 2 hours |
| Mess appears | 12 hours after a meal |
| Fatal at | 6 tics |

**Your pet survives a flat battery of attention, but not a flat battery.** It
persists across face changes and the watch's power-saving sleep, so in daily wear
it lives indefinitely. Pulling the cell or reflashing the firmware hatches a new
one.

---

*Casio Pet — a CS50x final project for the [Sensor Watch](https://www.sensorwatch.net).
Design notes in [DEVLOG.md](DEVLOG.md); segment reference in [SEGMENT_MAP.md](SEGMENT_MAP.md).*
