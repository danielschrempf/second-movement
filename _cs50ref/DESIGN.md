# Design Sketch — Virtual Pet Watch Face

Working design for the whole face, drafted before implementation so every piece has
a home. Constraints below come from the classic F-91W LCD segment map in
`watch-library/shared/watch/watch_common_display.h` and the Movement event system.
Items marked **[DECIDE]** are open choices for Dan.

---

## 1. Screen real estate (classic LCD)

```text
  [0][1]        [2][3]          <- top row: 0-1 letters-ish, 2-3 numbers (0-39 max)
                                   indicators: SIGNAL  BELL  PM  24H  LAP

  [4][5] : [6][7]   [8][9]      <- main line: 4-7 large digits, 8-9 small digits
           ^^^^^^   ^^^^^
            EYES    mouth
```

Per-digit segment quirks that shape what we can draw:

| Pos | Quirk | Consequence |
| --- | --- | --- |
| 0 | full 8-seg | any character |
| 1 | B/C linked, E/F linked | letters mostly OK, most digits wrong |
| 2 | A/D/G all one address, no F | no layered meter here; verticals (B,C,E) independent |
| 3 | full 7-seg | good spot for a 3-level vertical meter (D → D+G → D+G+A) |
| 4 | A/D linked | fine for symmetric shapes |
| 5 | full 7-seg | free |
| 6 | **A/D linked** (left eye) | left eye can't show top-only or bottom-only shapes |
| 7 | full 7-seg + **hardware auto-blink** (right eye) | free blinking, zero CPU, works in sleep |
| 8 | no H; D/E have the autonomous tick/tock animation | free 2-frame "chewing" mouth animation |
| 9 | full 7-seg | free |

Proposed allocation:

- **Eyes = positions 6–7** (the minutes, as planned). The linked A/D on the left eye
  becomes a personality quirk rather than a bug — asymmetric expressions are charming.
- **Mouth = positions 8–9** (the small seconds digits). Smaller than the eyes, which
  reads nicely as a face. The position-8 autonomous animation doubles as free chewing
  during feeding. **[DECIDE: mouth yes/no — pure-eyes minimalism is also valid]**
- **Positions 4–5**: blank in the idle face; used as an animation stage (food items
  appearing and sliding toward the mouth, hearts during petting, "z z" when asleep).
- **Top-left (0–1)**: two-letter status word ("PE" idle, "FD" feeding, "ZZ" asleep…)
  or blank. **[DECIDE]**
- **Top-right (2–3)**: nothing persistent (numbers cap at 39 here). Age lives on a
  stats subscreen instead.
- **Indicators as status flags** **[DECIDE — vs. drawn pips]**:
  BELL = hungry, LAP = waste present, SIGNAL = sick. Crude but always legible, and
  they don't fight with animations for digit space. Alternative: drawn pips in
  positions 4–5 (droppings accumulate as segments), which is cuter but busier.

## 2. Controls

| Input | Action |
| --- | --- |
| Mode short | next face (Movement convention — must keep) |
| Mode long | back to first face (default behavior) |
| Alarm short | **feed** |
| Alarm long | **stats subscreen** (age in days, record age; main line has room for "AGE 123") |
| Alarm really-long (1.5 s) | **reset/rebirth — only on the death screen** |
| Light short | **clean up** (suppress default LED flash) |
| Light long | actual backlight (so the watch still works at night) |
| Accelerometer single/double tap | **pet / play** (hardware only) |
| Simulator fallback | tap is remapped to a button so play is testable in-browser (`#ifdef __EMSCRIPTEN__`) |

## 3. Pet mechanics (all constants tunable in one header)

Stats, small ranges so they pack tightly:

- **hunger** 0–4 — decays ~1 per 4 h; feed sets to 4
- **waste** 0/1 — appears ~2 h after a feed; clean clears it
- **happiness** 0–4 — decays daily; petting/play raises it
- **health** 0–4 — drains while sick; refills while cared for
- **sick** flag — triggered by hunger at 0 for hours, or waste left sitting, or
  happiness at 0 for a full day
- **age** — whole days since birth timestamp; **record age** kept as the high score

Life cycle: birth → normal life → neglect → sick (SIGNAL indicator + red LED alert +
sick chirp) → death after ~24 h untreated → death screen (X X eyes) → long-hold
rebirth (birth chirp, age resets, record updated).

Sleep cycle: pet sleeps 21:00–08:00 (RTC time) **[DECIDE: hours]** — closed eyes,
no decay-nagging overnight, interactions wake it briefly.

## 4. Time & persistence model

- **Foreground**: EVENT_TICK drives animation; stats recomputed from timestamps.
- **Background**: no per-minute work. A **once-per-hour background task** (via the
  `advise` hook) checks for state transitions that deserve an alert chirp
  (got sick, died). Everything else is computed lazily on activate — the classic
  Tamagotchi trick: store *when* things happened, derive stats from elapsed time.
