/*
 * MIT License
 *
 * Copyright (c) 2026 Daniel Schrempf
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#pragma once

#include "movement.h"

/*
 * CASIO PET
 *
 * A virtual pet watch face, read with the watch turned 90 degrees clockwise:
 * the colon is the pet's eyes and the big digits below it are its mouth. See
 * the frame section further down for the segment geometry that implies.
 *
 * The pet has one mood meter, measured in "tics": it climbs while the pet is
 * neglected and drops when you look after it.
 *
 *   tic < 2  Happy      tic < 3  Confused     tic < 4  Upset
 *   tic < 6  Angry      tic >= 6 Dead (until resurrected)
 *
 * Controls
 *   Light  short  Feed (queues up to 4 pips; eats after a 3 s pause)
 *   Light  long   Hug
 *   Alarm  short  Sweep poo
 *   Alarm  long   Resurrect (only while dead)
 *   Shake         Play (accelerometer; simulator: Alarm long while alive)
 *   Mode          reserved by Movement — next face
 *
 * The pet sleeps 21:00–05:00. Nothing decays while it does; disturbing it
 * costs tics and earns no buff. While you're on another face nothing runs;
 * time is caught up lazily the next time the face is activated.
 *
 * Spec: _cs50ref/CS50x Final Project.md
 *
 * Markers used in pet_face.c:
 *   TODO    work that has to happen before this can run for real
 *   DECIDE  the spec is ambiguous or silent; a default is in place
 *
 * As of 2026-09-12 no DECIDE markers are left — every ambiguity in the spec has
 * been settled and the reasoning recorded at the point it applies. What remains
 * is TODOs: the animation frames, the four sounds, the on-screen food queue,
 * and a battery measurement.
 */

// ---- Tunables ---------------------------------------------------------------

// Mood is stored in quarter tics so the 0.25 steps in the spec stay integers.
#define PET_QT_PER_TIC              4
#define PET_TIC(n)                  ((n) * PET_QT_PER_TIC)
#define PET_QT_DEAD                 PET_TIC(6)

// Mood thresholds, in quarter tics (see the table above).
#define PET_QT_CONFUSED             PET_TIC(2)
#define PET_QT_UPSET                PET_TIC(3)
#define PET_QT_ANGRY                PET_TIC(4)

// Passive time: "accumulate 1 tic every 6 hrs" while resting. Only waking
// hours count — see PET_AWAKE_SECONDS_PER_DAY below.
#define PET_SECONDS_PER_TIC         (6 * 60 * 60)
#define PET_SECONDS_PER_QT          (PET_SECONDS_PER_TIC / PET_QT_PER_TIC)   // 1.5 h

// "1 tic every missed day" without feeding.
#define PET_MISSED_FEED_SECONDS     (24 * 60 * 60)

// Buffs and debuffs, in quarter tics.
#define PET_BUFF_PLAY               2   // -0.5 tic
#define PET_BUFF_HUG                1   // -0.25 tic per hug ...
#define PET_HUG_CAP                 4   // ... up to -1.0 tic, per calendar day
#define PET_BUFF_EAT                1   // -0.25 tic per pip eaten
#define PET_DEBUFF_DISTURB          1   // +0.25 tic each time sleep is disturbed
// Shaking past PET_NAUSEA_LIMIT makes the pet barf: it gives back the play
// buff it just earned and costs another 0.25 on top, so over-shaking is worse
// than never having played. Decided 2026-09-12.
#define PET_DEBUFF_BARF             1

// Poo appears this long after a pip is eaten. The spec says both "0.25 tic
// after every feed" (1.5 h) and, in Feed(), "poo countdown (2 tic)" (12 h).
// Decided 2026-09-12: 12 h, the more explicit of the two. At 1.5 h a poo is
// sitting on screen almost whenever you have fed, and since an unswept poo
// doubles the decay rate, feeding in the evening cost more than it gave.
#define PET_POO_DELAY_SECONDS       (2 * PET_SECONDS_PER_TIC)
// An unswept poo adds +1 tic per tic of waking time — same rate as passive
// decay, so a poo left sitting doubles the rate.
#define PET_POO_SECONDS_PER_QT      (PET_SECONDS_PER_QT)

// Feeding
#define PET_FOOD_MAX                4
#define PET_FEED_SETTLE_SECONDS     3   // pause after the last press before eating starts
#define PET_FEED_PIP_SECONDS        1   // between pips

