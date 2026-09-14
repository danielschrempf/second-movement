# Casio Pet — Manual

A Tamagotchi-style watch face for the [Sensor Watch](https://www.sensorwatch.net)
on a Casio F-91W. One mood meter, five expressions, fourteen animations, ten
sounds, and a pet that dies in about two days if you ignore it.

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
| `9` | shared | The floor. Character sweeps through it on resurrect; status keeps whatever was left there — a pile (`G\|B\|C`) or a barf puddle (`B\|C`) |
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
| `LIGHT` | Queue a meal | Up to four on the plate. Eating starts 3 s after the last press; only four stay down per sitting |
| `LIGHT` hold | Hug | Four a day help; past the cap it is still hugged, it just gains nothing |
| `ALARM` | Sweep | Clears the floor — both a pile and a puddle. A poo still on its way is left alone |
| `ALARM` hold | Resurrect | Only while dead |
| Shake | Play | Accelerometer tap detection. Each shake climbs a rung; 3 s deaf, then 5 s to shake again |
| `MODE` | Leave | Movement's default — next face |
| `LIGHT`/`ALARM` hold 1.5 s | Showcase | Walk the animations, or step the mood. `LIGHT` spends a hug getting there — see §10 |

In the simulator, which has no accelerometer, `ALARM` hold stands in for a shake
while the pet is alive.

**To silence the pet**, set `BTN beep` to `N` in the settings face — the pet
follows that setting like any button sound. See §9.

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
| Made sick by overfeeding | +0.25 | And the whole sitting's nutrition is taken back |
| Eats one pip | −0.25 | Four pips per sitting, three sittings a day |
| Hug | −0.25 | Four per calendar day |
| Play | −0.5 | Once per 2 h cooldown |

**Only waking seconds count.** Nothing decays between 21:00 and 06:00. The
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

### Sittings, and being sick of it

The waking day is fifteen hours and divides into three even **sittings** —
breakfast 06:00, lunch 11:00, dinner 16:00 — and the pet keeps only **four pips
down per sitting**. That is twelve pips a day if you spread them, which is the
most care feeding can buy.

The fifth pip in a sitting does not stay down. It leaves the plate, the barf
animation plays in its place, and the sitting's nutrition goes with it:

| | quarter tics |
| --- | --- |
| Pips 1–4 | −0.25 each, so −1.0 banked for the sitting |
| Pip 5 | **+1.0** handed back, **+0.25** penalty on top |
| Net for the sitting | **+0.25** — worse than never having fed at all |

That is the same penalty a play barf charges, and it leaves the same puddle to
sweep. Anything still on the plate is thrown out with it — the meal is over.

**The sitting stays closed.** The count does not reset after a barf, so feeding
again before the next sitting barfs again; by then there is no nutrition left to
return and it costs only the +0.25. The pet is done eating until 11:00.

Because the plate holds four and a sitting allows four, one full plate per
sitting is exactly the ceiling: you have to press deliberately past it to make
the pet sick. Being resurrected empties the stomach, so a pet that died overfed
can eat straight away.

The countdown is wall clock, so it usually expires while the face is in the
background. Whenever it lands, the **scene is owed to you**: the floor stays
clean until you are there to watch the pet squat, and only then does the pile go
down. That is the spec's "animation plays on revisit if one has been made". If
the pet is asleep when you arrive the scene is dropped rather than played over
the snore, and the pile simply appears.

**Sweeping does not cancel a poo on its way.** It clears the floor and nothing
else. An earlier build read "sweep clears all" to include the pending countdown,
which quietly made the poo unreachable: one press of `ALARM` after feeding — out
of curiosity, with nothing on the floor — reset the twelve hours, so a tidy owner
never saw a poo at all.

**When you feed decides what the poo costs.** It lands twelve hours later, and
only waking time is charged, so a meal at 08:00 drops a pile around 20:00 that
sits through the night and into the morning — three waking hours of double decay.
The same meal at 19:00 drops it around 07:00, an hour before a morning visit.
See §8.

---

## 5. Playing

Shake to play. Each shake climbs one rung of a three-rung ladder, and between
rungs the pet stops listening:

| Rung | Reached by | Shows |
| --- | --- | --- |
| 1 | Any shake while idle | `PLAY 1`, the small flourish |
| 2 | Shaking again inside the window | `PLAY 2`, the same shape a tone higher |
| 3 | Shaking again inside the next window | Barf |

After each flourish the pet is **deaf for 3 s**, then **listens for 5 s**. The
pause is measured from the end of the animation, not from the shake, so the
window you are offered is the whole of it. Let a window expire and the session
ends where it stands.

Reaching the third rung barfs — the buff is returned and a further 0.25 tic
added, so over-shaking is strictly worse than not playing — and leaves a puddle
in cell 9 to sweep. The barf serves out one more deaf period before the session
ends, for the same reason the rungs have one: the tail of the shake that caused
it would otherwise arrive as a fresh session and replace the barf animation with
`PLAY 1`, while the puddle and the debuff had already landed.

This replaced counting taps inside a single 5 s window, which measured how hard
the watch was shaken rather than how long it was played with. One flick of the
wrist is a burst of interrupts on a 400 Hz accelerometer, so a shake either
registered once — `PLAY 1`, every time — or tripped straight past the nausea
limit into a barf, with almost nothing in between. Pacing the ladder by the
clock means one shake can only ever count once, whatever the hardware makes of
it. Double-tap detection is left off for the same reason: the pet treats both
events identically, so enabling it only doubled the interrupts.

**The buff lands once per 2 h**, regardless of how often you play. Without that
cooldown, play was the one uncapped source of relief and a shake every 5 s healed
the pet to full in about a minute. The interaction is never blocked, only the
reward; a pet that silently ignored the button would read as broken.

---

## 6. Night

Sleeps 21:00 – 06:00, breathing on a 2 s loop and snoring on two breaths in
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

Ignored completely, a pet dies in **39–48 h of wall clock**. The spread depends
on how much of that window is night, which costs it nothing.

---

## 8. Balance

Passive decay alone is 1 tic per 6 waking hours, and the waking day is fifteen
hours, so the pet needs exactly **2.5 tics of care a day** to hold level. From
`check_balance.c`, quarter tics at the end of each of seven days:

```text
                                     d1 d2 d3 d4 d5 d6 d7
3/day, feed at 08:00 (morning)        5  7  9 11 13 15 17   struggling
3/day, feed at 13:00 (midday)         3  5  6  7  8  9 10   healthy
3/day, feed at 19:00 (evening)        1  5  5  5  5  5  5   healthy
2/day (08:00,19:00), feed morning     7 11 15 19 24 24 24   DEAD
2/day (08:00,19:00), feed evening     3  7  9 11 13 15 17   struggling
1/day (evening only)                  1 13 24 24 24 24 24   DEAD

3/day, full plate at each (12 pips)   1  1  1  1  1  1  1   healthy
3/day, 8 pips at one sitting          8 15 22 24 24 24 24   DEAD
1/day, 8 pips at one sitting          4 24 24 24 24 24 24   DEAD
no care at all                        9 23 24 24 24 24 24   DEAD
```

Three visits a day with an evening meal hold around 1 tic. Two a day either die
or struggle; one a day dies on day three. A visit worth making is all three
actions — a full plate of four, all four hugs, and a play if the cooldown is up.

**Feeding at every sitting is the new ceiling.** Twelve pips across the three
sittings holds the pet at one quarter tic indefinitely — the most attentive
routine in the table, and the reward for spreading meals rather than stacking
them. **Overfeeding is the sharpest way to lose.** Eight pips at one sitting is
worse than not feeding at all, and kills inside four days.

**Feed time is the sharpest lever in the game**, and it only became one when the
sweep stopped cancelling pending poos (§4). Before that fix a tidy owner cancelled
the countdown on every visit and never paid the mess penalty at all, so all three
feed times looked alike and held around 2 tics. They don't: the poo lands twelve
hours after the meal and charges double decay for every waking hour it sits, so a
morning meal costs about 0.67 tic a day more than an evening one. Against a care
budget that nets barely a quarter tic a day, that is the difference between
holding level and dying on day six.

The knobs, if that reads as too sharp: `PET_POO_DELAY_SECONDS` (12 h — the spec's
other reading is 1.5 h, which puts the poo on screen while you are still holding
the watch) and `PET_POO_SECONDS_PER_QT` (currently equal to the passive rate,
which is the spec's "+1 tic every 1 tic").

**A permanently happy pet is not the target.** Holding at Confused or Upset is a
realistic week, and the point is to see the whole range of moods rather than one
contented face.

---

## 9. Sounds

Nineteen sequences, each also flashing `SIGNAL`. Durations are 1/64 s; the
buzzer is a monophonic square wave, so pitch and rhythm are the only tools.

Thirteen are attached to a moment in an animation, as **cues** — a cell, a
segment mask and an edge — rather than timed against it. See §14.

| Sound | Cue | Is |
| --- | --- | --- |
| Snore in / out | loop start; cell 1 `D` lights | High then low, the low landing on the puff |
| Kiss | cell 6 `G` lights | Four chromatic semitones in 1/8 s — a peck, not a trill |
| Eat gulp | cell 7 clears | Mid chirp on the swallow |
| Eat chew | cell 6 `G` lights | One thud per jaw movement — fires three times |
| Barf uh-oh | barf begins | Two falling notes over the wobbling mouth |
| Barf slide | cell 7 `D` lights | Chromatic octave down, travelling with it |
| Poo | cell 9 `B\|C` lights | One short low knock as the pile lands |
| Play small / big | animation begins | Arpeggio up and back; big starts a whole tone up |
| Wake | cell 7 fills, as the mouth opens | A yawn: up into the stretch, then settling |
| Resurrect fade | resurrect begins | Two low notes under the vanishing tombstone |
| Resurrect rise | cell 6 `A` lights | Two octaves of major arpeggio as the pet reassembles |

The other six answer something with no art to cue against — a button that only
moves a counter, or a change of state whose animation *loops*, where a cue would
fire again on every turn. Those are played straight from the code that causes
them.

| Sound | Fires | Is |
| --- | --- | --- |
| Feed | every `LIGHT` press | One short blip, so a burst of presses ratchets |
| Sweep | `ALARM`, when the floor had something on it | A brisk brush downwards |
| Grumble | any interaction that disturbs sleep | Two low, curt notes — the lowest thing the pet says |
| Death | the tic count reaches 6 | Four falling notes, slow enough to land as an ending |
| Mood up / down | the mood changes while you watch | Two notes, the direction of the step |

Three of those deserve their rule spelled out, because each is a moment that can
recur without being a new event:

- **Wake** plays for the morning and for a poke in the night. Only the morning
  yawns; a poke has already grumbled, so `_pet_cue_audible` suppresses the yawn
  by scene, the same mechanism that voices two snores in three.
- **Death** tolls once. The scene survives a visit, so coming back to the same
  tombstone is quiet — and `PET_ANIM_DEAD` loops, which is why this is an edge
  in `_pet_rest` rather than a cue.
- **Mood up / down** sounds only for a change earned in front of you. Arriving to
  a pet that soured while the face was closed is silent: `_pet_enter` seeds
  `shown_mood` before anything settles. Sweeping a clean floor is silent for the
  same reason — the button really has done nothing.

### Silencing it

The pet follows the watch's own **`BTN beep`** setting, in the settings face: set
it to `N` and the pet is silent, `L` or `H` and it plays at that volume. The
`SIGNAL` indicator still flashes on every sound either way, so a muted pet reads
exactly the same.

That is the only mute Movement offers — `SIGNAL` and `ALARM` are soft-or-loud
with no off — and the pet's sounds belong under it: they answer what you just
did, rather than being a scheduled chime. They play at `BUZZER_PRIORITY_BUTTON`
for the same reason, which also means a real alarm is never talked over by the
pet chewing. An earlier build used `BUZZER_PRIORITY_SIGNAL`, and nothing the
wearer could reach would shut it up.

---

## 10. Showcase

Most of the pet's animations are gated behind the clock: angry takes most of a
day of neglect, dead a day and a half, snoring waits for 21:00. Two controls walk
the whole set on demand.

| Press | Does |
| --- | --- |
| `LIGHT` hold 1.5 s | Hold the next animation on screen. Repeat to walk all sixteen — the fourteen drawn animations plus the two floor states — and hand the screen back to the live pet |
| `ALARM` hold 1.5 s | Push the mood up one tic, wrapping past dead back to blissful |

Held animations stay put rather than flashing past once, so one-shots can be
looked at for as long as you like. Stepping the mood is the fast way to see all
five expressions in order, death and resurrection included.

A held one-shot never ends, so `_pet_anim_busy` stays true and the scene timers
that wait on it — feeding and the play ladder — pause for as long as the showcase
is up. That is left as it is: a queued meal already sits through a 3 s settle
before the pet starts eating, so waiting out a browse is the same kind of pause,
and it resumes untouched the moment the screen is handed back. Nothing is lost
and nothing is charged.

Where the walk has got to is kept in `showcase_anim`, separately from "an
animation is currently held" (`showcase_on`). It has to be: the 0.5 s long press
arrives on the way to every 1.5 s hold, and it hands the screen back so the hug
can show its kiss — so by the time the hold lands, nothing is held. An earlier
build read the cursor off `showcase_on`, which meant every hold restarted at
`HAPPY`; with the pet usually happy already, the control looked completely dead.

Leaving the showcase always goes through `_pet_showcase_exit`, which calls
`_pet_rest`. Clearing `showcase_on` on its own is not enough: it only stops
`_pet_layer_tick` forcing a one-shot to loop. A one-shot then ends and rests by
itself, but a looping animation keeps looping, and the two floor states leave the
character layer on `PET_ANIM_NONE`, which the tick skips entirely — so the
showcased animation stayed up for good, and you could walk away from the showcase
with a healthy pet stuck confused, snoring at noon, dead, or gone from the screen
altogether behind a pile it never made. `check_showcase.py` asserts you can
always get back.

A short press, a sweep or a shake clears the cursor as well, so leaving the
showcase properly and coming back starts the walk over.

Both fire their 0.5 s long-press on the way past, since Movement delivers that
first. The short actions do **not** fire — `EVENT_*_BUTTON_UP` only arrives on a
release under half a second — so nothing is fed and nothing is swept. What does
happen:

- **`LIGHT` spends a hug.** Each animation you step through costs one of the four
  daily hugs and −0.25 tic. Walking the whole list exhausts the cap four presses
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
| `check_sounds.py` | Every cue describes a moment the art actually reaches, no cue repeats faster than its own sound can play, no sound is unreachable — whether it is cued or played directly — and every animation has art | **Any animation is redrawn**, or a cue or sound is edited |

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

**What they cannot tell you.** All four prove internal consistency, and all four
passed through a build in which the poo was unreachable: sweeping cancelled the
countdown that produced it, the arrival was only noticed on activate, and the
scene was gated on a freshness window that a twelve-hour timer never lands
inside. `check_layers` proved the status layer owns cell 9's `G B C`, which it
did; `check_sounds` proved the poo cue fires when the pile reaches the floor,
which it does — in an animation that had no way to play. A rule can be
implemented perfectly and still be the wrong rule. Wear it for a day.

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

**The shadow.** `_pet_draw` composites into a ten-byte framebuffer and then
pushes only the cells that differ from what it last wrote, because a frame of
animation typically moves one cell out of ten and repainting a cell costs up to
sixteen SLCD register writes. `_pet_invalidate` marks the shadow stale on
activate and on a low-energy update, since Movement clears the display on a face
switch and the shadow would otherwise describe a screen that no longer exists.
Numbers in §16.

**Motion.** Both halves of the play pause live in one countdown: `play_ticks`
starts at `(deaf + window) × 8` and is heard only once it drops below
`window × 8`. It does not start until `_pet_anim_busy` goes false, so the window
runs from the end of the flourish and the pet is deaf for the animation itself —
which is where most of the stray taps land. A barf keeps `PET_SCENE_PLAYING` and
drops `play_stage` to 0, which reads as deaf whatever the countdown says, so the
scene survives to the end of the barf animation plus one deaf period rather than
being replaced by a new session. See §5 for why counting taps did not work.

**Frame rate** is 8 Hz (`movement_request_tick_frequency` takes powers of two).
Art is drawn at 8 fps, so one exported frame is one frame on the watch and the
conversion is `frames ÷ 8 = seconds`. The decoder collapses identical
consecutive poses into one held frame, which is the `hold` field.

**Persistence** is RAM only. State lives in the face's Movement context,
`malloc`ed once in `pet_face_setup` and never written anywhere else. It survives
face switches and the watch's low-energy sleep, so in daily wear a pet lives
indefinitely; a reflash or a battery pull hatches a new one. That is the whole
design, not a stub — there is no save format, no load path and none planned. All
three ways to lose a pet are deliberate acts that mean opening the watch or
rewriting it, so none of them can happen by surprise.

**One LCD, and no gate on it.** The art is drawn for the classic F-91W panel, so
`_pet_draw` indexes `Classic_LCD_Display_Mapping` directly rather than choosing a
table at runtime. Nothing stops the face building for another panel:

```sh
make BOARD=sensorwatch_pro DISPLAY=custom -j8    # builds and links
```

On the custom LCD the pet is composited from the wrong geometry and will not read
as intended. That is the documented cost of the face, not a bug to be guarded
against — the segment map in §1 is the classic panel's, and supporting the custom
one means redrawing every animation, not picking a different table.

Two stricter versions were tried and both dropped. A bare `#error` on
`FORCE_CLASSIC_LCD_TYPE` refused to compile at all, which — since `pet_face.c` is
listed unconditionally in `watch-faces.mk` — stopped the *whole firmware* from
building for another panel rather than just withholding the face. Wrapping the
file in `#ifdef FORCE_CLASSIC_LCD_TYPE` fixed that by compiling it away to an
empty translation unit, but it bought a build guarantee nobody had asked for at
the price of a face that silently did not exist. Letting it build and saying
plainly that it looks wrong is the smaller claim and the easier one to explain.
The classic build is byte-identical under all three.

---

## 15. Specifications

| Item | Value |
| --- | --- |
| Tick rate on screen | 8 Hz |
| Waking hours | 06:00 – 21:00, three even 5 h sittings |
| Passive decay | 1 tic / 6 waking hours, so 2.5 tics a day |
| Mood resolution | quarter tics, `uint8_t` |
| Food queue | 4 pips on the plate, 4 kept down per sitting |
| Hug allowance | 4 / calendar day |
| Play ladder | 3 rungs, 3 s deaf + 5 s window between them |
| Play buff cooldown | 2 h |
| Mess appears | 12 h after a meal |
| Fatal at | 6 tics |
| Animations | 14, decoded from GIF exports |
| Sounds | 19 — 13 cued to frames, 6 played directly — at `BUZZER_PRIORITY_BUTTON` |
| Muting | Follows the watch's `BTN beep` setting (`N` = silent) |
| Flash | 138,424 text + 2,632 data = 141,056 (57% of 245,760) |
| — of which is this face | **6,096 bytes**, 2.48% of the budget — see §16 |
| RAM | **84 bytes** of context + 8 bytes of Movement's per-face arrays |

---

## 16. What it costs

Measured by building the same firmware with and without `pet_face` in
`movement_config.h` (Mac, GCC 15.3 — the PC's GCC 14.2 reports different totals
for the same source, per the gotcha in `CLAUDE.md`):

| | text | data | bss |
| --- | --- | --- | --- |
| With the pet | 138,424 | 2,632 | 4,608 |
| Without | 132,472 | 2,488 | 4,600 |
| **The face** | **+5,952** | **+144** | **+8** |

**6,096 bytes of flash**, 2.48% of the 245,760 available, leaving ~102 KB free.
Where it goes:

| | bytes |
| --- | --- |
| `pet_face_loop` — the whole state machine, since every `_pet_*` helper is static and gets inlined into it | 2,088 |
| The fourteen frame tables | 1,812 |
| `_pet_draw` — the compositor | 440 |
| `_pet_anims` | 272 |
| The nineteen sound sequences (`data`) | 145 |
| `_pet_sounds`, the dispatch table | 76 |
| The nine cue tables | 65 |
| Everything else (cue engine, waking-time maths, layer engine) | ~1,198 |

Two later passes account for 328 of that. Filling in every silent action — the
nine sounds the first pass left out — cost **200 bytes**, and the three sittings
with their overfeed barf another **128**. Neither was worth economising on.

**92 bytes of RAM.** `sizeof(pet_state_t)` is 84, `malloc`ed once in
`pet_face_setup` and never freed; Movement's `watch_face_contexts` and
`scheduled_tasks` arrays each grow by one entry. That is 0.28% of the 32 KB.

### Battery

**Nothing runs while the face is off screen.** `pet_face_resign` drops the tick
back to 1 Hz and turns tap detection off, and Movement's inactivity timeout
(default **60 s**, `to_interval`) returns to face 0 by itself. So the whole
extra cost is bounded by *how long the pet is on screen* — a handful of minutes
a day — and is exactly zero the rest of the time. The pet never reaches
low-energy mode, because it resigns long before the LE interval.

While it *is* on screen, against the stock clock face:

| | Stock clock | Pet |
| --- | --- | --- |
| Tick | 1 Hz | **8 Hz** |
| Accelerometer | powered down (the default background rate) | **400 Hz, low-noise** |
| LCD writes | on the second | on frame changes only |

The accelerometer is the dominant term by a wide margin, and it is what
shake-to-play costs. It is turned off while the pet is dead, where motion is
ignored anyway.

The display work was cut by keeping a shadow of what is on the LCD and pushing
only the cells that changed. Replaying every animation (`redraw_cost.py`
in `tools/` — it reuses `check_sounds.py`'s parser) gives:

```text
143 frame transitions across all sixteen animations
  before: 10 cells repainted per redraw
  after:   1.05                            -> 90% fewer
```

And in the state the face actually spends its time in — resting on a mood loop —
the saving is close to total, because the blink is carried by the colon, which
is a flag rather than a cell:

| Resting on | Before | After |
| --- | --- | --- |
| Happy / Upset / Angry | 35 cell repaints/s | **0**, plus 3 colon flips/s |
| Confused | 10 /s | **1** /s |
| Dead | 20 /s | **0** |

Each cell repaint is up to sixteen SLCD register read-modify-writes, so this is
most of the per-tick work gone.

**What is not measured here is absolute current.** The µA figures would need the
LIS2DW12 datasheet's consumption table or a bench measurement across the battery;
neither is something this document should guess at. The configuration to look up
is `LIS2DW_MODE_LOW_POWER` + `LIS2DW_LP_MODE_1` + `LIS2DW_DATA_RATE_HP_400_HZ`
with low-noise on, set in `movement_enable_tap_detection_if_available`.
