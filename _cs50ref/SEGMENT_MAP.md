# Classic LCD Segment Constraints — Design Reference

Compiled from `Classic_LCD_Display_Mapping` in
`watch-library/shared/watch/watch_common_display.h` and the driver code.
Companion to the labeled schematic ([positions-labeled.png](positions-labeled.png)).

Segment letters use the standard 7-segment convention, upright orientation:

```
    AAA
   F H B
    GGG     H is the centre VERTICAL stroke, and only positions 0-1 have it.
   E H C    Upright, G|H draws a plus sign and A|H draws a letter T — the
    DDD     firmware's own character set maps '*' to G|H and calls it "the +
            sign for use in position 0".
```

Rotate the display 90° clockwise to read the face sideways and those two swap
roles: **G becomes the centre vertical and H the centre horizontal**. So a minus
sign as the wearer sees it is `H` alone, not the `G` the firmware uses for '-'
in upright text.

## The one-line rules

1. **Tied segments share one electrical address** — turning one on turns the whole
   group on. You cannot untie them in software.
2. **The colon is ONE segment** — both dots on/off together (`watch_set_pixel(1, 16)`).
3. Everything is binary — no dimming, no grayscale.
4. Only position 7 can blink by itself; only position 8 has the built-in
   tick/tock animation. Everything else animates on CPU ticks.

## Per-position constraint table