// Play
#define PET_PLAY_WINDOW_SECONDS     5
#define PET_NAUSEA_LIMIT            3   // more motion than this inside the window -> barf
// Shaking always plays with the pet, but the buff only lands once per cooldown.
// Without this, play was the one uncapped source of relief — hugs are capped
// and feeding is self-limiting via poo — so a shake every 5 s healed the pet
// from death's door in about a minute. At 2 h that's ~8 buffs per waking day,
// -16 quarter tics against a ~10.7 daily cost: generous, but not infinite.
#define PET_PLAY_COOLDOWN_SECONDS   (2 * 60 * 60)

// Day parts (local time, 24 h clock)
#define PET_HOUR_WAKE               5   // 05:00 morning starts
#define PET_HOUR_AFTERNOON          10  // 10:00 afternoon starts
#define PET_HOUR_SLEEP              21  // 21:00 night starts

// Decided 2026-09-12: neither passive decay nor an unswept poo accrues while
// the pet is asleep — you are not neglecting a pet that is in bed, and the
// +0.25 disturb penalty already covers interrupting it. So only these many
// seconds of each day count against the pet. An entirely ignored pet still
// dies, in ~54 h of wall clock rather than ~36 h.
#define PET_AWAKE_SECONDS_PER_DAY   ((PET_HOUR_SLEEP - PET_HOUR_WAKE) * 60 * 60)

// The spec doesn't say when a disturbed pet settles back down. Long enough to
// interact with it once more, short enough that it clearly wants to sleep.
#define PET_NIGHT_AWAKE_SECONDS     30

// How often the pet snores while you're watching it sleep. The spec's snore is
// an animation; a single beep on nodding off is easy to miss, so it repeats.
#define PET_SNORE_PERIOD_SECONDS    6

// Tick rate while the face is on screen. Every timer above is counted in
// these ticks; the pet is drawn at most this often.
#define PET_ANIM_HZ                 8

// How long a transient mark stays up: the buff/debuff sign, the dinner bell,
// the sound indicator. Long enough to read, short enough not to linger.
#define PET_FLASH_TICKS             (PET_ANIM_HZ)           // 1 s
#define PET_BELL_TICKS              (PET_ANIM_HZ / 2)       // 0.5 s

// Cells the non-animated layers live in.
#define PET_BUFF_POSITION           0   // plus / minus sign
#define PET_FOOD_POSITION           3   // the four pips

// Development controls, for reviewing art and moods without waiting on the
// clock — checking that the Angry animation looks right shouldn't mean
// neglecting the pet for most of a day.
//
//   LIGHT held 1.5 s   step to the next animation and hold it on screen, one
//                      at a time through all fourteen, then back to the live pet
//   ALARM held 1.5 s   push the mood up one tic, wrapping past dead to blissful
//
// Both also fire their normal 0.5 s long-press on the way past — a hug, a
// resurrect — because Movement delivers that before the 1.5 s event. Harmless
// while previewing: the mood stepper overrides the state anyway.
#define PET_DEBUG_CONTROLS          1

// ---- Frames -----------------------------------------------------------------

// THE FACE READS SIDEWAYS — the watch is turned 90 degrees clockwise, so the pet
// stacks down the screen as 4, 5, (colon = the eyes), 6, 7, with the smaller 8
// and 9 off to one side. Settled 2026-09-12: every animation is drawn for this
// orientation, and the upright alternative is off the table.
//
// Segment bits keep the hardware's upright lettering, because that is what the
// driver's mapping tables use. But when you author a frame you are thinking in
// the rotated view on the right:
//
//    upright (what the bits are named)     sideways (what you actually see)
//
//           AAA                                  EEE FFF     <- top edge
//          F   B                                 D  G  A     <- left vertical,
//           GGG                                  D  G  A        centre vertical,
//          E   C                                 D  G  A        right vertical
//           DDD                                  CCC BBB     <- bottom edge
//
//      A -> right vertical        E -> top edge, left half
//      D -> left vertical         F -> top edge, right half
//      G -> centre vertical       C -> bottom edge, left half
//                                 B -> bottom edge, right half
//
// So in a mouth cell: E|F is a flat line high (nearest the eyes), B|C a flat
// line low, A|B|C|D|E|F an open "0" mouth, A|D|E|F an open rectangle missing
// its bottom. H, which only positions 0 and 1 have, is the centre vertical
// stroke: upright, G|H is a plus sign and A|H is a letter T.
//
// Per-cell quirks that constrain the art. Full table in _cs50ref/SEGMENT_MAP.md:
//
//   4   top of the head     A tied to D: both verticals together, or neither
//   5   full 7 segments     the most expressive cell above the eyes
//   :   THE EYES            one single segment — both dots always move together,
//                           and the classic LCD cannot blink them in hardware,
//                           so every blink is CPU-driven from the tick handler
//   6   just below the eyes A tied to D: symmetric mouths only, no lopsided smirks
//   7   below that          full 7, and the only position with autonomous blink
//   8   small, off-axis     full 7; its D+E carry the hardware tick/tock, which
//                           the system also borrows as its sleep indicator
//   9   small, off-axis     full 7
//
// The position 7 blink is tempting for snoring — it runs with no CPU and keeps
// going in STANDBY and sleep mode — but watch_start_character_blink() takes a
// *character*, not a segment mask, and only a handful blink cleanly (segment B
// cannot: 5, 6, b, C, c, E, F, h, i, L, l, n, o, S, t and some punctuation).
// watch_stop_blink() also clears position 7 outright. Mixing it with the frame
// renderer below means giving that one cell over to the hardware entirely.
//
#define SEG_A   (1 << 0)
#define SEG_B   (1 << 1)
#define SEG_C   (1 << 2)
#define SEG_D   (1 << 3)
#define SEG_E   (1 << 4)
#define SEG_F   (1 << 5)
#define SEG_G   (1 << 6)
#define SEG_H   (1 << 7)
#define SEG_NONE 0

