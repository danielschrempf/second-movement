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
 *   Alarm  short  Sweep the floor (a pile or a barf puddle; not a poo on its way)
 *   Alarm  long   Resurrect (only while dead)
 *   Shake         Play (accelerometer; simulator: Alarm long while alive)
 *                 Each shake climbs a rung; the pet is deaf between them
 *   Mode          reserved by Movement — next face
 *   Light  1.5 s  Showcase: hold the next animation on screen (PET_SHOWCASE)
 *   Alarm  1.5 s  Showcase: push the mood up one tic
 *
 * The showcase holds fire their 0.5 s action on the way past, so stepping an
 * animation also hugs the pet. Left in: it is a fair trade, and the short
 * actions never fire, since BUTTON_UP only arrives on a sub-0.5 s release.
 *
 * The pet sleeps 21:00–05:00. Nothing decays while it does; disturbing it
 * costs tics and earns no buff. While you're on another face nothing runs;
 * time is caught up lazily the next time the face is activated.
 *
 * Spec: _cs50ref/CS50x Final Project.md, with the reasoning behind every
 * departure from it recorded in _cs50ref/DEVLOG.md.
 *
 * The art and the sounds are all in. Several sounds are cued to specific frames
 * of their animation -- _cs50ref/tools/check_sounds.py asserts they still line
 * up, and wants running after any redraw.
 *
 * The pet answers to the watch's own BTN beep setting: N in the settings face
 * and it is silent. That is the only mute Movement has, so its sounds play at
 * BUZZER_PRIORITY_BUTTON rather than SIGNAL -- see _pet_play_sound.
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
// Barfing returns the play buff and costs this on top, so over-shaking ends up
// worse than never having played.
#define PET_DEBUFF_BARF             1

// Poo arrives this long after a pip is eaten. The spec gives two different
// figures; this is Feed()'s explicit "poo countdown (2 tic)". The other reading,
// 1.5 h, left a poo on screen almost whenever you had fed.
#define PET_POO_DELAY_SECONDS       (2 * PET_SECONDS_PER_TIC)
// An unswept poo accrues at the same rate as passive decay, so it doubles it.
#define PET_POO_SECONDS_PER_QT      (PET_SECONDS_PER_QT)
// The poo scene plays the first time the owner is on the face to see it,
// however long ago the drop came due. An earlier build only played it if the
// drop was under a minute old, which was honest about the clock and meant
// nobody ever saw it: a twelve-hour countdown expires while the face is in the
// background, and a visit almost never lands in the minute it happens.
//
// A barf leaves a puddle behind the same way, minus the countdown: the scene
// has already been watched, so there is nothing left to replay.

// Feeding
#define PET_FOOD_MAX                4
#define PET_FEED_SETTLE_SECONDS     3   // pause after the last press before eating starts
#define PET_FEED_PIP_SECONDS        1   // between pips

// Play: a ladder of three rungs, paced by the clock rather than by how many
// taps a shake happens to produce.
//
// Counting taps inside one window measured how hard the watch was shaken rather
// than how long it was played with. A flick of the wrist is a burst of
// interrupts, so a single shake either registered once — PLAY_SMALL, every time
// — or tripped clean past the nausea limit into a barf, with almost nothing in
// between. Now each shake moves the pet up one rung and then the pet is deaf
// for a while, so one shake can only ever count once:
//
//   shake                  -> PLAY_SMALL, then deaf, then a window to shake again
//   shake in that window   -> PLAY_BIG, deaf and a window again
//   shake in that window   -> barf
//   window expires         -> the session ends where it stands
//
// The deaf period starts when the animation ends rather than when the shake
// landed, so a flourish never eats into the window the owner is being offered.
#define PET_PLAY_DEAF_SECONDS       5   // motion ignored while the pet settles
#define PET_PLAY_WINDOW_SECONDS     5   // ... and then this long to shake again
#define PET_PLAY_STAGE_BARF         3   // the rung that makes the pet sick
// Shaking always plays with the pet, but the buff only lands once per cooldown.
// Hugs are capped and feeding is limited by poo; without this, play was the one
// uncapped source of relief and a shake every 5 s healed the pet in a minute.
#define PET_PLAY_COOLDOWN_SECONDS   (2 * 60 * 60)

// Day parts (local time, 24 h clock)
#define PET_HOUR_WAKE               5   // 05:00 morning starts
#define PET_HOUR_AFTERNOON          10  // 10:00 afternoon starts
#define PET_HOUR_SLEEP              21  // 21:00 night starts