- **Persistence**: 2 of the 4 spare RTC backup registers —
  one for the birth timestamp (u32), one packing stats + last-fed/cleaned/pet
  offsets in hours (5–6 small fields + flags). Survives deep sleep and reset.
  The record age is written to a littlefs file only on death (flash writes stay rare).
  Survives battery swaps too.
- **Low-energy mode**: the face keeps working — EVENT_LOW_ENERGY_UPDATE redraws
  once a minute (sleeping pet + system sleep animation in position 8). The pet can
  be the default face without wrecking battery life.

## 5. LED language (used sparingly — red is ~4.5 mA)

Brief flashes (1–2 s) only on events, never continuous:

- **green** = positive feedback (fed, cleaned, petted, wake-up greeting)
- **red** = attention (became sick, critical hunger)
- **blue / mixes** = flavor accents (sleepy purple, birth rainbow…) **[DECIDE: how much]**

## 6. Chirp sketches (six, via `movement_play_sequence`)

Character sketches to compose against in the simulator:

- **happy** — quick rising major figure (C6→E6→G6), short and bouncy
- **sad** — two-note falling minor sigh
- **tired** — slow low two-note yawn with a rest between
- **sick** — wobbly semitone alternation, weak and queasy
- **death** — slow descending line ending on a long low note
- **birth** — tiny fanfare: rising figure + a held top note with a trill

## 7. Animation engine (the foundation to build first)

Data-driven so authoring animations = writing tables, not code:

```c
typedef struct {
    uint8_t left_eye;    // 7-seg mask (bit0=A ... bit6=G), pos 6
    uint8_t right_eye;   // pos 7
    uint8_t mouth_a;     // pos 8 (if mouth adopted)
    uint8_t mouth_b;     // pos 9
    uint8_t stage_a;     // pos 4 (props: food, hearts, zzz)
    uint8_t stage_b;     // pos 5
    uint8_t ticks;       // frames held, at the animation tick rate (8 Hz)
} pet_anim_frame_t;

typedef struct {
    const pet_anim_frame_t *frames;
    uint8_t count;
    bool loop;
} pet_anim_t;
```

- Renderer: a `pet_draw_digit(position, mask)` helper that walks the segment map
  and calls `watch_set_pixel`/`watch_clear_pixel` — full control, no charset limits.
- Engine: one active animation + frame index + countdown, advanced on tick;
  returns to the mood-appropriate idle loop when a one-shot finishes.
- Idle blinking is free: position 7's hardware auto-blink, while position 6 stays lit.
- Tick rate: 1 Hz idle (battery), bump to 8 Hz only while an animation plays,
  back to 1 Hz after (`movement_request_tick_frequency`).

## 8. Multi-species architecture (design in now, ship one animal first)

The engine/logic split makes multiple animals nearly free *if* the seam is
designed in from the start:

- The state machine never references animations directly — it asks the current
  **species** for a named animation slot. A species is a const struct of pointers:

```c
typedef struct {
    const pet_anim_t *anims[PET_ANIM_COUNT];  // idle, blink, eat, play, sleep,
                                              // sick, death, birth, ... every slot filled
    const int8_t *const *chirps;              // the six tunes, per-species voice
    pet_species_config_t config;              // sleep hours, decay tuning, LED palette
} pet_species_t;
```

- The **animation slot list is the contract**: logic is written once against the
  slots; each animal is just a complete set of tables. Finalize the slot list
  during the drawing phase — the states Dan draws *define* the vocabulary.
- Cost is trivial: a frame is ~7 bytes; a full animal (say 20 animations × 8
  frames) is on the order of 1 KB. We have >100 KB of flash headroom.
- Species choice at egg/rebirth (button select or alternating), stored in a
  couple of bits of the packed persistence register.
- Notable option: **orientation can be a species trait.** The mask→pixel engine
  doesn't care which way art reads — one upright animal with expressive eyes and
  one sideways animal with the big Muppet mouth could coexist in the same
  firmware, which turns the upright-vs-sideways question from either/or into
  both/and. **[DECIDE: v1 = one animal (recommended); second species as the
  stretch goal after M5]**

## 9. Build order

1. **M1 — render + idle**: frame engine, draw helper, resting/blink/sleep idles in sim
2. **M2 — needs**: hunger/waste/timestamps, feed + clean buttons, indicator flags
3. **M3 — feelings**: happiness/sickness/health, LED language, sleep cycle
4. **M4 — sounds**: compose and wire the six chirps
5. **M5 — mortality**: death, rebirth, age + record, persistence registers + littlefs
6. **M6 — play**: accelerometer tap on hardware, sim button fallback
7. **M7 — polish**: custom-LCD variant via fallback strings, tuning pass, devlog wrap-up