// Non-digit segments a frame can switch on.
#define PET_FRAME_COLON     (1 << 0)
#define PET_FRAME_SIGNAL    (1 << 1)
#define PET_FRAME_BELL      (1 << 2)
#define PET_FRAME_PM        (1 << 3)
#define PET_FRAME_24H       (1 << 4)
#define PET_FRAME_LAP       (1 << 5)

// One frame of animation: what every position shows, and for how long.
typedef struct {
    uint8_t seg[10];    // segment mask for LCD positions 0-9 (SEG_A | SEG_B ...)
    uint8_t flags;      // PET_FRAME_* extras
    uint8_t hold;       // frames to hold this for, at PET_ANIM_HZ (8 = one second)
} pet_frame_t;

// ---- Layers -----------------------------------------------------------------
//
// The screen is composited from independent layers, each owning its own cells.
// Without that, every combination would need its own art: five moods times five
// food states times two poo states is fifty full-screen animations, against
// twelve as separate layers.
//
// Ownership is declared per segment, not just per position, because position 9
// is split — the character has its top edge and both verticals, the poo has the
// rest. That split is only safe because 9 has no tied segments; the same trick
// in position 4 or 6 would break, since A is tied to D in both.
//
//   CHARACTER   1, 4, 5, 6, 7, 8, colon, and 9's A D E F
//               the pet itself: moods, eating, kissing, snoring, dying.
//               Position 1 sits off the mouth for snores and kisses; it is the
//               one character cell with ties (B+C and E+F are whole edges).
//   STATUS      9's G B C
//               poo is the stem and base (G|B|C), barf just the puddle (B|C).
//
// Three things are composited but aren't animations, because they're a direct
// function of state rather than a sequence: the food pips (position 3, filled
// B, C, F, E as the queue grows), and the transient marks — the buff/debuff
// sign in position 0, the dinner BELL, and the SIGNAL blink that stands in for
// a sound when the watch is silent.
typedef enum {
    PET_LAYER_CHARACTER = 0,
    PET_LAYER_STATUS,
    PET_LAYER_COUNT
} pet_layer_id_t;

// What a layer is allowed to light. Anything a frame sets outside this is
// masked off, so a stray segment in the art can't invade another layer's cell.
typedef struct {
    uint8_t seg[10];
    uint8_t flags;
} pet_layer_def_t;

typedef struct {
    const char *label;          // drawn instead of frames while frames == NULL
    const pet_frame_t *frames;
    uint8_t count;
    bool loop;
    pet_layer_id_t layer;       // which layer this animation plays on
} pet_anim_t;

// One layer's playback position.
typedef struct {
    uint8_t anim;               // pet_anim_id_t currently playing
    uint8_t idle;               // what to fall back to when a one-shot ends
    uint8_t frame;
    uint8_t hold_left;
} pet_layer_t;