// Only waking seconds count against the pet: nothing decays while it sleeps,
// and the disturb penalty already covers interrupting it. An ignored pet dies
// in ~40 h of wall clock rather than ~36 h.
#define PET_AWAKE_SECONDS_PER_DAY   ((PET_HOUR_SLEEP - PET_HOUR_WAKE) * 60 * 60)

// How long a disturbed pet stays up before settling again.
#define PET_NIGHT_AWAKE_SECONDS     30

// The pet breathes continuously while it sleeps, but is only audible on some of
// those breaths -- otherwise the snore never lets up. Two voiced out of every
// three is what gives it its rhythm. Counted in breaths rather than seconds so
// it stays tied to the animation: the sound cannot drift out of step with the
// thing it is describing.
#define PET_SNORE_BREATH_CYCLE      3
#define PET_SNORE_AUDIBLE_BREATHS   2

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

// Showcase: walk the animations and moods without waiting on the clock. Most of
// them are gated behind it — angry wants most of a day of neglect, dead a day
// and a half — so this is how anyone sees the art, and it is as much of the
// appeal as the game.
//
//   LIGHT held 1.5 s   hold the next animation on screen; walks the whole list
//                      and then hands the screen back to the live pet
//   ALARM held 1.5 s   push the mood up one tic, wrapping past dead to blissful
//
// Both fire their 0.5 s long-press on the way past, since Movement delivers that
// first: LIGHT spends a hug, ALARM does nothing unless the pet is dead, in which
// case it resurrects. Set to 0 for a build where the buttons only play the game.
#define PET_SHOWCASE          1

// ---- Frames -----------------------------------------------------------------

// THE FACE READS SIDEWAYS — the watch is turned 90 degrees clockwise, so the
// main line stacks downward as 4, 5, (colon = the eyes), 6, 7, then the smaller
// 8 and 9 below it. The old top row (0-3 and the indicators) becomes a column
// up the right-hand side.
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
// Per-cell constraints, verified against Classic_LCD_Display_Mapping. Ties are
// single electrical addresses: both halves light together or not at all. Full
// table in _cs50ref/SEGMENT_MAP.md.
//
//   0   right column, top   all 8 independent, H included — the only such cell
//   1   below it            6 controls; B+C and E+F are tied whole edges
//   3   right column        7 independent; carries the food pips
//   4   top of the head     A tied to D — both verticals together, or neither
//   5   above the eyes      7 independent, the most expressive cell up there
//   :   THE EYES            one segment: both dots always move together, and
//                           the classic LCD can't blink them in hardware, so
//                           every blink is a CPU-drawn frame
//   6   below the eyes      A tied to D — symmetric mouths only, no smirks
//   7   below that          7 independent
//   8   smaller, below      7 independent; D+E carry the hardware tick/tock
//   9   smaller, bottom     7 independent; shared with the status layer
//
// Position 7 is the only one that can blink autonomously, which is tempting for
// snoring, but watch_start_character_blink() takes a character rather than a
// segment mask and watch_stop_blink() clears the cell outright — using it means
// giving that cell to the hardware entirely.
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
// The screen is composited from independent layers, each owning its own cells,
// so the pet's mood and what's beside it animate separately. Bespoke art per
// combination was never viable: five moods times five food states times two poo
// states is fifty full-screen animations against twelve as layers.
//
//   CHARACTER   1, 4, 5, 6, 7, 8, colon, and 9's A D E F
//               moods, eating, kissing, snoring, dying. Position 1 sits off the
//               mouth for snores and kisses.
//   STATUS      9's G B C — poo is the stem and base, barf just the puddle.
//
// Ownership is per segment rather than per position because position 9 is split
// between the two. That is only safe because 9 has no tied segments; the same
// split in 4 or 6 would break, since A is tied to D in both.
//
// Three things are composited but aren't animations, being a direct function of
// state: the food pips (position 3, filling B, C, F, E as the queue grows) and
// the transient marks — the buff/debuff sign in position 0, the dinner BELL,
// and the SIGNAL blink that stands in for a sound on a silent watch.
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