| Pos | Where | Independent controls | Tied / missing |
| --- | --- | --- | --- |
| 0 | weekday left | **8** — A B C D E F G + H | none — the most capable digit |
| 1 | weekday right | 6 — A, B+C, D, E+F, G, H | **B tied to C**, **E tied to F** |
| 2 | day tens | **4** — A+D+G, B, C, E | **all three horizontals are ONE control**; F doesn't exist |
| 3 | day ones | 7 — full | none |
| 4 | hours tens | 6 — A+D, B, C, E, F, G | **A tied to D** |
| 5 | hours ones | 7 — full | none |
| : | colon | **1** — both dots together | can't wink, can't move |
| 6 | minutes tens | 6 — A+D, B, C, E, F, G | **A tied to D** |
| 7 | minutes ones | 7 — full | **hardware auto-blink** (all segments except B; 50 ms–4.25 s period; keeps blinking in sleep mode) |
| 8 | seconds tens | 7 — full | **hardware tick/tock animation on D+E** (also the system's low-energy sleep indicator) |
| 9 | seconds ones | 7 — full | none |

Only positions 0 and 1 have an H segment; 2–9 have no H at all, and position 2
has no F either. Verified against `Classic_LCD_Display_Mapping` by comparing
segment addresses per position.

Indicators (SIGNAL, BELL, PM, 24H, LAP) are each one segment. The five-bar
SIGNAL "fan" lights as a unit.

## Sizes and geometry (from the schematic)

- Positions 4–7 are the large digits; 8–9 are smaller and raised; 0–3 smaller
  still, in the top row. The colon sits between positions 5 and 6.
- Digit shapes are italic/slanted — sideways faces will have a slight lean.

## The face reads sideways (rotated 90° clockwise, position 4 at top)

**This is the orientation.** The animations are all drawn for it; the upright
alternative is off the table. Confirmed against the drawn exports, which rotate
counter-clockwise back to the native F-91W layout exactly.

Reading top-to-bottom becomes: 4, 5, **colon (eyes)**, 6, 7, then the small 8, 9.
The old top row (weekday, day, indicators) becomes a right-hand column.

Segment orientation remaps (rotate CW: top→right, right→bottom, bottom→left, left→top):

| Upright segment | Sideways becomes |
| --- | --- |
| A (top) | right vertical |
| B (top-right) | bottom edge, right half |
| C (bottom-right) | bottom edge, left half |
| D (bottom) | left vertical |
| E (bottom-left) | top edge, left half |
| F (top-left) | top edge, right half |
| G (middle) | center vertical |

Handy sideways mouth shapes on a full digit (5, 7, 8 or 9):

- **flat mouth (high)**: E+F — a horizontal line near the eyes
- **flat mouth (low)**: B+C
- **open mouth**: all outer segments ("0")
- **half-open**: E+F+A+D (top line + both sides = open rectangle missing bottom)
- position 6 directly under the eyes has A tied to D (its two *verticals*, once
  rotated) — mouth shapes there always get both sides or neither; fine for
  symmetric mouths, no lopsided smirks in that cell

The trade-off this orientation makes: the eyes gain nothing but position — a
fixed pair of dots that can only go on and off together — while the mouth gains
a full 7-segment cell of expression range.

One consequence to design around: the colon is the one thing on the classic LCD
that **cannot** blink autonomously. Since the colon is the eyes, every blink is a
CPU-drawn frame at `PET_ANIM_HZ`, and the pet stops blinking once the watch drops
into sleep mode. Position 7 *can* blink in hardware and keeps going in STANDBY,
but only as a whole character from a fixed list rather than a mask, so using it
means handing that cell to the hardware entirely.

## Regions

Each area of the screen is owned by one system, so they animate independently
instead of needing bespoke art for every combination. Drawn content must stay
inside its own cells.

| Region | Cells |
| --- | --- |
| Character | 1, 4, 5, 6, 7, 8, 9, colon |
| Pile | 9's `G B C` — what the poo leaves behind, until it is swept |
| Food pips | 3, filling `B`, `C`, `F`, `E` in that order |
| Buff / debuff | 0 — plus is `G\|H`, minus is `H` |
| Sound | SIGNAL, flashed whenever a sound plays |
| unused | 2, PM, 24H, LAP |

Position 9 is shared rather than split. The pile only ever wants its centre and
bottom edge, but the character reaches across all of it: worn sideways the main
line runs top to bottom with 9 at the far end, and the resurrect animation rises
the spirit up from there through the whole line. Clipping that would cut the
entrance in half. Sharing costs nothing because compositing is a
straight `OR` — neither region can erase the other — and the two are never both
drawing there anyway, since the pet only resurrects with the floor clean.

Poo and barf are drawn as whole scenes, the pet and the floor together, so they
play as character animations like any other; only the pile that outlives them
belongs to the status region.

Position 1 sits off the mouth for snores and kisses, and is the one character
cell with ties — `B`+`C` and `E`+`F` are whole edges once rotated.

## Frame notation, with worked examples

Convention: `position:SEGMENTS`, space-separated; tied groups written as their
group (`AD`, `BC`, `EF`, `ADG`); `:on`/`:off` for the colon; indicators by name;
positions left out of a full-screen frame are off. Duration in ticks at the end.

**Everything off** (a blank frame):

```text
(all clear)
```

**Everything on** (matches the all_segments_face demo / the '@' character):

```text
0:ABCDEFGH  1:A,BC,D,EF,G,H  2:ADG,B,C,E  3:ABCDEFG
4:AD,B,C,E,F,G  5:ABCDEFG  :on  6:AD,B,C,E,F,G  7:ABCDEFG
8:ABCDEFG  9:ABCDEFG  +SIGNAL +BELL +PM +24H +LAP
```

(Position 2 has no F segment to light; commas mark the independent controls
within a position — e.g. position 6 has six switches, not seven.)

**Friday the 25th, 12:34:56** — what the stock clock face displays as `FR 25 12:34 56`:

```text
0:AEFG          <- F
1:A,BC,EF,G,H   <- R (everything but D; H is the centre vertical)
2:ADG,B,E       <- 2 (works here because 2 needs all three horizontals anyway)
3:ACDFG         <- 5
4:BC            <- 1
5:ABDEG         <- 2
:on
6:ABCDG         <- 3 (A and D are tied, but 3 uses both — no conflict)
7:BCFG          <- 4
8:ACDFG         <- 5
9:ACDEFG        <- 6
```

(Add `+PM` if it's afternoon in 12-hour mode. Note how often the quirks turn out
harmless: several digits happen to want both halves of a tied pair anyway. The
ones that bite are shapes needing *one* half — a top-only bar in position 6, a
lone right-top in position 1.)