// Every animation in the spec's checklist.
typedef enum {
    PET_ANIM_NONE = 0,
    // moods (looped while resting)
    PET_ANIM_HAPPY,
    PET_ANIM_CONFUSED,
    PET_ANIM_UPSET,
    PET_ANIM_ANGRY,
    PET_ANIM_DEAD,
    // one-shots
    PET_ANIM_RESURRECT,
    PET_ANIM_POO,
    PET_ANIM_PLAY_SMALL,
    PET_ANIM_PLAY_BIG,
    PET_ANIM_BARF,
    PET_ANIM_EAT,
    PET_ANIM_KISS,
    PET_ANIM_SNORE,
    PET_ANIM_WAKE,
    PET_ANIM_COUNT
} pet_anim_id_t;

typedef enum {
    PET_SOUND_SNORE = 0,
    PET_SOUND_KISS,
    PET_SOUND_BARF,
    PET_SOUND_EAT,
    PET_SOUND_COUNT
} pet_sound_id_t;

typedef enum {
    PET_MOOD_HAPPY = 0,
    PET_MOOD_CONFUSED,
    PET_MOOD_UPSET,
    PET_MOOD_ANGRY,
    PET_MOOD_DEAD
} pet_mood_t;

typedef enum {
    PET_DAYPART_MORNING = 0,    // PET_HOUR_WAKE      .. PET_HOUR_AFTERNOON
    PET_DAYPART_AFTERNOON,      // PET_HOUR_AFTERNOON .. PET_HOUR_SLEEP
    PET_DAYPART_NIGHT           // PET_HOUR_SLEEP     .. PET_HOUR_WAKE
} pet_daypart_t;

// What the pet is doing right now, on screen.
typedef enum {
    PET_SCENE_IDLE = 0,     // awake, resting between interactions (the spec's "blink")
    PET_SCENE_ASLEEP,       // night, snoring
    PET_SCENE_NIGHT_AWAKE,  // night, disturbed: interactions cost tics and give nothing
    PET_SCENE_FEEDING,      // pips queued or being eaten
    PET_SCENE_PLAYING,      // inside the 5 s shake window
    PET_SCENE_DEAD
} pet_scene_t;

#define PET_QUEUE_LEN 4

typedef struct {
    // -- Pet state. Lives in RAM across face switches and low-energy mode, so
    //    in daily wear it persists indefinitely; only a battery pull, a
    //    reflash or a crash hatches a new pet. See _pet_load in pet_face.c for
    //    why that's deliberate.
    uint8_t  quarter_tics;
    bool     has_poo;
    uint32_t last_update_ts;    // decay applied up to here (UTC)
    uint32_t last_fed_ts;       // for the missed-day penalty
    uint32_t poo_due_ts;        // a poo is on its way; 0 = none pending
    uint32_t poo_since_ts;      // the current poo has been sitting since here
    // Waking seconds counted but not yet worth a whole quarter tic. Decay is
    // charged in awake time, which doesn't divide evenly into wall time, so
    // the leftovers are carried here instead of being rounded away.
    uint16_t awake_residual;
    uint16_t poo_residual;
    uint32_t last_play_buff_ts; // the play cooldown runs from here
    uint8_t  hugs_today;
    uint8_t  hug_day;           // local day-of-month hugs_today belongs to
    uint8_t  woke_day;          // local day-of-month the wake animation last played

    // -- Per-visit state. Reset every activate.
    pet_scene_t scene;
    pet_layer_t layer[PET_LAYER_COUNT];
    pet_anim_id_t queue[PET_QUEUE_LEN];     // sequences on the character layer
    uint8_t  queue_len;
    // Transient marks, in ticks remaining. Composited while non-zero.
    uint8_t  buff_ticks;        // plus sign, position 0
    uint8_t  debuff_ticks;      // minus sign, position 0
    uint8_t  bell_ticks;        // dinner bell on a feed
    uint8_t  signal_ticks;      // stands in for a sound on a silent watch
    uint8_t  food_queue;
    uint16_t feed_ticks;
    uint16_t play_ticks;
    uint8_t  nausea;
    bool     play_buffed;       // did this play session actually earn the buff?
    uint16_t night_awake_ticks;
    uint16_t snore_ticks;
    bool     tap_enabled;
    bool     debug_preview;     // PET_DEBUG_CONTROLS: an animation is held on screen
    uint8_t  preview_anim;      // ... and which one, so stepping walks the list
} pet_state_t;

void pet_face_setup(uint8_t watch_face_index, void ** context_ptr);
void pet_face_activate(void *context);
bool pet_face_loop(movement_event_t event, void *context);
void pet_face_resign(void *context);

#define pet_face ((const watch_face_t){ \
    pet_face_setup, \
    pet_face_activate, \
    pet_face_loop, \
    pet_face_resign, \
    NULL, \
})
