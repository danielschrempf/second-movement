# Casio Pet — Manual

A Tamagotchi-style watch face for the [Sensor Watch](https://www.sensorwatch.net)
on a Casio F-91W. One mood meter, five expressions, fourteen animations, ten
sounds, and a pet that dies in under two days if you ignore it.

**The face reads sideways** — turn the watch 90° clockwise so the main line runs
top to bottom. All the art is drawn for that orientation; there is no upright
mode.

Built as a CS50x final project. Design notes and the reasoning behind every
departure from the spec are in [DEVLOG.md](DEVLOG.md); the segment reference is
[SEGMENT_MAP.md](SEGMENT_MAP.md).

---

## 1. Display map

The pet is composited from the segments that normally show the time. The colon
is its eyes; the minutes-tens digit is its mouth.

```text
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

| Cell | Owner | Is |
| --- | --- | --- |
| `:` | character | Eyes. One segment — both dots blink together, they cannot wink |
| `6` | character | Mouth. Carries the whole expression |
| `4`,`5`,`7`,`8` | character | Body. Yawns, play, the barf on its way out |
| `9` | shared | The floor. Character sweeps through it on resurrect; status keeps the pile |
| `1` | character | Off the mouth — snores and kisses drift out here |
| `3` | procedural | Food. One pip per queued meal (`B C F E` in order), max four |
| `0` | procedural | Effect. `G\|H` is a plus, `H` alone a minus |
| `2` | unused | Freed when the tic HUD was dropped |
| `BELL` | procedural | Flashes as food is added |
| `SIGNAL` | procedural | Flashes on every sound, so a silent watch still reads |

Cells `0`, `2` and `3` are barred to both animation layers — they are drawn
straight from state, and the compositor masks off anything an animation puts
there. See §14.

---

## 2. Controls

Long press is 0.5 s; the showcase holds in §10 are 1.5 s.

| Press | Does | Notes |
| --- | --- | --- |
| `LIGHT` | Queue a meal | Up to four. Eating starts 3 s after the last press |
| `LIGHT` hold | Hug | Four a day help; past the cap it is still hugged, it just gains nothing |
| `ALARM` | Sweep | Clears the floor, and any mess already on its way |
| `ALARM` hold | Resurrect | Only while dead |
| Shake | Play | Accelerometer tap detection, 5 s window |
| `MODE` | Leave | Movement's default — next face |
| `LIGHT`/`ALARM` hold 1.5 s | Showcase | Walk the animations, or step the mood. `LIGHT` spends a hug getting there — see §10 |

In the simulator, which has no accelerometer, `ALARM` hold stands in for a shake
while the pet is alive.

**Nothing runs in the background.** The face only ticks while it is on screen;
`pet_face_resign` drops the tick rate back to 1 Hz and disables tap detection.
Decay still accrues — `_pet_catch_up` reconstructs it from timestamps on the next
activate. This is also what stops an arm rolling over in bed from shaking the pet
awake all night.

---

## 3. Mood

One counter drives everything, stored in **quarter tics** so the spec's 0.25
steps stay integer. Six tics (24 quarter tics) is fatal.

| Mood | Tics | Face |
| --- | --- | --- |
| Happy | 0 – 1.75 | Smile, eyes blinking |
| Confused | 2 – 2.75 | Flat mouth, a brow that shifts once a second |
| Upset | 3 – 3.75 | Brow down, mouth turned down |
| Angry | 4 – 5.75 | The same frown under a heavier scowl |
| Dead | 6+ | A grave |

### What moves it

| Event | Tics | Limit |
| --- | --- | --- |
| Every 6 **waking** hours | +1 | Never stops |
| A day without eating | +1 | Stacks on passive decay, capped at 6 days |
| Mess left on the floor | +1 / 6 h | Doubles the passive rate until swept |
| Disturbed at night | +0.25 | Every time |
| Made sick by over-shaking | +0.25 | And the play buff is taken back |
| Eats one pip | −0.25 | Four pips per feeding |
| Hug | −0.25 | Four per calendar day |
| Play | −0.5 | Once per 2 h cooldown |

**Only waking seconds count.** Nothing decays between 21:00 and 05:00. The
accounting is exact rather than approximate — `_pet_awake_between` converts any
two timestamps into elapsed waking seconds, handling spans over whole nights and
both day boundaries, and leftover seconds carry in a residual so that opening the
face often cannot round decay away.

---

## 4. Feeding

Each `LIGHT` press queues one pip in cell 3, max four. Three seconds after the
last press the pet eats one pip at a time, with a chew for each; pressing during
the meal adds to the plate and restarts the 3 s wait.

The pip timer waits on the eat animation rather than cutting it short, so a full
plate of four takes about 17 s end to end. Twelve hours after a meal the pet
squats and leaves a pile in cell 9. Until swept it decays at double rate.

The poo scene only plays if the drop is fresh — a catch-up that discovers one
which landed hours ago just shows the pile.

---

## 5. Playing

Shake to play. The pet plays for as long as you keep shaking, up to 5 s, then
reacts to how enthusiastic you were: a small routine under the nausea limit, a
bigger one for a committed session.

Past the limit it barfs — the buff is returned and a further 0.25 tic added, so
over-shaking is strictly worse than not playing.

**The buff lands once per 2 h**, regardless of how often you play. Without that
cooldown, play was the one uncapped source of relief and a shake every 5 s healed
the pet to full in about a minute. The interaction is never blocked, only the
reward; a pet that silently ignored the button would read as broken.

---

## 6. Night

Sleeps 21:00 – 05:00, breathing on a 2 s loop and snoring on two breaths in
three.

Feeding, hugging or shaking a sleeping pet wakes it instead: +0.25 tic, no
benefit, and it stays up 30 s before settling. **Sweeping works at night without
waking it.**

---

## 7. Death

At six tics the pet dies and a grave appears. `_pet_blocked` absorbs every
interaction while dead.

Hold `ALARM` to resurrect: the grave fades, a spirit rises up the whole main
line, and a new pet forms at zero tics with a clean floor, a reset hug cap and a
cleared play cooldown.

Ignored completely, a pet dies in **38–46 h of wall clock**. The spread depends
on how much of that window is night, which costs it nothing.

---

## 8. Balance

Passive decay alone is 1 tic per 6 waking hours, so the pet needs roughly 2.5
tics of care a day to hold level. From `check_balance.c`, quarter tics at the end
of each of seven days:

```text
3/day, feed at 13:00 (midday)       3  5  5  6  7  7  8   healthy
3/day, feed at 19:00 (evening)      1  5  5  6  7  7  8   healthy
2/day (08:00,19:00), feed morning   7 10 12 15 18 24 24   DEAD
2/day (08:00,19:00), feed evening   3  7  9 12 15 17 24   DEAD
1/day (evening only)                1 14 24 24 24 24 24   DEAD
no care at all                      9 24 24 24 24 24 24   DEAD
```

Three visits a day converge and hold around 2 tics. Two a day die within the
week; one a day dies on day three. A visit worth making is all three actions —
a full plate of four, all four hugs, and a play if the cooldown is up.

Both feed timings survive, which was not true of an earlier tuning: feeding only
in the evening used to be a trap, because the poo landed overnight and the pet
woke up already behind.

**A permanently happy pet is not the target.** Holding at Confused or Upset is a
realistic week, and the point is to see the whole range of moods rather than one
contented face.

---

## 9. Sounds

Ten sequences, each also flashing `SIGNAL`. Durations are 1/64 s; the buzzer is a
monophonic square wave, so pitch and rhythm are the only tools.

| Sound | Cue | Is |
| --- | --- | --- |
| Snore in / out | loop start; cell 1 `D` lights | High then low, the low landing on the puff |
| Kiss | cell 6 `G` lights | Chromatic trill up as the pucker completes |
| Eat gulp | cell 7 clears | Mid chirp on the swallow |
| Eat chew | cell 6 `G` lights | One thud per jaw movement — fires three times |
| Barf uh-oh | barf begins | Two falling notes over the wobbling mouth |
| Barf slide | cell 7 `D` lights | Chromatic octave down, travelling with it |
| Poo | cell 9 `B\|C` lights | One short low knock as the pile lands |
| Play small / big | animation begins | Arpeggio up and back; big starts a whole tone up |

Sounds are attached to animations as **cues** — a cell, a segment mask and an
edge — rather than timed against them. See §14.

---

## 10. Showcase

Most of the pet's animations are gated behind the clock: angry takes most of a
day of neglect, dead a day and a half, snoring waits for 21:00. Two controls walk
the whole set on demand.

| Press | Does |
| --- | --- |
| `LIGHT` hold 1.5 s | Hold the next animation on screen. Repeat to walk all fourteen and hand the screen back to the live pet |
| `ALARM` hold 1.5 s | Push the mood up one tic, wrapping past dead back to blissful |

Held animations stay put rather than flashing past once, so one-shots can be
looked at for as long as you like. Stepping the mood is the fast way to see all
five expressions in order, death and resurrection included.

Both fire their 0.5 s long-press on the way past, since Movement delivers that
first. The short actions do **not** fire — `EVENT_*_BUTTON_UP` only arrives on a
release under half a second — so nothing is fed and nothing is swept. What does
happen:

- **`LIGHT` spends a hug.** Each animation you step through costs one of the four
  daily hugs and −0.25 tic. Walking all fourteen exhausts the cap four presses
  in; the rest are no-ops, so the pet ends up a tic healthier and out of hugs.
- **`ALARM` does nothing** on hardware while the pet is alive. If it is dead, the
  0.5 s press resurrects it before you reach the mood step. In the simulator,
  which stands in for the accelerometer here, it plays with the pet instead.

The hug is left in deliberately rather than refunded on escalation. It is a
fair trade — a pet you stop to admire gets a cuddle out of it — and the only
consequence is that a showcase session spends that day's hugs on a pet you are
also raising.

Set `PET_SHOWCASE` to `0` in `pet_face.h` for a build where the buttons only play
the game.

---

## 11. Build and flash

`BOARD` and `DISPLAY` are mandatory. The face is already registered in
`movement_config.h`.

```sh
make BOARD=sensorwatch_pro DISPLAY=classic -j8
```

Flashing: double-tap the reset button on the back of the board until the LED
pulses red and a `WATCHBOOT` drive appears.

- **macOS:** `make install` works directly.
- **Windows:** copy `build\firmware.uf2` onto the `WATCHBOOT` drive from
  Windows. `make install` from inside WSL cannot see the drive.

Simulator — name the HTML target explicitly, or the emscripten `all` target races
under `-j`:

```sh
emmake make -j8 BOARD=sensorwatch_pro DISPLAY=classic build-sim/firmware.html
python3 -m http.server -d build-sim 8000
```

> **After editing any header, `touch movement.c` or `make clean`.** The build
> system writes no `.d` files, so editing a header rebuilds nothing — and with
> `--gc-sections` that silently produces firmware missing the face entirely.
> Verify: `arm-none-eabi-nm build/firmware.elf | grep pet_face`

---

## 12. Verification

Four harnesses in [tools/](tools/). None are part of the firmware build; each
prints a pass/fail report and exits non-zero on failure.

```sh
cd _cs50ref/tools
cc -O2 -o check_layers check_layers.c && ./check_layers
cc -O2 -o check_awake_time check_awake_time.c && ./check_awake_time
cc -O2 -o check_balance check_balance.c && ./check_balance
python3 check_sounds.py
```

| Harness | Proves | Re-run when |
| --- | --- | --- |
| `check_layers.c` | The procedural cells are off limits to every layer, the two layers overlap only in cell 9 where that is intended, and a rogue frame gets clipped | The region map or `_pet_layers` changes |
| `check_awake_time.c` | Waking-seconds accounting is monotonic and additive over 400 spans, and handles whole nights and both day boundaries. Prints time-to-death from four start hours | `PET_HOUR_WAKE`/`PET_HOUR_SLEEP` or the decay rate change |
| `check_balance.c` | A week of care at different visit rates and feed timings, printing the mood trajectory | Any tunable in the buff/debuff block changes |
| `check_sounds.py` | Every cue describes a moment the art actually reaches, no cue repeats faster than its own sound can play, and no sound is unreachable | **Any animation is redrawn**, or a cue or sound is edited |

`check_sounds.py` parses `pet_face.c` directly, so it cannot go stale. Sample:

```text
eat_chew:   cell 6 G lights    fires at ticks [10, 12, 14] of 21
barf_slide: cell 7 D lights    fires at tick [9] of 20;
                               condition also true at [12], suppressed by `once`
```

That second line is the check earning its keep: cell 7 flickers as the barf
tumbles through it, so the slide fired twice and the second firing restarted a
9-tick chromatic ramp partway down, cutting it off. `once` in the cue table
suppresses the repeat, and the report keeps the choice visible in case a redraw
ever makes the second edge meaningful.

The other three copy their constants from the firmware rather than including it.
They are **not** wired to it, so a tunable changed in `pet_face.h` will not fail
them until it is changed in both — check both if a number moves.

---

## 13. Redrawing the art

Animations are segment art on a faithful F-91W template, rotated 90° clockwise,
exported as GIFs into [FaceAnimations/](FaceAnimations). They are not
transcribed by hand.

```sh
cd _cs50ref/tools
./decode.sh ../FaceAnimations/CasioPet_Animations-Happy.gif happy --check
./decode.sh --all ../FaceAnimations        # every file, checked
```

The output is a paste-ready `pet_frame_t` table; point the matching row of
`_pet_anims` at it with `PET_FRAMES()`, which fills in the count too so the two
cannot drift apart. `--check` reports:

- **Tied segments.** Some segments share one electrical address and light
  together whether the art wants it or not. A frame lighting one member alone
  cannot render as drawn, and this is the most likely way segment art goes wrong.
- **Cells used.** Which cells the drawing touches, so art straying outside its
  region shows up instead of being silently masked off.

Blank frames at the ends of an export are trimmed, and the note says so — they
are an artefact of the drawing program, and on a looping mood a blank tail reads
as the pet vanishing every cycle. Interior blanks are kept; that is how a flash
is drawn.

The decoder assumes a 480×480 canvas with the template where the current exports
put it, so keep the alignment identical between files. If it shifts, `CELLS` in
`decode.py` needs re-measuring — `segmap.c` prints component boxes to do it from.

**On the PC, run `decode.sh` from WSL.** `ffmpeg` is installed Windows-side and
`python3` is in WSL, so the script reaches across for `ffmpeg.exe` and puts its
scratch directory on `/mnt/d` where both sides can see it. On the Mac everything
is native.

Then run `check_sounds.py` — cues are attached to frames, and a redraw can move
the moment a sound was written for.

---

## 14. Implementation notes

**Layers.** The screen is composited from two independently timed layers,
`CHARACTER` and `STATUS`, each owning a per-segment mask. Bespoke art per
combination was never viable — five moods × five food states × two floor states
is fifty full-screen animations against twelve as layers. Cell 9 is shared rather
than split, because resurrect sweeps the spirit through it; that is safe because
compositing is a straight `OR`, so neither layer can erase the other.

**Cues.** Sounds name a moment in the art rather than a time:

```c
typedef struct {
    uint8_t sound;
    uint8_t position;   // the cell to watch
    uint8_t mask;       // these segments... (0 = when the animation begins)
    bool    on_clear;   // ...going dark, rather than lighting up
    bool    once;       // only the first time, per play or per loop
} pet_cue_t;
```

Storing the offset would mean writing down `rest 72, then knock` when the intent
was *knock when the pile hits the floor*. Offsets are unstable under redraw; the
moment is not. The snore's period is counted in breaths rather than seconds for
the same reason — it cannot drift from the animation it describes.

**Frame rate** is 8 Hz (`movement_request_tick_frequency` takes powers of two).
Art is drawn at 8 fps, so one exported frame is one frame on the watch and the
conversion is `frames ÷ 8 = seconds`. The decoder collapses identical
consecutive poses into one held frame, which is the `hold` field.

**Persistence** is RAM only, and deliberately so. State lives in the face's
Movement context (`malloc`ed once in `pet_face_setup`); `_pet_load` and
`_pet_save` are no-ops. It survives face switches and the watch's low-energy
sleep, so in daily wear a pet lives indefinitely. A reset, a reflash or a battery
pull hatches a new one — which is reasonable, since all three are deliberate acts
that mean opening the watch or rewriting it.

---

## 15. Specifications

| Item | Value |
| --- | --- |
| Tick rate on screen | 8 Hz |
| Waking hours | 05:00 – 21:00 |
| Passive decay | 1 tic / 6 waking hours |
| Mood resolution | quarter tics, `uint8_t` |
| Food queue | 4 pips |
| Hug allowance | 4 / calendar day |
| Play window / nausea limit | 5 s / 3 |
| Play buff cooldown | 2 h |
| Mess appears | 12 h after a meal |
| Fatal at | 6 tics |
| Animations | 14, decoded from GIF exports |
| Sounds | 10, cued to frames |
| Flash | 135,112 text + 2,124 data = 137,236 (56% of 245,760) |
