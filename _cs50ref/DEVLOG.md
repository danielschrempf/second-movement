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

---

## Session 10 — playtesting on the wrist

Dan flashed the firmware and wore it. Two reports came back, and between them
they turned up three bugs that no check harness could have caught, because each
one was a rule that was internally consistent and wrong about the world.

### "It doesn't seem to draw the poos or barfs. There's nothing to clear."

Three separate causes, all of which had to be fixed before a poo could be seen
at all.

**1. Sweeping cancelled digestion.** `_pet_sweep` read the spec's "Sweep clears
all" to include a poo still on its way, zeroing `poo_due_ts` along with
`has_poo`. That is a defensible reading of the sentence and a catastrophic one
in play: `ALARM` is the sweep button, there is nothing to sweep most of the
time, and pressing it out of curiosity after feeding silently reset the
twelve-hour countdown. Anyone who touched the button between meals never saw a
poo in their life. The spec's own tree says `if sweep -> clear poo state`, which
is what is on the floor; sweep now clears that and leaves the countdown alone.

**2. The poo only arrived on activate.** `_pet_catch_up` runs on `EVENT_ACTIVATE`
and nowhere else, so an arrival was only ever noticed on the next visit. Now
`_pet_poo_arrives` is factored out and called from the tick as well, once a
second.

**3. The scene was gated on freshness.** `PET_POO_FRESH_SECONDS` played the poo
animation only if the drop was under a minute old, on the reasoning that
animating the pet squatting over something that landed hours ago is a small lie.
It is, and it cost the animation its entire existence: a twelve-hour countdown
expires while the face is in the background, so the odds of a visit landing in
that minute are about one in seven hundred. The gate is gone. The scene is now
owed to whoever next has the face open, which is the spec's own wording —
"animation plays on revisit if one has been made" — and the floor is held clean
until it plays, so the pile is the scene's ending rather than a spoiler for it.

**And the barf left nothing behind.** `_pet_set_status` only ever asked about
`has_poo`, so the puddle the barf animation ends on was wiped the moment the
scene rested. The header had claimed since the layers went in that the status
layer draws "poo is the stem and base, barf just the puddle" — the art was
drawn, the second half was never wired up. There is now a `has_barf` flag and a
`PET_ANIM_PUDDLE` (`B|C`, the pile minus its stem), swept by the same button.
It charges no tics: the barf already took 0.25 out of the pet on its way past,
and charging again for the evidence is double jeopardy.

**What this did to the balance.** `check_balance.c` modelled the old sweep, so
its simulated owner cancelled the countdown on every visit and never paid the
mess penalty. Fixing the model changes the game:

```text
                                    d1 d2 d3 d4 d5 d6 d7
3/day, feed at 08:00 (morning)      5  8 10 13 16 24 24   DEAD   (was healthy)
3/day, feed at 13:00 (midday)       3  6  8 11 14 16 19   struggling
3/day, feed at 19:00 (evening)      1  5  5  6  7  7  8   healthy
```

Verified it is the mess penalty and nothing else by rebuilding the harness with
the poo decay stubbed out: all three feed times return to healthy. So the
mechanic simply never bit before. A meal at 08:00 drops a pile around 20:00 that
sits through the night and into the morning — four waking hours of double decay,
about 0.67 tic a day — against a care budget that nets barely a quarter tic a
day in hand. Feed time is now the sharpest lever in the game.

This is left as it is rather than re-tuned in the same change that fixed the
bug. It is the spec's rate ("+1 tic every 1 tic") applied to the spec's delay,
and it is now honest. MANUAL.md §8 states the asymmetry outright and names the
two knobs — `PET_POO_DELAY_SECONDS` and `PET_POO_SECONDS_PER_QT` — if it reads
as too sharp on the wrist.

### "Too sensitive. Hard to get to the second stage, but really easy for barf"

The old rule opened a 5 s window on the first motion and counted every event
inside it; more than three was nausea. That counted *interrupts*, not play. Tap
detection runs the accelerometer at 400 Hz, so one flick of the wrist is a burst
— and the pet was asking the burst a question it could not answer. A shake
either registered once, giving `PLAY 1` every time, or overshot the limit
entirely and barfed. The middle rung was nearly unreachable, which matches
exactly what Dan saw.

Replaced with Dan's design: a ladder paced by the clock instead of a count.

```text
shake                -> PLAY 1, then 5 s deaf, then a 5 s window
shake in that window -> PLAY 2, deaf and a window again
shake in that window -> barf
window expires       -> the session ends on whichever rung it reached
```

Each shake climbs exactly one rung, because after it lands the pet stops
listening. The count of interrupts stops mattering: whatever the hardware makes
of one shake, only the first event is heard.

Both halves of the pause live in one countdown — above `PET_PLAY_OPEN_TICKS` the
pet is deaf, below it a shake is heard — and the countdown does not start until
`_pet_anim_busy` goes false, so the 10 s runs from the end of the flourish
rather than from the shake. That was Dan's "start the count *after* the
animation has played", and it has a second effect worth having: the pet is deaf
for the whole animation, which is where most of the stray taps land.

Double-tap detection is now off (`movement_enable_tap_detection_if_available(false)`).
The pet handles `EVENT_SINGLE_TAP` and `EVENT_DOUBLE_TAP` identically, so
enabling it only doubled the interrupts one shake produced. No behaviour change
given the pacing, but no reason to spend them.

`nausea` and `PET_NAUSEA_LIMIT` are gone, replaced by `play_stage`.

### What the harnesses did and did not catch

All four still pass, and all four would have passed before this session. That is
worth writing down: every one of these bugs was a correct implementation of a
wrong rule. `check_layers` proved the status layer owns cell 9's `G B C` — it
did, and nothing ever asked it to draw there after a barf. `check_sounds`
proved the poo cue fires when the pile reaches the floor — it does, in an
animation that had no way to play. A test suite that only checks internal
consistency cannot tell you the pet is unreachable; wearing it can.

Flash is 135,248 + 2,124 = 137,372 (56%), up 136 bytes.

---

## Session 11 — two controls that did nothing

More wrist time, two more reports. Both turned out to be the same shape as
session 10's: code that did exactly what it said, where what it said was wrong.

### The pet ignored a silent watch

