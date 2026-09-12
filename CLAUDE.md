# second-movement — working notes

Fork of [joeycastillo/second-movement](https://github.com/joeycastillo/second-movement)
(firmware for the [Sensor Watch](https://www.sensorwatch.net), a SAML22 board that
replaces the guts of a Casio F-91W). `origin` is the fork, `upstream` is Joey's repo.

The active project is **`pet_face`** — a Tamagotchi-style virtual pet, built as a
CS50x final project. Dan's design is the pseudo code in
[CS50x Final Project.md](_cs50ref/CS50x%20Final%20Project.md); read it before
changing pet behaviour. The LCD segment reference is
[SEGMENT_MAP.md](_cs50ref/SEGMENT_MAP.md) — which also holds the region map
saying which cells each part of the pet owns — and [DEVLOG.md](_cs50ref/DEVLOG.md)
is the process log, appended to as work lands. Every ambiguity in the spec has
been settled, with the reasoning recorded where it applies; `TODO` in
`pet_face.c` marks the work still outstanding.

Animations are drawn as segment art on an F-91W template and exported as GIFs.
They are not transcribed by hand: the frames rotate back to the native layout,
which locates every segment geometrically and decodes to masks. `ffmpeg` splits
the GIF (reading one directly only ever yields its first frame). See the
Session 6 devlog entry for the pipeline and its calibration.

Development happens on two machines — a Windows 11 PC (primary) and a Mac.

## Building

`BOARD` and `DISPLAY` are mandatory; the Makefile errors out without them. Dan's
board is `sensorwatch_pro` with the `classic` (F-91W) LCD.

```sh
# hardware
make BOARD=sensorwatch_pro DISPLAY=classic -j8

# simulator — HTML target ONLY, see gotcha 2
emmake make -j8 BOARD=sensorwatch_pro DISPLAY=classic build-sim/firmware.html
python3 -m http.server -d build-sim 8000   # → localhost:8000/firmware.html
```

Flashing: double-tap the reset button on the back of the board until the LED
pulses red and a `WATCHBOOT` drive appears.

- **Mac:** `make install` works directly — `uf2conv.py` scans `/Volumes`.
- **PC:** copy `build\firmware.uf2` onto the `WATCHBOOT` drive from Windows;
  `make install` from inside WSL cannot see the drive.

### Toolchains

| | PC (WSL2 Ubuntu, repo at `/mnt/d/code/second-movement`) | Mac (repo at `~/code/second-movement`) |
|---|---|---|
| ARM GCC | apt `gcc-arm-none-eabi` | ARM GNU Toolchain 15.3.rel1 in `~/arm-gnu-toolchain-*/bin` |
| emscripten | emsdk in `~/emsdk`, auto-loads via `.bashrc` | emsdk 6.0.9 in `~/emsdk`, auto-loads via `.zshrc` |

Use **emsdk**, never a distro/Homebrew emscripten package — emsdk is what CI
(`emscripten/emsdk` image) uses. Apple's `make` 3.81 is fine; the build system
uses no GNU Make 4 features.

The repo also ships [flake.nix](flake.nix) + [.envrc](.envrc) for a nix devshell.
Not currently used on either machine.

## Gotchas

**1. No header dependency tracking.** `CFLAGS` builds its `-MT`/`-MF` flags from
`$(*F)`/`$(@F)` ([gossamer/make.mk:66](gossamer/make.mk#L66)), which expand empty
at eval time, so no per-object `.d` files are ever written and gossamer's
`-include $(wildcard *.d)` matches nothing. **Editing a header rebuilds nothing.**
This bites hardest via `--gc-sections`: adding a face to `movement_config.h` and
rebuilding produced bit-identical firmware, because a stale `movement.o` never
referenced the new face and the linker silently dropped it.

> After editing any header, `touch movement.c` (or whatever `.c` includes it), or
> `make clean`. Editing only `.c` files is safe. Verify a face actually linked:
> `arm-none-eabi-nm build/firmware.elf | grep pet_face`

**2. The emscripten `all` target races under `-j`.** It links both
`firmware.elf` and `firmware.html`, and each emcc link writes its own
`firmware.wasm` side file — concurrently, clobbering each other. Symptoms are a
missing wasm or `zero length section` at objcopy. Always name the HTML target
explicitly, as in the command above.

**3. Flash budget.** Usable flash is `0x40000 - 0x2000 (bootloader) - 0x2000
(eeprom)` = **245,760 bytes**. Too many faces in `movement_config.h` is a
compile error, not a runtime surprise. Current build on the PC (GCC 14.2):
132,752 text + 2,044 data = 134,796 (55%, ~108 KB free). RAM is 32 KB.

Expect the two machines to report *different* sizes — the ARM GCC versions
differ. GCC 15.3 also emits warning classes the PC's older GCC doesn't (e.g.
`-Wunterminated-string-initialization` in `kitchen_conversions_face.c`). Nothing
uses `-Werror`; don't chase warnings that only appear on one machine.

**4. `template/watch_face.py` corrupts the files it edits.** Its
`update_include_file()` does `seek(0)` + `writelines` with no `truncate()`, so
if the new content is shorter than the old, stale bytes dangle past the end —
usually surfacing as `watch-faces.mk: *** missing separator`. After scaffolding a
face with it, check the tails of `movement_faces.h` and `watch-faces.mk` by hand.
*(Worth an upstream PR: add `file.truncate()`.)*

## Working across the two machines

Work goes straight onto `main`, tracking `origin`. Push before leaving a machine,
`git pull --rebase` on arrival, and push a `wip` branch rather than relying on
`git stash` — stashes don't travel. `make clean` after pulling the other
machine's work, per gotcha 1.

Line endings: the PC's repo lives on `/mnt/d`, shared between WSL and Windows git
(`autocrlf=true`), and a CRLF working tree has already caused one bug (gotcha 4).
The Mac is LF-native and won't introduce CRLF, but it will faithfully receive it.
Prefer `core.autocrlf=false` on the Windows-side git. Do **not** add a
`.gitattributes` with `* text=auto eol=lf` — `legacy/`, `lib/TOTP/`, and
`blackjack_face.c` are genuinely CRLF *in the index* upstream, so it would
renormalize them into a large spurious diff against `upstream`.
