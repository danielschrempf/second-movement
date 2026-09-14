# Tools

Small things that exist to keep the pet honest. None of them are part of the
firmware build; they are run by hand when there is something to convert or a
claim to re-check.

## Converting animations — `decode.sh`

The animations are drawn as segment art on a faithful F-91W template, rotated 90°
clockwise for the sideways face, and exported as GIFs into
[`../FaceAnimations`](../FaceAnimations). They are **not** transcribed by hand.

```sh
./decode.sh happy.gif happy            # prints a pet_frame_t table
./decode.sh happy.gif happy --check    # ... plus the render report
./decode.sh --all ../FaceAnimations    # every GIF in the folder, checked
```

Rotating the frames back counter-clockwise puts the watch in its native layout,
where every cell sits in a known place, so each segment is read straight out of
the pixels. Consecutive identical poses collapse into one held frame, which is
exactly the `hold` field — so the output is paste-ready. With no name given, the
name is taken from the filename (`CasioPet_Animations-Play-1.gif` → `play_1`).

Always pass `--check`. It reports two things:

- **Tied segments.** Some segments share one electrical address and light
  together whether the art wants it or not. A frame that lights one member alone
  **cannot be rendered as drawn**, and this is the most likely way segment art
  goes wrong.
- **Cells used.** Which cells the drawing actually touches, so art that strays
  outside its region shows up here instead of being silently masked off by the
  compositor. Compare it against the region table in
  [SEGMENT_MAP.md](../SEGMENT_MAP.md).

Blank frames at either end of an export are dropped, and the note says so. They
are an artefact of the drawing program rather than a pause the animation asked
for: on a looping mood a blank tail reads as the pet vanishing every cycle.
Interior blanks are kept — those are deliberate, and they are how a flash or a
fade-out gets drawn. `--keep-blanks` turns the trimming off.

### How segments are located

From the LCD's own geometry. `CELLS` in `decode.py` holds one measured box per
cell; everything inside a cell follows from the fixed seven-segment layout, and
only positions 0 and 1 have the centre vertical `H` at all.

An earlier version grew the map out of connected components found in each file,
and it was wrong in two ways that both failed silently:

- a segment held lit for a whole animation never changes, so it was invisible,
  and the pet's static parts decoded as blank;
- neighbouring segments that touch merge into one blob and get a single label,
  which produced `H` assignments in positions that have no `H`.

If the canvas shifts, `CELLS` needs re-measuring. `segmap.c` is kept for that:
it prints the bounding boxes of everything that moves, which is enough to read
the cell boundaries off.

```sh
cc -O2 -o segmap segmap.c
ffmpeg -i anim.gif -fps_mode passthrough -vf transpose=2 -pix_fmt gray -f rawvideo native.gray
./segmap native.gray <frame-count>          # component table on stdout
python3 decode.py --describe                # what the current boxes imply
```

`-fps_mode passthrough` is not optional. Without it ffmpeg resamples the GIF to
a constant rate and silently duplicates frames — the 16-frame exports came out
as 48 — which stretches every `hold` in the decoded table.

### Running it on the PC

Neither shell has the whole toolchain: `ffmpeg` is installed Windows-side, the
compiler and `python3` are in WSL. Run `decode.sh` **from WSL** and it reaches
across for `ffmpeg.exe`, putting its scratch directory on `/mnt/d` so both sides
can see it. On the Mac everything is native and it just runs.

## Checks

Compile and run; each prints a pass/fail report and exits non-zero on failure.

```sh
cc -O2 -o check_layers check_layers.c && ./check_layers
```

| | What it proves | Re-run when |
| --- | --- | --- |
| `check_layers.c` | The cells drawn from state — the buff sign and the food pips — are off limits to every layer, the two layers overlap only in cell 9 where that is intended, and a rogue frame gets clipped | The region map or `_pet_layers` changes |
| `check_awake_time.c` | The waking-seconds accounting is monotonic and additive, and handles spans over whole nights and both day boundaries. Also prints how long neglect takes to kill the pet | `PET_HOUR_WAKE` / `PET_HOUR_SLEEP` or the decay rate change |
| `check_balance.c` | Simulates a week of care at different check-in rates and feed timings, printing the mood trajectory | Any tunable in the buff/debuff block changes |
| `check_sounds.py` | Every sound cue describes a moment the art actually reaches, no cue repeats faster than its own sound can play, no sound is left unreachable, and every animation has a frame table | **Any animation is redrawn**, or a cue or sound is edited |
| `check_showcase.py` | From every position in the showcase walk, leaving it by any route puts the live pet back on screen showing its real mood, with nothing on the floor it did not put there | The showcase, `_pet_layer_tick` or `_pet_rest` change, or an animation is added to the walk |

`check_layers.c` and `check_awake_time.c` copy their constants from the
firmware. They are **not** wired to it, so a tunable changed in `pet_face.h` will
not fail these until it is changed here too — check both if a number moves.

`check_showcase.py` is half and half. It parses the animation table out of
`pet_face.c`, and it reads the one behaviour it is actually testing — whether
leaving the showcase hands the screen back — out of the source too, so reverting
that fails the check rather than being quietly modelled away. Everything else,
`_pet_layer_tick` and `_pet_rest` included, is a transcription: change those and
change them here.

## Measuring — `redraw_cost.py`

Not a check; it prints numbers. `_pet_draw` keeps a shadow of what it last wrote
to the LCD and pushes only the cells that changed, and this says what that is
worth by replaying every animation and counting.

```sh
python3 redraw_cost.py            # defaults to ../../watch-faces/complication/pet_face.c
```

The output backs the figures in [MANUAL.md](../MANUAL.md) §16. Re-run it when
the art changes or the compositor does, and update §16 if the numbers move.

`check_sounds.py` is the exception: it parses `pet_face.c` directly, so it cannot
go stale.

```sh
python3 check_sounds.py            # defaults to ../../watch-faces/complication/pet_face.c
```

Sounds are attached to animations as **cues** — "when cell 9's bottom edge
lights" rather than "1.125 seconds in" — so redrawing an animation carries the
sound along with it. That removes timing drift by construction, but it trades it
for a quieter failure: a cue whose condition never comes true simply never
fires, and nothing goes wrong loudly. This replays each animation and reports
where every cue actually lands, which is the only way to see that.

It also catches a cue firing more often than intended. A segment flickers as
something moves through its cell, so a condition can come true several times in
one pass; `once` in the cue table suppresses the repeats, and this reports both
what fired and what was suppressed so the choice stays visible.
