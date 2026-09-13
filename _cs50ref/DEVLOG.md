# CS50x Final Project Devlog — Sensor Watch Virtual Pet

A running log of the process of building a tamagotchi-style virtual pet watch face
for the Sensor Watch, on top of the Second Movement firmware. Research, scaffolding,
and code assistance by Claude Code (AI pair programmer), directed by Dan.

---

## Session 1 — 2026-08-16 — Environment survey & feasibility

### Hardware identified (from photos in this folder)

- **Main board:** Sensor Watch Pro, rev SWAT-C1-06 ("Sensor Watch Pro!" on silkscreen),
  Atmel/Microchip ATSAML22J18 (ARM Cortex-M0+). Build target: `BOARD=sensorwatch_pro`.
  - Silkscreen confirms an RGB LED (Red PA12, Blue PA13, Green PA22), piezo on PA27,
    on-board temp sense (PA03) and light sense (PA04), and a 9-pin flex connector
    (I2C on PB30/PB31, plus A1–A4).
- **Accelerometer:** Oddly Specific Objects Sensor Watch Accelerometer, OSO-SWAB-B1-02.
  ST **LIS2DW12** on I2C at address 0x19; INT1/INT2 broken out to A3/A4. Plugs into the
  9-pin connector.
- **LCD:** Dan has both LCDs. Primary target is the **classic** F-91W LCD
  (`DISPLAY=classic`); a custom-LCD variant may come later for sharing, so the face
  should use `watch_display_text_with_fallback()` where practical to keep that door open.

### Firmware architecture (Second Movement)

- A watch face = one `.c`/`.h` pair implementing 4 callbacks (+1 optional):
  `setup` / `activate` / `loop(event, context)` / `resign` / `advise`.
  Template in `template/`, plus a generator script `template/watch_face.py`.
- Registering a face touches three files: `watch-faces.mk` (source list),
  `movement_faces.h` (include), `movement_config.h` (the `watch_faces[]` rotation).
- The `loop` receives typed events: ticks (1–64 Hz on request), button down/up/long
  (0.5 s)/really-long (1.5 s) for all three buttons, plus `EVENT_SINGLE_TAP` /
  `EVENT_DOUBLE_TAP` / `EVENT_ACCELEROMETER_WAKE` from the accelerometer, and
  once-per-minute background/low-energy events.

### Feature feasibility for the pet — all green