Sounds went out at `BUZZER_PRIORITY_SIGNAL`, which maps to
`movement_signal_volume()`. That setting is soft-or-loud with **no off** — and
neither is the alarm volume. The only mute Movement gives the wearer is
`BTN beep -> N` (`movement_button_should_sound()`), which governs button sounds
and nothing else. So a pet at signal priority could not be silenced by anything
reachable from the settings face, on a watch whose whole appeal is being
unobtrusive.

Now gated on `movement_button_should_sound()` and played at
`BUZZER_PRIORITY_BUTTON`. That is the right home on the merits, not just the
convenient one: these sounds answer what the wearer just did, rather than being
a scheduled chime, and at the lowest priority an actual alarm is never talked
over by the pet chewing. `SIGNAL` still flashes when muted, which is what that
indicator was put in for.

### The animation showcase looked completely dead

Holding `LIGHT` hugged the pet and then, apparently, nothing. The cause is an
interaction between two things that were each reasonable alone.

Movement delivers `EVENT_LIGHT_LONG_PRESS` at 0.5 s on the way to
`EVENT_LIGHT_REALLY_LONG_PRESS` at 1.5 s. The escape hatch at the top of
`pet_face_loop` treated the long press as a real interaction and cleared
`showcase_on` — correctly, since the hug needs the screen back to show its kiss.
But `_pet_showcase_next` read its cursor off that same flag:

```c
pet_anim_id_t next = s->showcase_on ? s->showcase_anim + 1 : PET_ANIM_HAPPY;
```

By the time the 1.5 s press arrived, `showcase_on` was always false. So every
hold stepped to `PET_ANIM_HAPPY` — and since the pet was usually happy already,
every hold appeared to do nothing but hug. The walk could never reach its second
entry, let alone its sixteenth.

Split in two: `showcase_anim` is the cursor and survives the long press,
`showcase_on` only means "an animation is held right now". A short press, a
sweep or a shake clears both, so leaving the showcase deliberately and coming
back starts the walk over; a hug in passing does not.

Dan offered to move the showcase to the `ALARM` hold instead, since `ALARM`'s
long press does nothing while the pet is alive. Not needed — the button was
never the problem, and he likes that stepping an animation hugs the pet. Kept on
`LIGHT`.

### Worth noting about the diagnosis

Nothing here was found by running anything. Both were found by reading
`movement.c`: the volume table in `_movement_get_buzzer_volume`, and the order
`_process_button_longpress_timeout` emits events in. `EVENT_LIGHT_REALLY_LONG_PRESS`
has exactly one other user in the whole tree (`hydration_face`, on `ALARM`), so
there was no prior art to copy the pattern from — and the failure was silent in
the most literal way, since stepping to the animation already on screen is
indistinguishable from the button not working.

Flash is 135,272 + 2,124 = 137,396 (56%), up 24 bytes.

---

## Session 12 — a peck, and what the pet costs

### The kiss

Down from six semitones at 3/64 s each (19/64 s ≈ 0.3 s) to four at 2/64 s
(8/64 s = 0.125 s). Same chromatic shape and the same F6 on top, started closer
to it so there is less distance to travel. A peck rather than a trill.

### What it costs