// A sound cued to a moment in the art rather than to a time.
//
// The engine fires the cue on the frame where its condition first comes true,
// so what gets written down is "when the pile reaches the floor" rather than
// "1.125 seconds in". The difference matters because frame timings move every
// time an animation is redrawn and the meaning doesn't: a cue follows the
// drawing, where an offset silently stops matching it.
typedef struct {
    uint8_t sound;              // pet_sound_id_t to play
    uint8_t position;           // the cell to watch
    uint8_t mask;               // these segments... (0 = when the animation begins)
    bool    on_clear;           // ...going dark, rather than lighting up
    bool    once;               // only the first time, per play or per loop
} pet_cue_t;

typedef struct {
    const char *label;          // fallback if a row has no frames yet
    const pet_frame_t *frames;
    uint8_t count;
    bool loop;
    pet_layer_id_t layer;       // which layer this animation plays on
    const pet_cue_t *cues;      // sounds, cued to frames of this animation
    uint8_t cue_count;
} pet_anim_t;

// Fills in both `frames` and `count` from one table, so the two can never drift
// apart when an animation is redrawn and comes back a different length.
#define PET_FRAMES(t)   (t), (uint8_t) (sizeof(t) / sizeof((t)[0]))
#define PET_NO_FRAMES   NULL, 0
#define PET_CUES(t)     (t), (uint8_t) (sizeof(t) / sizeof((t)[0]))
#define PET_NO_CUES     NULL, 0

// One layer's playback position.
typedef struct {
    uint8_t anim;               // pet_anim_id_t currently playing
    uint8_t idle;               // what to fall back to when a one-shot ends
    uint8_t frame;
    uint8_t hold_left;
    uint8_t cues_fired;         // bit per cue, for the ones that fire once
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
    // the status layer: what the scenes above leave on the floor, which stays
    // put until it is swept. POO and BARF play on the character layer like any
    // other one-shot; these are what is still there afterwards. The pile has a
    // stem, the puddle doesn't — matching the last frame of each scene.
    PET_ANIM_PILE,
    PET_ANIM_PUDDLE,
    PET_ANIM_COUNT
} pet_anim_id_t;

// The spec asks for four; poo and the two play flourishes were added because
// those moments read as silent without them.
//
// Some are in two parts, because the two halves are cued to different moments
// of the same animation -- the barf's "uh oh" plays over the wriggling mouth,
// and the slide waits until the contents are actually on their way.
typedef enum {
    PET_SOUND_SNORE_IN = 0,
    PET_SOUND_SNORE_OUT,
    PET_SOUND_KISS,
    PET_SOUND_BARF_UHOH,
    PET_SOUND_BARF_SLIDE,
    PET_SOUND_EAT_GULP,
    PET_SOUND_EAT_CHEW,
    PET_SOUND_POO,
    PET_SOUND_PLAY_SMALL,
    PET_SOUND_PLAY_BIG,
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
    PET_SCENE_PLAYING,      // mid play session: settling, or waiting to be shaken again
    PET_SCENE_DEAD
} pet_scene_t;

#define PET_QUEUE_LEN 4

typedef struct {
    // -- Pet state. Survives face switches and low-energy mode, so in daily
    //    wear it persists indefinitely; only a battery pull, a reflash or a
    //    crash hatches a new pet. See _pet_load for why that's deliberate.
    uint8_t  quarter_tics;
    bool     has_poo;
    // A barf leaves a puddle to sweep too. It is kept apart from has_poo
    // because the two are drawn differently and only the poo goes on charging
    // tics — the barf has already taken its own out of the pet.
    bool     has_barf;
    uint32_t last_update_ts;    // decay applied up to here (UTC)
    uint32_t last_fed_ts;       // for the missed-day penalty
    uint32_t poo_due_ts;        // a poo is on its way; 0 = none pending
    uint32_t poo_since_ts;      // the current poo has been sitting since here
    // Waking seconds counted but not yet worth a whole quarter tic. Carried
    // rather than rounded away, or frequent visits would outrun decay.
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
    uint16_t play_ticks;        // deaf period plus window, counted down together
    uint8_t  play_stage;        // rung of the play ladder: 0 idle, 1 small, 2 big
    bool     play_buffed;       // did this play session actually earn the buff?
    bool     poo_pending;       // a poo has landed unwatched; play the scene when idle
    uint16_t night_awake_ticks;
    uint8_t  breath;            // which breath of the snore cycle we are on
    bool     tap_enabled;
    bool     showcase_on;       // PET_SHOWCASE: an animation is held on screen
    uint8_t  showcase_anim;     // ... and which one, so stepping walks the list
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