| Wanted feature | Supported by |
| --- | --- |
| Eyes in the minutes digits | `watch_display_text(WATCH_POSITION_MINUTES, "00")` — positions 6–7 of the main line; raw per-segment control also available via `watch_set_pixel(com, seg)` and the display mapping tables in `watch_common_display.h` |
| Pips for food/waste | Any other digit positions + indicator segments (`watch_set_indicator`: signal, bell, lap, etc.) |
| Mood/sickness colour | `movement_force_led_on(r, g, b)` — Pro board has true RGB, 0–255 per channel |
| "Pet" via accelerometer | `movement_enable_tap_detection_if_available(true)` then handle `EVENT_SINGLE_TAP`/`EVENT_DOUBLE_TAP` in loop; wrist-motion wake via `EVENT_ACCELEROMETER_WAKE` |
| Feed / clean buttons | Alarm & Light button events (Light's default LED flash can be suppressed) |
| Long-hold reset after death | `EVENT_*_REALLY_LONG_PRESS` (1.5 s hold) |
| Chirps (happy/sad/tired/sick/death/birth) | `movement_play_sequence()` — non-blocking note sequences: `{note, duration, ..., 0}` arrays; many example tunes in `movement_custom_signal_tunes.h` |
| Days-lived counter surviving sleep | RTC backup registers via `movement_claim_backup_register()` (4 spare × 32 bits) or littlefs filesystem; or store timestamps and compute lazily |

### Display quirks to design around (classic LCD)

- Main clock line is positions 4–9; **minutes = positions 6 and 7**.
- Position 6's top (A) and bottom (D) segments share one address — they switch on/off
  together. Eye designs must respect this on the left eye.
- Position 7 (right eye) is fully independent AND supports hardware auto-blink
  (`watch_start_character_blink`) — free blinking-eye animation, even in sleep mode.
- Position 8 has the autonomous two-segment tick/tock animation (sleep animation).
- Several other positions share segment addresses (1B/1C, 1E/1F, 2A/2D/2G, 4A/4D) —
  matters for pip/status glyph design outside the minutes area.

### Accelerometer notes

- Firmware auto-detects the LIS2DW12 at boot (`has_lis2dw`); background it runs at
  1.6 Hz stationary/motion detection (very low power).
- Tap detection cranks the chip to 400 Hz — power hungry. Plan: only enable tap
  detection while the pet face is in the foreground (activate/resign), or gate it
  behind a short "play session".
- **The browser simulator has no accelerometer at all** — tap/pet features must be
  tested on hardware. Plan: compile-time or button fallback for testing in the sim.

### Build environment (this machine: Windows 11)

- Nothing usable installed natively: no `arm-none-eabi-gcc`, no `make`, no `emcc`.
- **WSL2 Ubuntu is installed** (currently stopped) and Docker Desktop exists; the repo
  ships a devcontainer (Ubuntu + ARM GCC 10.3 + emscripten) as a fallback.
- **Decision: use WSL Ubuntu** — simplest for iterating. Repo is at `/mnt/d/code/second-movement`.
- **Blocker found: git submodules were never initialized** (`gossamer/` is empty —
  that's the HAL that provides the whole make system). Fix: `git submodule update --init --recursive`.

### The planned workflow

1. One-time setup (WSL): `sudo apt install gcc-arm-none-eabi build-essential python3 emscripten`
   and init submodules.
2. Iterate in the browser: `emmake make BOARD=sensorwatch_pro DISPLAY=<type>`, then
   `python3 -m http.server -d build-sim` → <http://localhost:8000/firmware.html>
3. Flash hardware: `make BOARD=sensorwatch_pro DISPLAY=<type>`, double-tap the reset
   button on the board → `WATCHBOOT` drive appears in Windows → copy `build/firmware.uf2` onto it.
4. CI parity: GitHub Actions builds every board × display combo on push, so the fork's
   Actions tab doubles as a build check.

### Next steps

- [x] Confirm LCD type — classic primary, custom later (Dan has both)
- [x] Init submodules, install WSL toolchain, verify a stock firmware build
- [x] Verify simulator build + run in browser
- [x] Flash firmware.uf2 to the watch once to prove the full pipeline — verified on hardware, pet face placeholder displays on the watch
- [x] Scaffold `pet_face` from the template, with placeholder eyes on screen
- [x] Design the pet — Dan's spec, Session 4

---

## Session 2 — 2026-08-16 — Toolchain setup, first builds, face scaffold

### Environment setup (WSL Ubuntu 26.04)

- Initialized all git submodules (gossamer, tinyusb, littlefs, utz) — cloned from
  inside WSL so they get LF line endings (Windows git here has `autocrlf=true`).
- Installed `gcc-arm-none-eabi` 14.2.1 + GNU Make 4.4.1 via apt.
- **First hardware build succeeded**: `make BOARD=sensorwatch_pro DISPLAY=classic`
  → `build/firmware.uf2` (129 KB text — lots of flash headroom).
- Note: this WSL distro's default user is root; emsdk lives at `/root/emsdk`.

### Problem 1: Ubuntu's packaged emscripten is broken for this project

The simulator build died at link time: `llvm-objcopy-19: error: 'firmware.wasm':
zero length section`. Ubuntu's `emscripten` package is patched to use the system
LLVM 19, and its objcopy can't strip debug sections from the generated wasm.

**Fix:** removed the apt package and installed the official **emsdk** (same
toolchain the repo's CI uses) into the WSL home directory; added
`source /root/emsdk/emsdk_env.sh` to `.bashrc` so `emmake` always resolves to it.

### Problem 2: upstream bug in the face generator script

Scaffolded the face with the repo's own tool
(`python3 template/watch_face.py complication pet`), which created
`watch-faces/complication/pet_face.c/.h` and registered them in `movement_faces.h`
and `watch-faces.mk`. But the next build failed with
`watch-faces.mk:65: *** missing separator`.

Root cause: `update_include_file()` in `template/watch_face.py` rewrites files
in place (`seek(0)` + `writelines`) **without truncating**. Our working tree had
CRLF line endings (Windows checkout); running the script under Linux Python
rewrote every line as LF, shrinking the file — so stale bytes from the old, longer
file were left dangling past the new end. Both edited files had garbage tails.
Cleaned both tails by hand. *Possible upstream contribution: add `file.truncate()`
after the write.*

Also added `pet_face` to the rotation in `movement_config.h` (right after `clock_face`).

### Problem 3: revised diagnosis — the simulator failure was a parallel-make race

With emsdk, the sim build failed differently: objcopy couldn't find `firmware.wasm`
at all. Real root cause: gossamer's emscripten `all` target links BOTH
`firmware.elf` and `firmware.html`, and each emcc link emits a `firmware.wasm`
side file — under `make -j8` the two links run concurrently and clobber each
other's wasm. (The original "zero length section" error was most likely the same
race caught mid-write, so the apt-emscripten diagnosis in Problem 1 was probably
only half right. emsdk is still the better toolchain — it's what CI uses.)

**Fix/workflow:** build only the HTML target, which is all the browser needs:
`emmake make -j8 BOARD=... DISPLAY=... build-sim/firmware.html`

### Problem 4: no header dependency tracking in the build system

Adding `pet_face` to `movement_config.h` and rebuilding produced a bit-identical
firmware: `pet_face.o` compiled, but `movement.o` was NOT rebuilt (its `.d`
dependency files never get generated — gossamer's `-include $(wildcard *.d)`
expands to nothing when no `.d` files exist yet). The stale `movement.o` never
referenced the pet face, so `--gc-sections` silently dropped it from the link.
Verified the fix with `arm-none-eabi-nm build/firmware.elf | grep pet_face`.

**Workflow rule: after editing any header, `touch` the .c files that include it
(usually `movement.c`) or do a clean build.** Editing only `.c` files is safe.

### Where things stand

- `build/firmware.uf2` — flashable firmware containing the pet face placeholder
  (title "PET", eyes "00" in the minutes digits), verified present via `nm`.
- `build-sim/firmware.html` — browser simulator with the same face; serve with
  `python -m http.server 8000 -d build-sim` and open
  <http://localhost:8000/firmware.html>.
- Both builds run from WSL Ubuntu; emsdk auto-loads via `.bashrc`.

### Milestone: full pipeline verified on hardware

Flashed `build/firmware.uf2` to the watch (double-tap reset → WATCHBOOT drive).
The pet face placeholder (title + "00" eyes in the minutes digits) displays on the
physical watch. Build → simulate → flash all work end to end.

### Build cheat-sheet (from WSL, in /mnt/d/code/second-movement)

```sh
# hardware
make BOARD=sensorwatch_pro DISPLAY=classic -j8
# simulator (HTML target only — avoids the -j link race)
emmake make -j8 BOARD=sensorwatch_pro DISPLAY=classic build-sim/firmware.html
# flash: double-tap reset on the board, then from Windows copy
#   build\firmware.uf2  onto the WATCHBOOT drive
```

---

## Session 3 — 2026-08-16 — Whole-design sketch

> **Superseded.** The design sketched in this session was replaced by Dan's own
> spec in Session 4. `DESIGN.md` has been removed; its hardware notes live on in
> [SEGMENT_MAP.md](SEGMENT_MAP.md).

Chose to sketch the complete design before writing more code. Drafted
`DESIGN.md`: screen allocation around the classic LCD's shared-segment
quirks (eyes in positions 6–7, optional mouth in 8–9, positions 4–5 as an
animation stage), full button map, pet mechanics with lazy timestamp-based decay,
persistence via RTC backup registers + littlefs for the record age, LED language,
chirp character sketches, a data-driven animation engine design, and a 7-milestone
build order. Several decision points are marked [DECIDE] for review before M1.

### Source material research

Researched the original 1996 Tamagotchi (P1) — mechanics and hardware — as
background, written up in [TAMAGOTCHI_NOTES.md](TAMAGOTCHI_NOTES.md) with sources.
Standout finding: the P1 runs a 4-bit Epson E0C6S46 at 32.768 kHz (a watch-chip
family — the same frequency our SAM L22 uses for its RTC crystal) with 320 bytes
of RAM, but drives a 32×16 dot-matrix display. Our watch has ~1000× the compute
and ~100× the memory but a tenth the display resolution — the design constraints
are inverted, which motivates leaning on eye animation, LED color, and sound for
personality instead of sprites. Notes include a borrow/differ list feeding into
DESIGN.md (care-mistake mechanic and stage-gating in; sprites and scolding out).

### Segment constraint reference + the sideways-face question

Added a labeled LCD schematic from the Sensor Watch docs
([positions-labeled.png](positions-labeled.png)) and compiled every segment-tying
constraint from the driver's display mapping into
[SEGMENT_MAP.md](SEGMENT_MAP.md). Key code finding: the classic LCD's colon is a
single electrical segment — both dots switch together — which frames the open art
direction question: sideways face with colon dots as (blink-only) eyes and a full
7-segment digit as a highly expressive mouth, vs. upright face with expressive
eyes in positions 6–7 and a small mouth in 8–9. Both use the same frame engine;
decision pending.

### Multi-species idea

Question raised: could there be two "animals" — two sprite sets over the same
logic? Answer: yes, cheaply, if the seam is designed in from day one. Added a
multi-species section to DESIGN.md: species = const struct of animation-table
pointers + chirp set + tuning config; the state machine only ever asks the
current species for a named animation slot. A full animal's animation data is
~1 KB against >100 KB of flash headroom. Orientation (upright vs sideways) can
even be a per-species trait, turning the art-direction dilemma into both/and.
Plan: ship v1 with one animal but keep the indirection; second species is the
stretch goal. Next: Dan is drawing the animation states on paper — the drawn
state list will become the engine's animation-slot contract.

---

## Session 4 — 2026-09-03 — Dan's design, restructured face, second machine

### Second development machine (Mac)

Set up the Mac as a second dev box: ARM GNU Toolchain 15.3.rel1 (from ARM's
tarball, into `~/`) and emsdk 6.0.9 (`~/emsdk`), both on `PATH` via `.zshrc`.
Hardware and simulator builds verified. Findings that matter across machines are
in the repo-root `CLAUDE.md`: the two GCCs differ (15.3 here vs apt's in WSL) so
expect different flash sizes and warning sets; header edits still need a
`make clean`; `make install` can flash directly from the Mac since `uf2conv.py`
scans `/Volumes`.

Repeatable setup, for the next Mac:

```sh
# ARM GCC — ARM's own tarball. (brew's gcc-arm-embedded cask is the same build
# but ships as a .pkg that needs a sudo password.)
curl -L -o /tmp/arm.tar.xz "https://gitlab.arm.com/api/v4/projects/tooling%2Fgnu-toolchains-for-arm/packages/generic/gnu-toolchain/15.3.rel1/arm-gnu-toolchain-15.3.rel1-darwin-arm64-arm-none-eabi.tar.xz"
tar -xf /tmp/arm.tar.xz -C ~

# emsdk — what CI and the WSL box use. Not brew's emscripten.
git clone --depth 1 https://github.com/emscripten-core/emsdk ~/emsdk
~/emsdk/emsdk install latest && ~/emsdk/emsdk activate latest

# ~/.zshrc (sourcing emsdk_env.sh costs ~0.1 s per shell)
export PATH="$HOME/arm-gnu-toolchain-15.3.rel1-darwin-arm64-arm-none-eabi/bin:$PATH"
source "$HOME/emsdk/emsdk_env.sh" >/dev/null 2>&1
```

Gotcha met along the way: non-login shells (scripts, Claude Code's Bash tool)
don't read `.zshrc`, so a build from one fails with `arm-none-eabi-gcc: command
not found` — export `PATH` inline or `source ~/emsdk/emsdk_env.sh` first.
Apple's stock `make` 3.81 needed no replacement.

### The design is now Dan's

Dan wrote the pet's actual design as pseudo code in
[CS50x Final Project.md](CS50x%20Final%20Project.md): a single "tic" mood meter
(Happy / Confused / Upset / Angry / Dead at 1/2/3/4/6 tics, +1 tic per 6 h of
rest), buffs and debuffs (play, hug, feed, poo, sleep disturbance), a button map
(Light short/long = feed/hug, Alarm short/long = sweep/resurrect, shake = play),
a 14-animation checklist (all sketched in Procreate Dreams, still to be
converted to segment values), four sounds, a 21:00–05:00 sleep window, a
4-pip feed queue, and a nausea counter for over-shaking.

The Session 3 sketch (`DESIGN.md`) and the placeholder scaffold in `pet_face.c`
were retired so nothing from that first pass lingers.

### Feasibility check against Second Movement

Everything in the spec maps onto the platform, with one correction:
`EVENT_ACCELEROMETER_WAKE` (motion over threshold) is never delivered — its
callback is commented out at `movement.c:1161`. Tap detection
(`EVENT_SINGLE_TAP` / `EVENT_DOUBLE_TAP`) does work and a shake fires it
repeatedly, so shake-to-play and the nausea count both ride on tap events. The
simulator has no accelerometer, so it stands in Alarm-long (while alive) for a
shake. The button map is collision-free because Movement only emits
`*_BUTTON_UP` for presses under 0.5 s and `*_LONG_PRESS` at 0.5 s.

### pet_face.c restructured to the spec

Rewrote `pet_face.h` / `pet_face.c` as the working skeleton:

- Tics are stored as integer quarter-tics (0–24) — no floats.
- Frames are per-position 7-segment masks (`SEG_A | SEG_B ...` for positions
  0–9, plus colon/indicator flags and a hold time), which is the form the
  Procreate sketches convert into. A renderer walks the LCD mapping table and
  lights tied segment pairs if either half is asked for.
- Animation engine: one playing animation, a 4-deep queue for sequences like
  wake → mood → poo, and a "rest" that loops the mood animation (the spec's
  "blink"). Animations without frames yet draw their 6-letter label so the
  state machine is testable in the simulator before any art exists.
- Time off-screen is caught up lazily on activate: passive decay, missed-feed
  penalty, poo arrival, unswept-poo penalty, daily hug-cap reset.
- Scenes: idle, asleep, night-awake, feeding, playing, dead — with the feed
  settle/pip timers, the 5 s play window, and the day-part tree from the spec.
- A debug HUD (top-left scene code, top-right quarter tics) behind
  `PET_DEBUG_HUD`.

Every tunable is a `#define` at the top of `pet_face.h`. Open questions are
marked `DECIDE` in the code and unfinished work `TODO`: the animation frames,
the four sounds, persistence across resets (RTC backup registers), showing the
food queue, and the spec's ambiguities (poo delay 0.25 vs 2 tic, what triggers
"Play big", whether missed-day and passive decay stack, when a disturbed pet
falls back asleep).

Both builds pass on the Mac; `pet_face` verified in the ELF with `nm`.

---

## Session 5 — 2026-09-12 — Orientation settled: the face reads sideways

### Triage of the open questions

Came back to the project and took stock. Hardware build is green on the PC
(GCC 14.2 in WSL): 131,744 text + 2,044 data = 133,788 of the 245,760 byte
budget, ~54%, with `pet_face` verified in the ELF. Nothing is broken; what
remains is decisions and art.

Sorting the `TODO` / `DECIDE` markers by what they block put one item on top
that wasn't tracked in the code at all — the Session 3 "sideways vs upright"
question, which lived only in this devlog as "decision pending" while gating
every animation table and the poo overlay.

A few balance findings came out of the same pass, now recorded for later:

- **Passive decay never pauses overnight.** `_pet_catch_up` is pure wall clock
  with no day-part check, but the pet sleeps 21:00–05:00 and any interaction
  then costs +0.25 with no buff. So the pet accrues 5.3 of its 24 quarter-tics
  every night with no way to offset them. Nothing in the spec addresses this.
- The **poo delay contradiction** (`0.25 tic after every feed` vs
  `poo countdown (2 tic)`) is the highest-leverage number in the file: an
  unswept poo doubles the decay rate, so at the current 1.5 h a fed-then-
  abandoned pet dies in ~19 h instead of the ~36 h pure passive decay implies.
- **Barfing costs nothing**, which leaves the nausea counter with no teeth.
- Backup registers 0–6 are all free — no other face in `movement_config.h`
  claims any — so persistence is unblocked work. Note that
  `movement_claim_backup_register()` returns `0` on exhaustion, which is also a
  valid register number, so the result can't be error-checked; claim early.

### The decision: sideways

Dan's call — the animations are all drawn for the rotated face and upright was
never really in play. So the watch is read turned 90° clockwise: the pet stacks
`4, 5, (colon = eyes), 6, 7` down the screen, with the small `8` and `9` off to
one side.

Wrote the consequences into the places that need them rather than leaving them
in a doc nobody reads while authoring frames:

- [pet_face.h](../watch-faces/complication/pet_face.h) now carries the rotated
  segment geometry beside the upright bit names (`A` is the right vertical, `D`
  the left, `G` the centre, `E`/`F` the top edge, `C`/`B` the bottom), the handy
  mouth shapes, and the per-cell quirks that constrain the art.
- The example frame table in [pet_face.c](../watch-faces/complication/pet_face.c)
  was recast sideways and now shows `PET_FRAME_COLON` carrying the eyes.
- The poo overlay `TODO` narrowed: 8 and 9 are the natural home, beside the pet
  rather than on it, so no mood frame has to reserve space for it.
- [SEGMENT_MAP.md](SEGMENT_MAP.md) records the decision at the head of the
  sideways section.

Two hardware findings fell out of writing that up, both specific to the sideways
design. The colon — now the eyes — is the one element on the classic LCD that
*cannot* blink autonomously, so every blink in the spec is a CPU-drawn frame and
the pet stops blinking in sleep mode. And position 7, the only position that
*can* blink in hardware (and keeps going in STANDBY, which is tempting for
snoring), takes a **character**, not a segment mask, from a fixed list of shapes
that avoid its segment B — so using it means giving that cell over entirely.

Next: converting the Procreate Dreams sketches into frame tables, which needs
the sketches in a readable form. Persistence is the parallel track that needs no
decisions.

### Every spec ambiguity settled

Worked through the whole `DECIDE` list in one pass. There are now **zero** left
in the face — what remains is `TODO`s, i.e. work rather than questions.

The four that changed the game:

- **Decay pauses overnight.** Neither passive decay nor an unswept poo accrues
  between 21:00 and 05:00. You are not neglecting a pet that is in bed, and the
  +0.25 disturb penalty already covers waking it. The daily cost drops from 16
  quarter-tics to 10.7, and a fully ignored pet now dies in 38–46 h of wall
  clock instead of 30 — the spread depends on what time you abandoned it, since
  walking away at 21:00 buys it a free night.
- **The poo delay is 12 h**, resolving the spec's contradiction in favour of
  Feed()'s explicit "poo countdown (2 tic)" over the Buffs list's "0.25 tic
  after every feed". At 1.5 h a poo was on screen practically whenever you had
  fed, and since an unswept poo doubles the decay rate, feeding at 20:00 cost
  +5 against the −4 the food gave: the pet was better off not being fed in the
  evening. At 12 h the same feed lands its poo at 08:00 the next morning.
- **Barfing costs.** Shaking past `PET_NAUSEA_LIMIT` now hands back the play
  buff and adds another 0.25 on top, so over-shaking ends up a quarter-tic worse
  than never having played. Previously the nausea counter had no teeth at all —
  the punishment for shaking the watch senseless was an animation.
- **The missed-day penalty keeps stacking** on passive decay, the literal
  reading of the spec and a 25% surcharge for not feeding at all in 24 h.

The rest, recorded inline at the point each applies: the hug cap resets on the
calendar day (per-visit would be gamed by switching face and coming back); the
pet is still kissed past the cap, because a button that silently does nothing
reads as broken; poo does not survive death; `PLAY_BIG` rewards a lively session
that stayed under the nausea limit; the pet nods off and wakes while you watch
if the face is open across the boundary; and the inactivity timeout still
returns to the clock.

That last one turned out to be load-bearing rather than cosmetic: resigning the
face is what disables tap detection, so it is the only reason an arm rolling
over in bed can't shake the pet awake all night. Worth remembering before anyone
"improves" the face by making it stay put. (The deadline is a user setting —
60, 120, 300 or 1800 s — not the fixed minute the old comment claimed.)

### Implementation notes

Decay is now charged in **waking seconds** rather than wall seconds, which needs
a little care since the two don't divide evenly. `_pet_awake_seconds(t)` returns
the waking seconds from the epoch to local time `t`; it's monotonic, so the
waking time in any span is just the difference of its ends, however many nights
fall in between. Leftover seconds that don't add up to a whole quarter tic carry
in `awake_residual` / `poo_residual` instead of being rounded away — otherwise
a diligent owner could outrun decay just by opening the face often.

Validated the accounting against a standalone harness before trusting it: ten
hand-checked spans (whole days, all-night spans, spans straddling both
boundaries), plus monotonicity and additivity over 400 generated spans. All
pass. The one known inaccuracy is a span straddling a DST change, which applies
the current UTC offset to both ends and so is off by an hour, once — at most two
thirds of a quarter tic, twice a year.

Two latent bugs fixed while in there. An eat animation longer than
`PET_FEED_PIP_SECONDS` would have been truncated by the next pip, so scene
timers now wait on `_pet_anim_busy()` rather than cutting a one-shot animation
off part-way. And a pet resurrected on the same day-of-month it was last hugged
would have found its hug cap already spent, since `hug_day` only stores the day
of the month; resurrection now resets the hug count along with everything else.

Snoring moved from a single beep on nodding off to a repeat every
`PET_SNORE_PERIOD_SECONDS`, so the pet is audibly asleep the whole time you're
watching it. Sweeping restarts the mood animation as an acknowledgement, since
the checklist has no sweep animation — except while the pet is asleep, where
that would cut off the snore.

Hardware and simulator builds both pass: 132,048 text + 2,044 data = 134,092,
up 304 bytes on the session's starting point, 55% of the flash budget.

### The play exploit, and what the balance actually feels like

Found one more hole while checking the finished numbers: **play was farmable.**
`_pet_on_motion` granted the full −0.5 tic every time a new 5 s window opened,
and a single shake per window never accumulates nausea — so shake, wait five
seconds, shake, and the pet went from death's door to blissful in about a
minute, no barf, no penalty. Hugs are capped and feeding is self-limited by poo;
play was the one uncapped source of relief, which made the whole mood meter
optional for anyone who noticed.

Fixed with a **2 h cooldown on the buff, not on the interaction**: the pet always
plays along and the animation always runs, but the tics only come off once per
cooldown. That keeps shaking spontaneous and always available — it's the fun
one — while capping it at ~8 buffs per waking day. Barfing gives back only a
buff that was actually granted, and leaves the cooldown spent: a pet that has
just been made sick isn't in the mood to go again.

Then simulated a week of care against the finished rules, which is the only way
to answer "is this actually playable". Quarter tics at 23:00 each day, death at
24:

```text
                                    d1 d2 d3 d4 d5 d6 d7
  3/day, feed at 08:00 (morning)      5  6  6  7  8  8  9   healthy
  3/day, feed at 13:00 (midday)       3  5  5  6  7  7  8   healthy
  3/day, feed at 19:00 (evening)      1  5  5  6  7  7  8   healthy
  2/day (08:00, 19:00), feed morning  7 10 12 15 18 24 24   DEAD (day 6)
  2/day (08:00, 19:00), feed evening  3  7  9 12 15 17 24   DEAD (day 7)
  1/day (evening only)                1 14 24 24 24 24 24   DEAD (day 3)
  no care at all                      9 24 24 24 24 24 24   DEAD (day 2)
```

Reading it: three interactions a day is the sustainable routine, and it
**converges** rather than drifting — the pet sits near 0 (Happy) whenever you
actually look at it, and the 8–9 in the table is its worst moment, late evening
after the longest gap, just into Confused. Two a day is survivable for most of a
week and then isn't. Feeding late beats feeding early by about a tic a day,
because a morning feed drops its poo at 20:00, after the last check-in, where it
sits until morning.

That is a demanding pet, deliberately: it comes straight out of the spec's own
numbers (1 tic per 6 h, dead at 6 tics). The sleep pause already took it from
"dies in 30 h" to "dies in 38–46 h". If it wants softening later the dials are
the hug cap, the play cooldown, and the poo rate — in that order, since the poo
rate is the one the spec actually pins down ("adds +1 tic every 1 tic").

Hardware and simulator builds both pass: 132,144 text + 2,044 data = 134,188,
55% of the flash budget, ~108 KB free.

### Why the pet is demanding, on purpose

Dan's call on the difficulty above, and the reasoning is better than
"spec-faithful": **the difficulty is the content.** There are fourteen
animations in the checklist, and a pet that is comfortably Happy all week only
ever shows you one of them. A pet that drifts through Confused, Upset and Angry
between check-ins — and occasionally dies and gets resurrected — is the one that
actually exercises the art. Neglect is how the animation range gets seen.

So the balance stays where the week simulation put it: three interactions a day
to hold steady, two survives most of a week, one is fatal by day three. No
softening. If anything the mood thresholds are the interesting dial later —
they control how much of the range you see per unit of neglect, independently of
how fast the pet dies.

### Persistence: not needed, and here's the proof

Raised persistence as the obvious next piece of work, then Dan questioned the
premise — flashing this watch means taking it apart, so how often does the pet
really get reset? Checking properly, the answer is: almost never.

The pet's state is a `malloc`'d struct in RAM. RAM survives switching faces, and
— the part that matters — it survives Movement's low-energy mode, which calls
`watch_enter_sleep_mode()`. That mode disables pins and peripherals but leaves
RAM intact. The mode that *would* wipe it is BACKUP, which the library documents
as turning "off the RAM, obliterating your application's state" — and
`watch_enter_backup_mode()` is never called anywhere in this firmware. There's
also an erratum (Reference: 15010) that makes BACKUP impractical on current
SAM L22 silicon, so nothing is likely to start calling it.

So the pet is lost only to a battery pull, a flat battery, a reflash, or a
crash. Reaching the reset button means opening the case; there is a software
route to the bootloader (`shell_cmd_list.c` has a "reboot to UF2 bootloader"
command) but the shell runs over USB, so that needs the case open too. None of
it happens by accident on the wrist, and hatching a fresh pet after a battery
change is a fair reading of the fiction.

Decision: **no persistence.** The `TODO` is gone; `_pet_load` / `_pet_save` stay
as no-op hooks with the reasoning recorded at the definition, along with the two
routes if it ever changes (backup registers 2-6 — five, not the seven claimed
earlier today, since Movement reserves 0 and 1 — or a littlefs file in RWWEE).

Worth noting how close this came to being a day of work on a non-problem. The
`TODO` had been sitting in the file since Session 4 asserting that "a reset
hatches a fresh one", which is true but says nothing about how often a reset
happens. Checking the frequency rather than the mechanism is what killed it.

### A preview harness, before the art

Built the development controls that make converting sketches practical, on the
principle that the tool comes before the work it serves.

The problem it solves: most of the fourteen animations only appear when the
clock says so. Checking that the Angry face reads right means neglecting the pet
for most of a day; the dead one, a day and a half; snoring, waiting until 21:00.
That is a miserable loop to be in fourteen times over.

Two events were sitting unused as empty `break`s — `EVENT_LIGHT_REALLY_LONG_PRESS`
and `EVENT_ALARM_REALLY_LONG_PRESS`, both fired at 1.5 s, well clear of the 0.5 s
long-press the real controls use. So, behind a new `PET_SHOWCASE` flag:

- **LIGHT held 1.5 s** steps to the next animation and *holds* it, one-shots
  included — `_pet_anim_tick` loops whatever is being previewed instead of
  letting it play once and fall back to rest. Walking off the end of the list
  hands the screen back to the live pet, so repeated presses cycle through all
  fourteen and return rather than stranding you in preview.
- **ALARM held 1.5 s** pushes the mood up one tic, wrapping past dead back to
  blissful, so every threshold can be seen in order. Handy for the question the
  segment constraints actually raise: do Confused and Upset read as different at
  a glance, on a display this coarse?

Any real interaction drops out of preview, so there's nothing to remember about
escaping it. Both controls fire their normal long-press action on the way past —
Movement delivers the 0.5 s event before the 1.5 s one, so stepping animations
also hugs the pet. Harmless while previewing, and noted next to the flag.

Verified the flag actually gates it: with `PET_SHOWCASE` and
`PET_DEBUG_HUD` both at 0 the firmware builds clean with no unused-function
warnings and comes out 200 bytes smaller. A debug tool that breaks the release
build is worse than no debug tool.

Cost with both debug flags on: 132,256 text + 2,044 data = 134,300, 55% of
budget. Hardware and simulator both pass.

Next: the sketches. Everything downstream of them is ready — the frame format,
the sideways geometry reference, a way to look at each animation on demand, and
a balance that will actually drive the pet through its whole range.

---

## Session 6 — 2026-09-12 — The layered display engine

### Two questions that had to be answered before any art

Dan asked both before converting sketches, which was the right order — each one
changes the export.

**Frame rate.** Not free: `movement_request_tick_frequency` rejects anything
that isn't a power of two (`__builtin_popcount(freq) != 1` at movement.c:428)
and silently falls back to 1 Hz. So the menu is 1, 2, 4, 8, 16, 32, 64, 128 Hz,
and the 12 fps the animations were drawn at simply isn't on it. The panel itself
refreshes at 32 Hz — 32768 Hz crystal ÷ 64 prescale ÷ 4 clockdiv ÷ 4 commons,
from the `slcd_init` call — so there's no point going above that either.

The answer to "sparse frames or padded frames" is **sparse**: one frame per
distinct pose, with an explicit `hold` in ticks. That's what the `hold` field was
always for. Source fps stops mattering at conversion; what survives is the list
of poses and how long each sits.

Offered 16 Hz for the finer 62.5 ms granularity; Dan chose to **stay at 8 Hz**,
on the grounds that slower is more elegant for this pet. It also halves the
wakeups. Nothing else had to change — every scene timer is already written as
`seconds × PET_ANIM_HZ`.

**Layering.** The real question, and the answer is that bespoke combinations were
never viable: five moods × five food states × two poo states is fifty
full-screen animations against twelve as separate layers, and the checklist only
has fourteen drawings in it.

### The regions, and the one that needed checking

Dan's assignment, which drove the implementation:

| Layer | Cells |
| --- | --- |
| Character | 1, 4, 5, 6, 7, 8, colon, and 9's A D E F |
| Poo / Barf | 9's G B C — poo is `G\|B\|C`, barf just the puddle `B\|C` |
| Food | position 3, pips filling `B`, `C`, `F`, `E`; BELL rings on a press |
| Buff / Debuff | position 0, plus `G\|H` and minus `H` |
| Sound | SIGNAL, since the watch is usually kept silent |

Position 1 joined the character region late, for snores and kisses coming off
the mouth. It's the only character cell with ties (`B`+`C` are one control, so
are `E`+`F`), which in the rotated view means whole top and bottom edges — it
can draw three stacked horizontal strokes and three verticals, no diagonals. Good
for puffs, no good for Z shapes.

**Position 9 is deliberately shared** between the character and the poo, which is
only safe because 9 has no tied segments — all seven of its addresses are
distinct. The same split in position 4 or 6 would have broken silently, since A
is tied to D in both.

The engine therefore declares ownership **per segment, not per position**, and
the compositor masks every frame against its layer's allowance. Art that strays
outside its cells gets clipped rather than invading a neighbour.

### Segment H was documented wrong

Dan's plus/minus idea prompted a check, and the Session 3 notes turned out to be
wrong: `SEGMENT_MAP.md` called H "the diagonal leg" of an R. It isn't — **H is
the centre vertical stroke**, confirmed three ways in the firmware's own
character set: `*` is `G|H` and is commented "The + sign for use in position 0",
`T` is `A|H`, `I` is `A|D|H`. Fixed in both the doc and the header.

Which makes Dan's scheme right, and right in a way the firmware itself isn't:
upright, G is horizontal and H vertical, but **rotate for the sideways face and
they swap** — so a minus as the wearer sees it is `H` alone, where the firmware's
own `-` glyph is `G` and would read as a vertical bar on this face.

### What got built

- `pet_layer_def_t` per layer, `pet_layer_t` runtime state, and a compositor
  that ORs masked layers into one framebuffer.
- Layers run on independent clocks — the pet's mood and what's beside it advance
  separately. The character layer keeps the animation queue; other layers settle
  to an idle animation instead.
- Animations declare their own layer, so callers never route by hand.
- Food pips and the four transient marks aren't animations at all: they're a
  direct read of state, composited each frame. That's simpler than contorting
  them into the animation model, and it's what they actually are.
- `_pet_play_sound` now also flashes SIGNAL, so every sound has a visual twin
  automatically — the silent-watch case was Dan's, and putting it in the one
  funnel every sound already went through made it free.
- Poo and barf have **real art now**, not placeholders. They were fully specified.

Fixed a bug the refactor created: the morning entry sequence still queued
`PET_ANIM_POO` onto the character layer's queue. Since POO now belongs to the
status layer, that would have started it on the wrong layer and stalled the
character layer with nothing to advance it. The poo doesn't need queueing at all
any more — it has its own cell and simply appears.

### The debug HUD is gone

It wanted four cells and Dan's regions claimed three of them. Offered to shrink
it to a single tic digit in position 2; Dan dropped it instead — digits read as
artificial on a sideways face, and the character's expression is the readout
that matters. The preview harness stays, and losing the numeric crutch arguably
makes it a better test: you judge the mood by looking at the pet.

### Verification

Wrote a standalone harness for the invariant the whole design rests on — that no
two layers claim the same segment. It checks every layer pair, both non-animated
regions against every layer, that position 9 ends up fully allocated across the
two owners, that position 2 is genuinely unclaimed, and that a deliberately
rogue frame with every bit set gets clipped out of its neighbours' cells. All
pass.

Hardware and simulator both build clean. 132,752 text + 2,044 data = 134,796,
55% of budget. With `PET_SHOWCASE` at 0: no unused-function warnings,
160 bytes smaller.

Next: the sketches. Nothing else is in the way.

---

## Session 7 — 2026-09-12 — Cleanup pass before the art

A deliberate stop to get the tree clean before animations start landing, on the
principle that it's easier to keep documentation honest than to repair it later.
Comment-only changes throughout; the firmware is byte-identical at 132,752 text
+ 2,044 data.

### Verified rather than trusted

Rather than reading the docs for plausibility, checked their factual claims
against the source. Parsed `Classic_LCD_Display_Mapping` and derived the tie
structure per position directly from duplicate segment addresses:

```text
  0   8 controls, none tied, has H      5   7 controls, none tied
  1   6 controls, B+C and E+F tied      6   6 controls, A+D tied
  2   4 controls, A+D+G tied, no F      7   7 controls, none tied
  3   7 controls, none tied             8   7 controls, none tied
  4   6 controls, A+D tied              9   7 controls, none tied
```

Every entry in SEGMENT_MAP's per-position table matches, as do all of its worked
glyph examples when checked against the firmware's own character set. One
inconsistency fixed: the table flagged "no H" on some positions but not others,
when in fact only 0 and 1 have H at all.

### What was actually wrong

- **"the smaller 8 and 9 off to one side".** They aren't. Decoding the exported
  art settled the geometry: the main line stacks 4, 5, colon, 6, 7 and then 8
  and 9 *continue below it*, smaller. The old top row becomes a right-hand
  column. Corrected in the header.
- **The outstanding-work list was stale.** It still named the on-screen food
  queue, which the layer work implemented, and persistence, which was dropped on
  purpose. Two TODOs remain — character frames and the four sounds — and the
  header now says exactly that.
- **CLAUDE.md still described `DECIDE` markers**, of which none survive, and
  carried a flash baseline measured on the Mac before the face was built out.
- **Section headings had drifted** from the file's own index after the layer
  refactor: "Renderer" and "Animation engine" against a contents block that said
  "Compositor" and "Layer engine".

### Simplification

The inline comments had grown essayistic — several were four or five lines of
rationale where the surrounding codebase uses one. Trimmed throughout, keeping
the *why* and dropping the retelling: a comment earns its place by explaining
something the code can't say itself.

Also stripped the "Decided 2026-09-12" stamps sprinkled through the code. The
decision belongs next to the code it governs; the date belongs here, in the log.
Net 40 lines lighter across the two files with nothing lost.

Added to SEGMENT_MAP a regions table naming which cells each part of the pet
owns, since that is the document open while drawing. CLAUDE.md now also points
at the GIF decode pipeline, which is otherwise only findable by reading back
through Session 6.

Both targets build clean, `PET_SHOWCASE` at 0 included.

### The tooling was living in a temp directory

Caught this before losing it: everything built this session to validate the
design — the GIF decoder, the calibration, the three check harnesses — existed
only in the session's scratch directory, which is temporary and outside the repo.
About 640 lines of working code, none of it committed, including the calibration
coordinates that took the most effort to establish.

Now in `_cs50ref/tools/`:

| | |
| --- | --- |
| `decode.sh`, `decode.py`, `segmap.c` | GIF → `pet_frame_t` table, with `--check` for the tied-segment report |
| `check_layers.c` | no two layers claim the same segment |
| `check_awake_time.c` | the waking-seconds maths is monotonic and additive |
| `check_balance.c` | a week of simulated care at different check-in rates |

Two things needed solving to package the decoder. It had been run as a pile of
ad-hoc commands, so it became one script — which immediately exposed that
**neither shell on the PC has the whole toolchain**: `ffmpeg` is installed
Windows-side, the compiler and `python3` are in WSL. The fix is that WSL can
invoke `ffmpeg.exe` directly, provided the scratch directory sits on `/mnt/d`
where both sides can see it and paths go through `wslpath -w`. `decode.sh`
detects which case it is in, so it also just runs on the Mac.

Packaging also surfaced a real bug. The first decode run put four components in
positions `2H` and `3H`, which should not exist — positions 2 through 9 have no H
segment at all. The cell boundaries for 2 and 3 had been estimated rather than
derived, and the segments belonged to position 3. With that corrected the food
pips decode as `3B 3C 3E 3F` — exactly the four specified in the region map,
found independently — and the pose count goes from 149 to **151**. The earlier
figure was wrong.

The checks copy their constants from the firmware rather than including it, so a
tunable changed in `pet_face.h` won't fail them until it is changed in both. The
README says so, and says when to re-run each.

---

## Session 8 — the art lands, and the decoder gets rebuilt

Twelve labelled exports arrived in `_cs50ref/FaceAnimations`, all 480×480 and
drawn at 8 fps, which is the playback rate — so a frame in the file is a frame
on the watch, and the sums are `frames ÷ 8 = seconds`.

The first decode of `Happy.gif` came back as four segments. That was the thread
that unravelled the whole decoder.

**The decoder had been calibrated on a stream where everything moved.**
`segmap.c` finds segments by looking for pixels that are dark in some frames and
light in others. Run against the single 233-frame combined export that built the
original calibration, nearly every segment changed at some point, so it found
them all. Run against one short animation, a segment held lit for the whole clip
never changes — and is therefore invisible. `Happy` decoded at all only by
accident: it has four blank frames on the end, which made its otherwise-static
mouth look like it was changing.

Sharing one calibration across all the art fixed that, and immediately exposed a
second failure underneath. With more varied art in the stream, **neighbouring
segments that touch merge into one connected component** and get a single label.
The merged blobs landed in the middle of their cells, so the geometric assignment
called them `H` — in positions 5, 7 and 9, none of which have an `H` segment.
Parsing `Classic_LCD_Display_Mapping` out of the firmware confirms it: only
positions 0 and 1 carry the centre vertical, and the tie list is exactly
`1B=1C`, `1E=1F`, `2A=2D=2G`, `4A=4D`, `6A=6D`.

So the approach was wrong, not the calibration. **The LCD's geometry is fixed and
known, and deriving it from the art was always the long way round.** `decode.py`
now holds one measured box per cell and gets everything inside a cell from the
seven-segment layout. It cannot invent a segment the hardware lacks, a static
segment is no different from a moving one, and nothing can merge. Verified by
repainting the decode and diffing it against the source: `extra` is zero
everywhere, and the two read identically frame by frame.

`segmap.c` is kept, but only for re-measuring the cell boxes if the canvas moves.

**A second silent bug, unrelated and worse.** ffmpeg resamples a GIF to a
constant frame rate by default, duplicating frames: the 16-frame exports were
coming out as 48, while `ffprobe` still reported 16, so the decoder read the
first 16 of 48 duplicates — a third of the animation, stretched. `-fps_mode
passthrough` fixes it. This would have been near-impossible to spot from the
output, since the result is a plausible-looking table.

### What the art asked for that the engine hadn't allowed

- **Poo and barf are whole scenes.** They were modelled as a detached blob on
  the status layer; they are drawn as the pet doing something, with the floor
  changing as a result. They now play on the character layer like any other
  one-shot, and the status layer keeps only the pile that outlives them
  (`PET_ANIM_PILE`). The pile's single pose is the poo animation's last frame,
  `9 G|B|C` — which is exactly what had been guessed by hand, independently.
- **Resurrect sweeps across cell 9.** The spirit drifts in from the right-hand
  edge of the display, through the cell the status layer owned. Splitting cell 9
  between the layers would have cut the entrance in half, so it is now shared.
  That costs nothing: compositing is an `OR`, so neither layer can erase the
  other, and the pet only resurrects with the floor already clean.
  `check_layers.c` now asserts the sharing rather than disjointness.
- **A blank tail on a loop is an artefact.** `Happy` carried four blank frames
  and `Resurrect` one. Left in, a looping mood blinks the pet out of existence
  for half a second every cycle. The decoder trims blanks at the ends, keeps
  interior ones — those are how a flash is drawn — and says what it did.

`PET_FRAMES(t)` fills in both the table pointer and the count from one macro, so
they cannot drift apart when an animation is redrawn at a different length.

### Upset and angry

Drawn straight after, and they complete the set. The five moods escalate
cleanly: happy is a smile, confused a flat mouth with a brow that shifts every
second, upset drops the brow into a frown, and angry adds the verticals to that
brow so it reads as a scowl. Upset and angry share the same mouth — cell 6's top
edge and both verticals, an open frown once rotated — and differ only in cell 5
above it, which is a nice economy: one cell carries the whole escalation.

Seeing the four side by side also settled a labelling question. Confused had
looked thin on its own — two poses, a flat mouth — and could have been a
mislabelled *upset*. Next to the actual upset frown it is clearly the neutral,
puzzled one. Nothing in the folder is mislabelled.

Every animation now has art, so the label scaffold in `_pet_draw` is
unreachable. It stays as the fallback for a row added to `_pet_anims` before its
frames are drawn — a blank screen would be a worse failure than a name.

### The sounds

Dan's direction, and the thing that made it interesting: most of them are cued
to a moment *inside* an animation rather than just fired alongside it. The
snore's exhale belongs on the puff; the barf's slide starts as the contents
leave the mouth; the poo knocks when the pile hits the bottom. So the pose
timings came out of the decoded tables first and the sequences were written
against them.

The two clocks make this easy: sound durations are 1/64 s and animation ticks
are 1/8 s, so **one tick is exactly 8 duration units** and a rest is how you
wait for a frame.

| Sound | Cue |
| --- | --- |
| Snore | High at tick 0, low at 9-12 where the puff is drawn in cell 1, twice — 4 s of figure inside the 6 s period |
| Kiss | Chromatic trill from tick 6, as the pucker completes |
| Eat | Mid chirp at tick 9 when the pip is swallowed, three lower chews at 10, 12, 14 |
| Barf | "Uh oh" over the wriggling mouth at ticks 0-6, then a chromatic octave down from tick 9 as it enters cell 7 |
| Poo | One short low knock at tick 9, as the pile reaches the bottom of cell 9 |
| Play | Arpeggio up and back down, immediate; big is the same shape a whole tone up |

Three of those are new — the spec only asks for four, but poo and the two plays
read as silent moments without them.

**The snore needed a fix to be cueable at all.** The sound ran on a 6 s timer
while the animation looped every 2 s, entirely independently. They happened to
stay 1 tick apart because 6 s is exactly three turns of the loop, but nothing
enforced it and the first change to either length would have broken it. The
animation is now restarted alongside the sound, so alignment holds by
construction.

That left a real fragility, and Dan pushed on it: the cue points were frame
numbers written into a C array with nothing tying them to the art. Redraw an
animation and every cue slides out from under it, silently.

### Cues, not offsets

The diagnosis that mattered: the drift was not caused by sound and animation
being separate systems — that part is normal. It was caused by **writing down
the offset when what was meant was the moment**. "Rest 72, then knock" is a fact
about tick offsets; the intent was "knock when the pile hits the floor". Offsets
are unstable under redraw. The moment is not.

So the cue is now the thing stored, and the engine fires it on the frame where
its condition first comes true:

```c
typedef struct {
    uint8_t sound;
    uint8_t position;       // the cell to watch
    uint8_t mask;           // these segments... (0 = when the animation begins)
    bool    on_clear;       // ...going dark, rather than lighting up
    bool    once;           // only the first time, per play or per loop
} pet_cue_t;
```

All four cued sounds mapped onto simple conditions, which is the test of whether
an abstraction fits: the pile reaching cell 9's bottom edge, the contents
lighting cell 7, the puff lighting cell 1, the jaw lighting cell 6's centre.
Sounds became short phrases with no leading rests, and the chews stopped being
hardcoded at three — cueing on the jaw means they follow the drawing.

Two things fell out of the change:

- **The snore timer disappeared.** Its 6 s period was wall-clock, which is why
  it needed the animation restarted underneath it to stay in step. It is now
  counted in breaths — two voiced out of every three — so the rhythm is defined
  in terms of the animation and cannot drift from it. One tunable replaced two,
  and a hack went with it.
- **`once` exists because the check found a real bug.** Cell 7's `D` flickers as
  the contents tumble through it, so the barf slide fired twice, the second
  restarting the 9-tick ramp partway down and cutting it off. That would have
  shipped. Everything fires once except the chew, which deliberately repeats.

`check_sounds.py` was rewritten for the new invariant. Drift is no longer
possible, but a quieter failure took its place: a cue whose condition never comes
true is simply silent. So it replays each animation and reports where every cue
lands, catches a cue repeating faster than its own sound can play, and flags any
sound nothing reaches. Verified by pointing the poo cue at a cell the art never
touches and by letting the slide repeat — it catches both.

### Still outstanding

Nothing. The face is feature-complete against the spec.

Flash is 135,112 + 2,124 = 137,236 (56%), up 2,440 bytes over the pre-art
baseline for fourteen animations, ten sounds and the cue engine.

---

## Session 9 — the showcase is a feature, not a debug flag

`PET_DEBUG_CONTROLS` is now `PET_SHOWCASE`, along with `_pet_showcase_next`,
`_pet_showcase_step_mood`, `showcase_on` and `showcase_anim`. A rename, so the
firmware is byte-identical — but the old name described what it was built for
rather than what it is. Most of the animations are gated behind the clock, so
walking them on demand is how anyone actually sees the art, and Dan's read is
that the showcase is as much of the appeal as the game. It is documented as a
feature in MANUAL.md §10 and in the header's control list, with the flag kept
for a build where the buttons only play the game.

**A correction that matters.** The manual claimed the 1.5 s holds "feed or sweep
the pet on the way past". They do not: `EVENT_*_BUTTON_UP` only arrives on a
release under half a second, so the short actions never fire on a hold. What
does fire is the 0.5 s long-press:

- `LIGHT` hugs — one of the four daily hugs and −0.25 tic per animation stepped.
  Walking all fourteen exhausts the cap by the fourth press.
- `ALARM` does nothing on hardware while the pet is alive, and resurrects it if
  it is dead — so the mood step cannot walk past death without reviving first.
  In the simulator it plays instead, standing in for the accelerometer.

Session 6's entry had this right at the time ("stepping an animation also hugs
the pet"); the manual lost it. Worth noting as the failure mode: a fact recorded
once in a process log does not survive into a document written later from memory.

**No new gesture, checked rather than assumed.** The options were `MODE`
really-long-press, a double-tap, or a button chord. `MODE_LONG_PRESS` is handled
by `movement_default_loop_handler` at 0.5 s and jumps to face 0, so reaching
1.5 s means swallowing the system-wide "hold Mode to go home" that every other
face honours. `SINGLE_TAP` and `DOUBLE_TAP` are both marked "not yet
implemented" in `movement.h`, and a tap would fight shake-to-play regardless.
Movement exposes no chords. The existing gesture stays.

**The hug stays too, un-refunded.** Undoing it when the 1.5 s press arrives was
three lines and tempting, but the trade reads better as it is: a pet you stop to
admire gets a cuddle out of it.