Measured by building the same firmware twice, once with `pet_face` in
`movement_config.h` and once without. Worth recording that the first attempt
reported *zero* difference: `movement_config.h` has CRLF line endings on the PC
(gotcha 4's neighbourhood), so `sed "/^    pet_face,$/d"` never matched and both
builds were identical. The lesson is the one this repo keeps teaching — a
measurement that comes back suspiciously clean is usually measuring nothing.

```
with     135,192 text   2,116 data   4,600 bss
without  129,368        2,036        4,592
face     + 5,824        +   80       +   8
```

**5,904 bytes of flash**, 2.40% of the 245,760 budget. **80 bytes of RAM** for
`pet_state_t`, plus 8 for Movement's two per-face arrays. The single largest
symbol is `pet_face_loop` at 1,952 bytes, which is not a big switch statement —
every `_pet_*` helper is static with one call site, so GCC inlines the whole
state machine into it. The fourteen frame tables are 1,812.

**Battery is bounded by screen time, and that is the whole story.**
`pet_face_resign` drops the tick to 1 Hz and turns tap detection off, and
Movement's inactivity timeout returns to face 0 after 60 s by default. Off
screen the face costs literally nothing; on screen it costs an 8 Hz tick against
the clock face's 1 Hz, and the accelerometer at 400 Hz in low-noise mode against
a background rate that is powered down by default. The accelerometer dominates
by a wide margin.

What is deliberately *not* in MANUAL.md §16 is a µA figure. Two attempts to pull
the LIS2DW12 consumption table (ST and a Farnell mirror) both timed out, and a
remembered number in a document that will be read by a grader is worse than an
honest gap. §16 names the exact configuration to look up instead.

### Reducing it

Three changes, all measured rather than assumed:

**A shadow framebuffer.** `_pet_draw` used to push all ten cells on every
redraw. It now keeps what it last wrote and pushes only the differences, which
matters because repainting one cell is up to sixteen SLCD register
read-modify-writes. `redraw_cost.py` — a new tool, reusing `check_sounds.py`'s
parser — replays every animation and counts:

```
143 frame transitions:  10 cells per redraw -> 1.05    (90% fewer)
resting on happy:       35 cell repaints/s  -> 0, plus 3 colon flips/s
```

Zero, resting, because the blink is carried by the colon, which is a flag rather
than a cell. Resting is where the face spends most of its screen time, so that
is the number that counts. `_pet_invalidate` marks the shadow stale on activate
and on a low-energy update — Movement clears the display on a face switch, and a
shadow describing a screen that no longer exists is worse than no shadow.

**The accelerometer off while dead.** A dead pet turns every shake away in
`_pet_blocked`, so running the accelerometer at 400 Hz for it buys nothing.
`_pet_set_tap_detection` only touches the hardware when the answer changes, and
`_pet_rest` is on every route into and out of death, including the showcase's
mood step.

**The label fallback removed.** Each row of `_pet_anims` carried a `const char
*label` and a six-character string, drawn if an animation had no frames — a
fallback its own comment admitted could never fire in a finished build, at a
cost of about 200 bytes. Replaced by a `check_sounds.py` assertion that every
animation except `PET_ANIM_NONE` has a frame table: the same guarantee, moved
from runtime to build time, for no flash at all.

Adding that assertion turned up two parser gaps that had been there since the
tool was written. `read_anims` matched the array's own `[PET_ANIM_COUNT]`
dimension as though it were a row, and `read_frames` required the `hold` field
to be a decimal literal — so `_pet_frames_pile` and `_pet_frames_puddle`, whose
single pose holds for `PET_ANIM_HZ`, parsed as having no frames and were never
checked at all. Both fixed. A harness that quietly skips what it cannot parse is
the same failure as a cue that never fires.

Net: flash 137,396 -> 137,308, RAM 68 -> 80 bytes (the shadow). The flash saving
is small because there is not much fat; the display saving is not.

---

## Session 13 — the barf that played `PLAY 1`

**The deaf period, 5 s to 3 s.** Playtesting says the ladder itself works now,
but five seconds of a pet that ignores you is a long time to stand there holding
your wrist. Three still swallows the burst of taps a single shake produces,
which is all it was ever there for. `PET_PLAY_DEAF_SECONDS` is the only thing
that changed; the window stays at five.

**The barf that played `PLAY 1`.** Third shake, and the pet left a puddle, threw
the minus sign, charged the tic — and then played the small flourish instead of
the barf. All of the state was right and only the animation was wrong, which is
the tell: something was overwriting the animation after `_pet_barf` had already
run.

`_pet_barf` ended the session by dropping straight to `PET_SCENE_IDLE`. But the
shake that reached the third rung is still arriving — it is a burst of interrupts
on a 400 Hz accelerometer, which is the whole reason the ladder is paced by the
clock. Every one of those taps after the first now found `_pet_on_motion` in the
scene-idle branch, sailed past `_pet_blocked`, and started a brand new play
session: `PLAY 1`, over the top of a barf animation two frames old. The barf was
deaf to nothing, because ending the scene is what turned its deafness off.

So the barf keeps its scene. `play_stage` drops to 0, which now means "the ladder
is over" rather than only "no session", and `_pet_on_motion` reads stage 0 as
deaf whatever the countdown says. `play_ticks` is set to one more deaf period, so
`_pet_play_tick` waits out the animation — it already holds while
`_pet_anim_busy` — then three more seconds, then rests. The tail of the shake has
nowhere to land.

The general shape of the bug is worth keeping: a scene is also a guard, and
handing the screen back before the animation that ends the scene has played is
how you lose it.

**The comments cut back.** `pet_face.c` carried its own design diary — why the
tap counting was replaced, why the poo sweep leaves a pending drop alone, what a
label fallback used to cost. All of that now lives in `MANUAL.md`, which is where
someone reading *about* the face looks, and none of it belongs next to the line
it describes. The inline comments say what the line does and what would break if
you moved it; the reasoning is a link away. `pet_face.c` 1,582 -> 1,418 lines,
`pet_face.h` 485 -> 428, with no code removed but the barf fix added.

Flash 138,304 text + 2,576 data on GCC 15.3, unchanged by the comment pass, as
it should be.

---

## Session 14 — leaving the showcase never let go of the screen

Playtesting again: "some of the longer animations have the potential to loop
forever depending on when the showcase button is pressed."

Reading the code did not find it — the showcase escape switch looked complete,
every button cleared `showcase_on`, and the walk terminates by construction. So
I replayed the layer engine in Python against the tables parsed out of
`pet_face.c` and swept every position in the walk against every way of leaving
it. 185 of 512 combinations never got back to the live pet.

The bug is that **clearing `showcase_on` is not leaving the showcase.** All it
does is stop `_pet_layer_tick` forcing a one-shot to loop. That is enough for a
one-shot — it reaches its last frame and calls `_pet_rest` on the way out, which
is the only thing in the engine that puts the real pet back. It is not enough
for anything else:

- a **looping** animation goes on looping, because `loop_here` came from the
  animation table and never depended on the showcase. Walk to `CONFUSED` and tap
  `LIGHT` and you have a perfectly happy pet wearing a confused face, forever.
  `DEAD` is the good one: a tombstone on a pet in no trouble at all.
- the two **floor states** are worse. `_pet_showcase_next` blanks both layers
  before starting the next animation, and `PILE`/`PUDDLE` play on the *status*
  layer — so the character layer is left on `PET_ANIM_NONE`, which
  `_pet_layer_tick` returns from immediately. Nothing ever reaches `_pet_rest`.
  The pet is gone from the screen, and a pile it never made sits there lit.

Which is the "depending on when": whether you land on a one-shot or not decides
whether the screen comes back. The one-shots are the majority of the list, so
it looks intermittent rather than broken.

The fix is one function. `_pet_showcase_exit(keep_cursor)` clears the flag and
calls `_pet_rest`, and both escape cases go through it; it only rests if the
showcase actually had the screen, since `_pet_rest` would otherwise cut short
whatever the pet was doing on every stray button press. +16 bytes of flash.

**`check_showcase.py`.** The sweep that found it is now a check. The first
version of it was useless and worth recording why: it modelled the *fixed*
escape, so it passed against the broken firmware just as happily. A harness that
transcribes the behaviour under test proves nothing. It now reads that one fact
— do the escape cases route through a helper that calls `_pet_rest`? — out of
`pet_face.c`, and reverting the fix fails it, 185 stuck of 512. The rest of the
engine is still a transcription, which the tools README says out loud.

Same lesson as the barf last session, one level up: a scene, or a mode, is a
claim on the screen, and the code that gives up the claim has to hand the screen
back rather than just stop holding it.

---

## Session 15 — reading as deliberate

A pass for things that looked half-built rather than decided. None of it changed
what the pet does; all of it changed what the code claims about itself.

**Persistence.** `_pet_load` and `_pet_save` were empty functions with three
call sites, which is exactly what an unimplemented feature looks like. There is
no save format and none is planned: the pet lives in the face's Movement context
and a reflash hatches a new one, which is the design. Both functions and all
three calls are gone, and the header now says so in a sentence.

**The LCD.** `_pet_lcd_map()` chose between `Classic_LCD_Display_Mapping` and
`Custom_LCD_Display_Mapping` at runtime — but the art is drawn on the classic
F-91W's segment geometry and is only correct there, so the branch was offering
support that does not exist. Supporting the custom panel means redrawing sixteen
animations, not picking a different table. `_pet_draw` indexes the classic table
directly now. `DISPLAY=custom` still builds.

**Three indicator flags.** `PET_FRAME_PM`, `PET_FRAME_24H` and `PET_FRAME_LAP`
were defined and handled in `_pet_draw_flags`, and unreachable: the layer masks
let a frame set only `COLON`, and `_pet_draw` adds only `BELL` and `SIGNAL` from
state. Three dead branches in the hot draw path, gone with them.

**`<stdio.h>`**, included and unused.

Net 138,320 -> 138,152, so 168 bytes. The point was not the bytes. `_pet_load`
in particular had already misled the manual, which described the no-ops as
though a save path were pending.

Follow-up: the custom-LCD build is now refused rather than documented.
`pet_face.c` requires `FORCE_CLASSIC_LCD_TYPE` and `#error`s without it, which
also catches `DISPLAY=autodetect` — that resolves the panel at runtime, so it is
not a guarantee of anything. A garbled pet is a worse answer than a failed
build. The cost is that `pet_face.c` is listed unconditionally in
`watch-faces.mk`, so this refuses the whole firmware for another panel, not just
the face; the error message says which two lines to remove.

---

## Session 16 — the actions that made no sound

Wearing it for a while turned up something the checks could never fail: the pet
was silent for most of what you actually *do* to it. Ten sounds existed and all
ten were correct, so `check_sounds.py` passed every time. It was only auditing
the cues that had been written, not asking which moments had none.

The gap was lopsided in a telling way. Everything the pet does *to itself* —
snoring, eating, barfing, pooing, playing — had a sound. Everything *you* do to
it mostly did not: feeding, sweeping and resurrecting were all mute, and so was
being woken in the night, which is the only penalty in the game. Sounds had been
written alongside the animations, so anything without art had been skipped by
default rather than by decision.

Nine new sequences, +200 bytes of flash. The interesting part was not the notes.

**Cues cannot carry all of them.** A cue names a moment in an animation, which
is exactly right when there is art to name. Three of the silent actions have
none. A feed press only moves a counter; the eating is three seconds away and
already had its own sounds. Sweeping changes the floor, not the pet. And death
and a mood change both land on *looping* animations, where a cue fires again on
every turn of the loop — a tombstone that tolls for ever.

So those are played directly by the code that causes them, and the split is now
the shape of the sound enum: cued, then direct.

**Which meant the check had a hole.** `check_sounds.py` failed a sound nothing
cued — a good rule, and one that would have failed all six direct ones. The lazy
fix was to exempt them. Instead it now finds `_pet_play_sound` calls in the
source and names the function each lives in, so the "nothing reaches this sound"
guarantee still covers every sound, and the report says how each is reached:

```text
grumble: sound is defined                                ok
    played directly by _pet_disturb
```

**Three of them needed a rule, not just a sequence.** Each is a moment that can
recur without being a new event, and the first draft of each was wrong:

- `PET_ANIM_WAKE` plays both for the morning and for a poke in the night, so
  cueing a yawn to it yawned at a pet that had just been woken against its will.
  `_pet_cue_audible` suppresses the yawn by scene — the same mechanism that
  voices two snores in three — and `_pet_disturb` moves the scene *before*
  starting the animation so there is something to test.
- The death knell is an edge on entering `PET_SCENE_DEAD`, not a cue on
  `PET_ANIM_DEAD`. Because the scene survives a visit, returning to the same
  tombstone is quiet.
- The mood step only sounds for a change earned in front of you. `_pet_enter`
  seeds `shown_mood` after the catch-up, so arriving to a pet that soured while
  the face was closed opens silently. You were not there for it.

The same principle settled sweeping a clean floor: silent, because the button
really has done nothing. A sound is a claim that something happened.

**What was already right.** The play ladder reads as an escalation on its own —
small is a C arpeggio, big the same figure a whole tone up, then the barf — so
nothing was added there. Worth recording as a thing that looked like a gap in a
list and was not one in the firmware.

---

## Session 17 — three sittings, and a pet that can be overfed

Feeding had no ceiling. Four pips was the size of the *plate*, not a limit: wait
seventeen seconds for the plate to clear and you could fill it again, all day, and
each pip was worth a quarter tic. Nothing in the rules said no.

**The waking day moved to make room for the fix.** Overfeeding needs a window to
be measured against, and 05:00–21:00 is sixteen hours, which does not divide into
three. 06:00–21:00 is fifteen: breakfast 06:00, lunch 11:00, dinner 16:00, five
hours each. `PET_HOUR_AFTERNOON` moved from 10:00 to 11:00 at the same time so
the daypart boundary and the first sitting are the same line — "morning" and
"breakfast" now mean one thing rather than two. A `#error` guards the division,
because an uneven split would make the cap mean something different at dinner
than at breakfast and nothing would say so.

This diverges from the spec, which says 9pm–5am with morning 5am–10am. The
divergence is deliberate and this is the record of it.

**The rule.** Four pips stay down per sitting. The fifth leaves the plate, plays
the barf in place of the eat, and takes the sitting's nutrition with it: the
whole −1.0 tic handed back, plus the +0.25 a play barf charges, so the sitting
nets +0.25 — worse than never having fed at all. The plate is thrown out with it.

`seg_buff_qt` is the reason there is state rather than arithmetic. The refund has
to be *what was actually banked this sitting*, not four times the pip value,
because the second barf in a sitting has nothing left to return and must cost
only the penalty. Tracking the granted buff directly says that without a special
case, and survives the constants changing.

The sitting stays closed after a barf rather than resetting. Resetting would have
made throwing up a 0.25-tic tax on an unlimited feeding loop; keeping it closed
makes it a wall, which is what a cap is for. Resurrection clears it, or a pet
that died overfed would come back unable to eat.

**What the hour change cost, which was not nothing.** One waking hour a day is
one less hour of decay, and the balance table moved under it — 2.67 tics a day of
passive decay became exactly 2.5. Routines that used to die now struggle:

```text
                                    before        after
3/day, feed at 08:00        DEAD on day 6     struggling (17)
3/day, feed at 13:00       struggling (19)       healthy (10)
2/day, feed evening         DEAD on day 7     struggling (17)
```

Neglect death went from 38–46 h to 39–48 h, so the front-page claim of "under two
days" became "about two days". Worth recording that a scheduling change made to
tidy up an arithmetic problem quietly rebalanced the whole game.

**And it raised the ceiling it was meant to lower.** Four pips per sitting across
three sittings is twelve a day — three times what any routine in the table fed
before. `3/day, full plate at each` now holds at one quarter tic indefinitely,
the healthiest line in the sim. The cap punishes stacking meals, not feeding: the
pet wants to be fed often, just not all at once. That reads right, and it is the
opposite of what a cap sounds like it would do.

**Both harnesses had copied the constants.** `check_awake_time.c` failed loudly
and usefully — five cases with `05:00` in their *names* and `H(16)` in their
expectations. `check_balance.c` did not fail at all, which was worse: it fed
exactly four pips at one visit, so it sat precisely on the new cap and modelled
the new rule correctly by accident. It now models the sittings properly and has
three routines that exercise them, including two that overfeed.

---

## Session 18 — making it mergeable

Not a behaviour change: a packaging one, found by asking what an upstream PR
would actually contain.

**The file list is four, not three.** `pet_face.c`, `pet_face.h`,
`watch-faces.mk`, and — the one easy to forget — `movement_faces.h`, which is
the aggregate header every face is included from. Checking five upstream face
additions (`world_clock2`, `hydration`, `tomato`, `tide`, `local_solar_time`)
they all touch exactly those four and nothing else. `movement_config.h` is the
*user's* face list, not the project's: four of the five left it alone, and a PR
that edits it is asking to change what everyone else's watch runs.

**The `#error` was a blocker.** `pet_face.c` is listed unconditionally in
`watch-faces.mk`, so refusing to compile without `FORCE_CLASSIC_LCD_TYPE` did not
withhold the face on a custom panel — it stopped the entire firmware from
building. `make BOARD=sensorwatch_pro DISPLAY=custom` failed at `pet_face.o`.
Perfectly fine while the only person building this was me, and fatal the moment
anyone else pulls it.

The first fix was to compile away rather than refuse: wrap the whole of
`pet_face.c` in `#ifdef FORCE_CLASSIC_LCD_TYPE` so it becomes an empty
translation unit elsewhere, and leave the `pet_face` macro undefined in the
header. That worked — a custom build compiled the file, got nothing, and linked.

**Then the gate came out entirely**, which is where this ended up. Both mappings
are `static const` arrays in `watch_common_display.h`, so nothing was ever
stopping the face compiling for the custom panel; the guard existed only to stop
it *looking wrong*. That is a judgement the person building the firmware is
better placed to make than the face is. It now builds everywhere and the header
says plainly that the custom LCD will not read correctly.

The rule the `#error` broke is still worth stating: **a guard on a shared build
should withhold the thing it guards, not the build.** It could not tell "this
face does not work here" from "you cannot build anything here", and only ever
expressed the second. The `#ifdef` fixed exactly that — and then turned out to be
answering a question nobody had asked. Three versions, one behaviour, and the
classic build is byte-identical across all of them, which is the tell that none
of the strictness was protecting anybody.

What survives is the plain statement at the top of `pet_face.c`: the art is the
classic panel's geometry, the face runs anywhere, and on the custom LCD it will
not read as intended.


---

## Session 19 — the showcase becomes a reel

The showcase was a stepper: hold `LIGHT` for 1.5 s, get the next animation, hold
again for the one after. Sixteen holds to see sixteen animations, and each one
spent a hug on the way past — so a full walk exhausted the four-a-day cap by the
fourth press and the rest were silent no-ops.

It is now a reel. One hold starts it, another cancels it, and in between the
animations butt up against each other in a fixed order, twice each:

> resurrect, snore, wake, eat, kiss, play small, play big, barf, poo, happy,
> confused, upset, angry

The order is a life, which is the only ordering that made the sequence read as
something rather than as a list: the climb out of the grave, a night's sleep and
the morning after, the things you do to the pet, what comes back out of it, and
the four moods last — those being the ones a wearer sees anyway, so the ones to
lose least by missing if you cancel early.

**Three of the seventeen animations are not entries, and each omission is an
improvement.** All three are single static poses that another entry already
contains, frame for frame:

- `PET_ANIM_PILE` and `PET_ANIM_PUDDLE` are the last frames of `poo` and `barf`.
  As entries they would have been two still frames of the floor; where they are,
  they are the thing the pet just made.
- `PET_ANIM_DEAD` is frame 0 of `PET_ANIM_RESURRECT` — identical, segment for
  segment, which only turned up on going to check. So the reel still opens on the
  tombstone and then sinks it down the screen, and `dead` as an entry of its own
  was a second of a still image standing in front of the same image moving.

Dropping the two floor states also removed the whole status-layer half of the old
exit problem: every entry is now a character-layer animation.

**A pass is only countable at one instant.** An animation "ends" when the frame
index runs off the table, and for a looping one that instant exists *only*
because the showcase forces it to loop. So the counter lives in `_pet_layer_tick`
at exactly that point, and nowhere else would have worked: there is no
end-of-animation callback, and timing the entries would have re-introduced the
drift the cue system was built to remove. Count the pass where the loop turns
over, advance at `PET_SHOWCASE_PLAYS`.

**Toggling needed a third piece of state.** The 0.5 s long press arrives on the
way to every 1.5 s hold, and it hands the screen back so the hug can show its
kiss — so by the time the hold lands, `showcase_on` is already false and has no
memory of what it was. A toggle cannot be written against it. `showcase_exit` now
takes a `remember` flag that sets `showcase_interrupted` when it took the screen
from a running reel, and the hold toggles against *that*.

Clearing that note wanted care. The obvious place is the release, and it is
wrong: Movement drains a batch of pending events in enum order, and
`EVENT_LIGHT_LONG_UP` sorts before `EVENT_LIGHT_REALLY_LONG_PRESS`, so a release
landing in the same batch as the 1.5 s timeout wipes the note a moment before the
hold reads it — and the button starts a reel where it meant to cancel one. It is
cleared on `EVENT_LIGHT_BUTTON_DOWN` instead, which sorts first and always begins
the press, so the note cannot escape the press it was left in.

This is the same shape as the bug in Session 14, one level up: the state that
says "is it on screen" and the state that says "where are we" have to be separate
fields, because the button sequence clears the first on its way to reading the
second.

**The lap is sized against the inactivity timeout rather than fighting it.**
`check_showcase.py` was made to print the lap total, and the first version came
out at 60.5 s against a default timeout
(`movement_timeout_inactivity_deadlines[0]`) of 60 — so the face would have
resigned half a second before the reel finished its one and only lap, looking for
all the world like a deliberate design.

The first fix was to ignore `EVENT_TIMEOUT` while the reel was up, which works:
Movement arms that timeout once per spell of activity, so ignoring it holds the
face until the cancelling press arms it again. The better fix was to take `dead`
out. That is 1 s off the lap, leaving **59.5 s** against a 60 s timeout anchored
on the button release that started the reel — so the reel plays the whole set
through once and the face bows out a fraction of a second into the second lap,
with the special case deleted and `EVENT_TIMEOUT` handled exactly as it always
was.

Sizing the content to the constraint beat overriding the constraint, and the
entry it cost was the one entry worth losing. A longer timeout setting now gets
more laps rather than a truncated one, which is the right way round for something
the wearer chose.

**The reel resets `breath`.** Only the first `PET_SNORE_AUDIBLE_BREATHS` breaths
of a sleep are voiced, and that counter belongs to the live pet's night. A pet
that had been snoring since 21:00 would have reached the reel already hoarse and
shown its sleep silently. Two audible breaths and two passes is not a
coincidence: `PET_SNORE_AUDIBLE_BREATHS` is 2, so a reset makes both passes of
`snore` sound.

`check_showcase.py` grew a second half to match. It still proves you can always
get back to the live pet — now from every one of the thirteen positions and by
five routes out, the new cancel included, 520 combinations — and it now also
proves the reel visits every entry in order for its allotted passes and wraps at
the end. That second assertion is what would catch an entry whose art never
reaches the end of its table: the reel would stop there for ever, and nothing
else would say so.

The whole change costs **152 bytes** of flash and **4 bytes** of RAM (PC, GCC
14.2: 135,336 → 135,488 text, `sizeof(pet_state_t)` 84 → 88), which is the
thirteen-byte running order plus the advance.

No entry gets a minimum dwell, and none needs one now that `dead` is out. A floor
on time would have been a second rule about timing inside a feature whose whole
premise is that the art decides how long it takes, and the only entry short
enough to want one was the one with nothing to animate.

### The wake animation's missing segment

Dan added `SEG_F` to cell 7 in two frames of `_pet_frames_wake`, having noticed
what looked like a gap left for it — `SEG_E    ` with trailing space, as if
something had been read and then dropped.

Nothing was dropped. `decode.py` prints each cell with `"%-9s"`, a flat
nine-character column, and `SEG_E` padded to nine looks exactly like room for
`|SEG_F`. Re-running `decode.sh` on `CasioPet_Animations-Wake.gif` reproduces
`SEG_E` alone, so the decoder read the art correctly and the art has one segment
there.

So it is a change to the art rather than a repair of a transcription, and it
stands: cell 7 has no tied pair, so `E` alone and `E|F` are both renderable, the
`wake` cue watches cell 7's `A|B|C|D` and is unmoved by either, and
`check_sounds.py` passes.

**Which settles what the GIFs are for.** They are reference, not source. The
table in `pet_face.c` is the art now, and `CasioPet_Animations-Wake.gif` is
behind it by one segment — deliberately, not by accident. `decode.sh` stays
exactly as useful for what it was built for, which is getting a drawing into the
file without transcribing it by hand; what changed is that re-running it on an
old export is no longer assumed to be safe. Check the diff before pasting.

---

## Session 20 — a shake worth the name

Three complaints from a week of wearing it, all of them about the pet reacting to
things the wearer had not done.

### One tap was a whole rung

The accelerometer was the loudest of the three. It interrupted meals, cut moods
short and put `PLAY 1` on screen while the watch was sitting on a desk — and the
reason is that the code took a single hardware tap as a shake. Movement sets the
LIS2DW's Z-axis tap threshold to 12, or 750 mg at the 2 g full scale it runs, and
750 mg is not much: a knock against a table, a hand going into a pocket, an arm
swung hard enough clears it. Each one climbed a rung.

The fix is a burst. `PET_SHAKE_TAPS` taps — three — have to land inside
`PET_SHAKE_WINDOW_SECONDS` of the first, no two of them closer together than
`PET_SHAKE_GAP_TICKS`. Short of that the count expires and nothing happened.

Three parameters rather than one, because each rejects a different thing:

- **The count** rejects the isolated knock. Nothing incidental produces three
  deliberate taps.
- **The window** stops the count accumulating over a morning. It runs from the
  first tap and is deliberately *not* extended by the ones after it, so it is a
  burst and not a slow drum; a tap an hour never adds up to a shake.
- **The gap** rejects one *hard* knock. This is the one that is easy to miss. The
  hardware's quiet period at 400 Hz is about 60 ms and its shock window 40 ms, so
  a single impulse ringing out through the case can report several taps in a row
  — and without a gap those three would have satisfied the count on their own,
  turning a sharp knock into a guaranteed shake rather than an unlikely one.
  Anything inside 250 ms is read as the same knock still sounding.

With three taps wanted, each can afford to be easier to land, so the face writes
its own threshold — `PET_SHAKE_THRESHOLD`, 8, or 500 mg — immediately after
`movement_enable_tap_detection_if_available` returns true. Deliberate tapping
then registers reliably and the count does the rejecting, which is the right way
round: a sensitive ear with a strict brain, rather than a deaf ear hoping the one
thing it hears was meant.

That override leaks nowhere. Every face that wants taps calls
`movement_enable_tap_detection_if_available` on its own activate and that writes
12 back, and the disable path zeroes the register outright. The return value is
now checked, so a board without an accelerometer is never written to.

**This is not the tap counting that Session 10 abandoned**, and the distinction is
worth keeping straight because the code looks similar. That build let the number
of taps choose the *rung*, which measured how hard the watch was shaken rather
than how long it was played with, and a single flick either registered once or
shot straight past the nausea limit. The ladder is still paced by the clock. What
the count decides now is only whether a shake happened at all — one burst,
however many interrupts the hardware makes of it, is exactly one rung.

### The reel could be shaken out of

The showcase reel is a performance, and watching one means holding the watch,
which meant motion cancelled it and started a play session underneath. Buttons
should cancel it — you asked for that. Being handled should not.

So the reel is deaf: `_pet_showcase_play` disables tap detection outright, and
every exit already runs through `_pet_rest`, which turns it back on for any pet
that is not dead. There was nothing to add on the restore side. `EVENT_SINGLE_TAP`
came off the showcase's cancel list, and the main handler drops taps while
`showcase_on` in case an interrupt was latched a moment before the disable.

Turning it off at the hardware rather than filtering in software also parks the
accelerometer's 400 Hz high-performance mode for the length of the reel, which is
the most expensive thing the face ever asks for.

### A pet that had just been sick would eat again immediately

The sitting cap from Session 17 stops the fifth pip staying down, but nothing
stopped you offering a sixth the same second — barf, barf, barf, a quarter tic
each time. And a pet that had just thrown up from rough play would take a full
plate without hesitating, which reads wrong whatever the arithmetic says.

Every barf now shuts the kitchen for `PET_BARF_SETTLE_SECONDS` — 30 minutes — or
until the sitting turns over, whichever comes first. A feed inside that is
refused: the pet grumbles, and nothing is charged, because the barf that shut the
kitchen has been paid for already.

It hangs off `_pet_barf_scene` rather than off the feeding code, which is what
makes "tie it globally to the barf" true rather than approximately true: the play
ladder's third rung and the fifth pip at the table reach the same function, so
they get the same cooldown without either knowing about the other. While there,
`_pet_barf_scene` also took over clearing the plate from `_pet_feed_barf`. That
fixed a real if minor leak — a play barf landing while pips were queued left them
on the plate, scene-less and uneaten, to be swallowed hours later by the next
feed press.

**Settling does not hand the sitting back**, and that is deliberate. Past the cap
the pet is done eating until the next sitting either way, so waiting out the 30
minutes after an overfeed buys a pip that is *taken* and then returned, for the
penalty and nothing more. Resetting the count instead would have made the cap
purchasable — four pips, a barf, half an hour, four more — which is the one thing
Session 17 was built to prevent. After a *play* barf, where the cap is untouched,
the same 30 minutes buys a meal that keeps. Both readings of "you can at least
try and keep a meal down again" are satisfied by the same rule.

The "or until the next sitting" half is almost always the looser of the two — a
sitting is five hours — so it only bites on a barf in the last half hour of one,
where a new meal window is a clean stomach and the clock would otherwise run past
it.

### What the harnesses had to say

`check_sounds.py` needed no change and proved its worth anyway: the refusal reuses
`PET_SOUND_GRUMBLE`, and the checker's direct-play scan picked that up by itself,
now reporting `played directly by _pet_disturb, _pet_feed_press`.

`check_balance.c` did not fail, and would not have — every routine in it feeds at
08:00, 13:00 and 19:00, three different sittings, so the cooldown never binds and
none of the numbers moved. That is exactly the gap worth closing, so it learned
the rule and gained a pair of visits inside one sitting to exercise it. `run()`
cannot express that — its visits are hours apart by construction — so
`settle_case` places two calls by hand.

The first draft of that read `3 -> 3` refused against `3 -> 5` accepted and
invited the conclusion that a settled second plate costs two quarter tics. It
costs one. The other is passive decay over the extra forty minutes, which the
refused case was too early to be charged. A third row, calling at the same moment
without feeding, prints the decay alone so the subtraction is on the page rather
than in the reader's head.

### The manual's arithmetic, made checkable

Section 3 of the manual was a nine-row table that mixed gains and losses together
and never added anything up. It is now four: what loses time, what gains time,
what costs nothing either way — that third one did not exist, and it is where the
confusion lives, since a hug past the cap and a play on cooldown both *look* like
they should matter — and the arithmetic those add up to.

Writing it turned up a number nobody had noticed. **One complete visit is worth
exactly −2.5 tics** (four pips, four hugs, one play) and **passive decay is
exactly +2.5 tics a day**. Those are equal. One full visit a day breaks even on
paper and loses only because the meal drops a pile twelve hours later. That is
the whole game in two lines, and it fell out of the tunables rather than being
designed in.

All of which is hand-derived from `#define`s, which is exactly the kind of thing
that goes quietly wrong the first time a buff is retuned — and this file has
already recorded one such drift, `sizeof(pet_state_t)` being given as 84 in §15
and 88 in §16 of the same document.

So `check_economy.py`, a sixth harness. It evaluates the tunables out of
`pet_face.h`, recomputes the whole economy, prints it as a report, and then
checks every number §3 prints against what it just computed. Retune a buff
without touching the manual and it fails, naming both values.

It earned its place on the first run, twice over. It caught that the over-shake
barf had been written as **+1.0** when `PET_BUFF_PLAY` is 2 quarter tics and the
penalty 1, making it **+0.75** — a real error, in a table whose whole purpose is
to be trusted. And on the run after that it failed again, this time because the
harness itself was formatting quarter tics to one decimal place: the manual was
right and the checker was wrong. Both are the check doing its job.

Proving it actually bites: changing `PET_HUG_CAP` from 4 to 5 fails five separate
figures — the hug ceiling, the visit total, and all three rows of the visits
table — each naming what the manual says against what the header now implies.

While registering it, §12 turned out to have been claiming "four harnesses" while
`check_showcase.py` had never been listed there at all. Six now, all listed.

### The hug gets two arms

Dan's idea, and it pays for itself twice.

The hug moves off `LIGHT`'s 0.5 s press and onto both buttons at once — an arm on
each side, which is the point. It fires on whichever of the two lands *second*
rather than waiting out a hold, so it answers immediately; holding both for a
moment happens by itself anyway.

**Movement has no chord support**, but it does not need to. The three buttons are
separate GPIO pins on separate interrupt channels (`BTN_LIGHT` PA30, `BTN_MODE`
PA31, `BTN_ALARM` PA02), tracked as three independent `movement_button_t`s with
their own timestamps, and both event streams reach the face. So the face tracks
`light_down` / `alarm_down` itself and fires on the second down. The simulator is
the same shape — `keydown`/`keyup` per key with auto-repeat filtered — so the
chord is testable there too.

The catch is the releases. Let go of a hug and, untreated, `LIGHT`'s release
feeds the pet while `ALARM`'s sweeps the floor, and if the chord was held past
half a second the presses behind them fire as well. So `chord_hugged` swallows
the rest of the gesture and clears once both buttons are up.

Getting that right meant knowing exactly how many ways a button can report a
release. There are two, not three: `EVENT_*_BUTTON_UP` under half a second and
`EVENT_*_LONG_UP` over it — the 1.5 s release included, because
`EVENT_*_REALLY_LONG_UP` is commented out of the enum and
`_process_button_event` returns `down_event + 3` for it. Had there been a third,
missing it would have left a button stuck down and fired a hug at the next press
of the other one.

**What it bought.** `showcase_interrupted` is gone, and so is everything that
existed to support it. That field only ever existed because the 0.5 s hug handed
the screen back on its way to the 1.5 s hold, so `showcase_on` had been cleared
by the very gesture trying to read it — and the fix needed clearing on
`EVENT_LIGHT_BUTTON_DOWN` rather than on the release, to stay ahead of Movement
draining each event batch in enum order with `LONG_UP` sorting before
`REALLY_LONG_PRESS`. All of that reasoning, the most fragile in the file, is
deleted. `_pet_showcase_toggle` is now three lines and tests `showcase_on`.

Reaching the reel is also free now. It used to cost a hug each way.

One route out of the showcase is no longer in the escape switch: the chord fires
on a `BUTTON_DOWN`, long before either release gets there, so `_pet_chord` hands
the screen back itself. `check_showcase.py` now reads that call out of the source
the same way it reads `_pet_showcase_exit` — drop it and the kiss plays underneath
a running reel, which none of the 520 escape simulations would see, since they
model the exit rather than the call that performs it. Removing the call fails the
check, as it should.

`light_hold` came off the harness's list of escapes, because it is not one any
more: `LIGHT` between 0.5 s and 1.5 s now does nothing at all. That is a small
dead zone, and it is the price of the 1.5 s hold above it being clean.

**128 bytes of flash and no RAM at all** — the three new flags replaced one
removed field and fitted in padding that was already there, so
`sizeof(pet_state_t)` stays 96.

### Cost

**472 bytes of flash** for the three fixes above, all `text` (138,424 → 138,896
on the Mac, GCC 15.3), plus **128** for the chord, and **12 bytes of RAM** —
`sizeof(pet_state_t)` 84 → 96, being nine bytes of new state and three of the
padding they fell into. The face now stands at 6,704 bytes, 2.73% of the budget.

---

## Session 21 — the same work, written once

An audit pass, at Dan's asking: the game is balanced the way he wants and the
face is well inside its budget, so the question was only whether anything is
said twice or gated twice. Four things were. None of them changes behaviour, and
together they give back **104 bytes**.

### The showcase stays at 1.5 s

First, the question that prompted this. Moving the reel's toggle down to LIGHT's
0.5 s press would delete one `case` label and move one call — about two lines,
and it would cost something real, because a feed press held a moment too long
would start the reel. The 1.5 s hold stays. The dead zone between 0.5 s and
1.5 s is not paying for anything, so it may as well be the hidden thing Dan
liked about it.

### Decay was written twice

Passive decay and an unswept poo are the same mechanism at their own rates on
their own clocks, and `_pet_catch_up` spelled both out: take the waking seconds
since the last charge, add the carried residual, divide, keep the remainder,
move the clock up, clamp, apply. Nine lines each, differing in three
identifiers. They are now one `_pet_charge_decay` taking a clock, a residual and
a rate.

Which puts a subtlety in one place instead of two. The `if (qt > PET_QT_DEAD)`
clamp reads as redundant, because `_pet_add_qt` already pins the result to
`0..PET_QT_DEAD` — but the argument is `int16_t` and `qt` is `uint32_t`, so
without the clamp a pet left alone for a few years would wrap the cast negative
and come back *healed*. The helper says so.

### The sitting was computed three times

`_pet_feed_tick`, `_pet_barf_scene` and `_pet_settling` each read the clock, ran
`_pet_meal_segment` on the hour, and compared a day and a segment as two
separate fields. Neither half means anything alone — a segment number repeats
every day — so they are now one `_pet_sitting_now()` returning both packed into
a `uint16_t`, and "still the same sitting" is one comparison instead of two.
`fed_day`/`fed_seg` and `barf_day`/`barf_seg` become `fed_sitting` and
`barf_sitting`. Zero is still "no sitting", since a real day is 1..31.

### Death was handled in three places

`_pet_rest` knows what a dead pet needs: the tombstone, the accelerometer off,
the knell exactly once. `_pet_enter` had its own copy of all of it, with a
comment explaining that it skips `_pet_rest` and so must do these things itself.
It calls `_pet_rest` now. The behaviour is identical because `_pet_enter`
already seeds `shown_mood` before the branch, which is what keeps the mood step
silent on a visit that opens on a change you were not there for.

There are still three ways to ask whether the pet is dead — `scene ==
PET_SCENE_DEAD`, `_pet_mood(s) == PET_MOOD_DEAD`, `quarter_tics >= PET_QT_DEAD`
— and they are deliberately not collapsed, because they answer different
questions: what is on screen, what the counter says, and whether the arithmetic
should run at all. They can disagree for up to `PET_NIGHT_AWAKE_SECONDS`: a poke
that kills a pet at night leaves the scene at `NIGHT_AWAKE` until the settle
timer reaches `_pet_rest`. That is not a bug worth fixing — the pet grumbles,
then dies, which reads better than dying mid-grumble.

### Smaller things

`free_to_play` was a six-line `#if`/`#else` producing a `bool` in one build and
a `const bool` in the other, to avoid reading a field that is always false when
`PET_SHOWCASE` is 0. The field is in the struct either way; the test is now
inline and the preprocessor block is gone.

`_pet_layer_tick` had `a->frames ? a->count : 1`, a fallback it cannot reach:
`PET_ANIM_NONE` returns two lines above and is the only animation without
frames. The same fallback in `_pet_frame_hold` *is* reachable, through
`_pet_layer_play(l, PET_ANIM_NONE)` which is how a layer is cleared, and its
comment claimed to be about "label-only animations" — a kind of animation that
does not exist. Both now say what they are.

`_pet_draw` indexed `_pet_anims` and then decided whether to skip the layer.
Harmless, since `PET_ANIM_NONE` is a real table entry, but the wrong way round.

### What was left alone

The animation queue holds four and never more than two — `_pet_enter` is the
only caller, queueing a wake and a mood. Collapsing it to a single slot saves
nothing in RAM, since the struct pads to 96 either way, and would make any
future three-step scene a rewrite rather than an extra `_pet_queue_anim`.

`_pet_invalidate` is a one-line wrapper around a flag, kept because it names the
intent at both call sites.

`_pet_enter` still reads the clock twice, once directly and once inside
`_pet_catch_up`. Threading it through would change two signatures to save one
RTC read per activate.

    139,024 -> 138,920 text, data and bss unmoved
    sizeof(pet_state_t) 96, unchanged

All six harnesses pass unchanged, which is the point of the exercise: the only
visible difference is `check_sounds.py` now reporting the death knell as played
by `_pet_rest` alone rather than by `_pet_rest` and `_pet_enter`.
