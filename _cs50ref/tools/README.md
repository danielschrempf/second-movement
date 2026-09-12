# Tools

Small things that exist to keep the pet honest. None of them are part of the
firmware build; they are run by hand when there is something to convert or a
claim to re-check.

## Converting animations — `decode.sh`

The animations are drawn as segment art on a faithful F-91W template, rotated 90°
clockwise for the sideways face, and exported as GIFs. They are **not**
transcribed by hand.

```sh
./decode.sh happy.gif happy            # prints a pet_frame_t table
./decode.sh happy.gif happy --check    # ... plus the tied-segment report
```

Rotating the frames back counter-clockwise puts the watch in its native layout,
where every cell sits in a known place, so segments are located geometrically
rather than guessed at. Consecutive identical poses collapse into one held frame,
which is exactly the `hold` field — so the output is paste-ready.

Always pass `--check` on new art. It reports any frame that lights one half of a
tied pair on its own: those pairs share a single electrical address, so such a
frame **cannot be rendered as drawn**. It is the most likely way segment art goes
wrong and the cheapest thing to catch early.

### What it assumes

The calibration in `decode.py` is coordinate-based: a 480×480 canvas with the
template in the position the first combined export used. Keep the alignment
identical between exports and it is a one-time setup. If the art shifts, the
`CELLS` table needs rebuilding — run `segmap.c` directly to print the component
boxes and recluster them:

```sh
cc -O2 -o segmap segmap.c
ffmpeg -i anim.gif -vf transpose=2 -pix_fmt gray -f rawvideo native.gray
./segmap native.gray <frame-count>          # component table on stdout
```

`segmap.c` finds every pixel that is dark in some frames and light in others —
those are the segments that animate — and groups them into connected components.
Note that segment H comes back as *two* components, because G crosses and splits
it; `decode.py` rejoins them.

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
| `check_layers.c` | No two layers claim the same segment, both non-animated regions are clear of every layer, position 9 is fully allocated across its two owners, and a rogue frame gets clipped | The region map or `_pet_layers` changes |
| `check_awake_time.c` | The waking-seconds accounting is monotonic and additive, and handles spans over whole nights and both day boundaries. Also prints how long neglect takes to kill the pet | `PET_HOUR_WAKE` / `PET_HOUR_SLEEP` or the decay rate change |
| `check_balance.c` | Simulates a week of care at different check-in rates and feed timings, printing the mood trajectory | Any tunable in the buff/debuff block changes |

`check_layers.c` and `check_awake_time.c` copy their constants from the
firmware. They are **not** wired to it, so a tunable changed in `pet_face.h` will
not fail these until it is changed here too — check both if a number moves.

## Testing the decoder

There is no sample GIF committed, to keep the repo lean. Any export from the
animation set works; the calibration was built from the first combined one.
