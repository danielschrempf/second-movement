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
- [ ] Design the eye/animation data tables
- [ ] Design pet state machine (hunger/waste/mood/sickness/age + timestamps)
- [ ] Compose the six chirps

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

Chose to sketch the complete design before writing more code. Drafted
[DESIGN.md](DESIGN.md): screen allocation around the classic LCD's shared-segment
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
