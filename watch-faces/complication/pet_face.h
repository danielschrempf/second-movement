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
 *   Light + Alarm Hug -- an arm on each side, on whichever lands second
 *   Alarm  short  Sweep the floor (a pile or a barf puddle; not a poo on its way)
 *   Alarm  long   Resurrect (only while dead)
 *   Shake         Play (accelerometer; simulator: Alarm long while alive)
 *                 A burst of PET_SHAKE_TAPS taps, not one knock
 *                 Each shake climbs a rung; the pet is deaf between them
 *   Mode          reserved by Movement — next face
 *   Light  1.5 s  Showcase: start the reel of every animation, or cancel it
 *                 (PET_SHOWCASE)
 *   Alarm  1.5 s  Showcase: push the mood up one tic
 *
 * Light's 0.5 s press does nothing on its own, and that is what keeps the 1.5 s
 * hold behind it clean: nothing is spent reaching the showcase. Alarm's 0.5 s
 * still resurrects on the way to its 1.5 s, which only matters while dead.
 *
 * The pet sleeps 21:00-06:00. Nothing decays while it does; disturbing it costs
 * tics and earns no buff. Nothing runs while you are on another face; time is
 * caught up lazily on the next activate.
 *
 * The pet answers to the watch's own BTN beep setting: N in the settings face
 * and it is silent -- see _pet_play_sound.
 *
 * Two things this face does not do, by choice rather than omission:
 *
 *   The pet lives in RAM. It survives face switches and low-energy sleep, so in
 *   daily wear it lives indefinitely, but a reflash or a battery pull hatches a
 *   new one. There is no save format and none is planned.
 *
 *   The art is drawn for the classic F-91W LCD. The segment geometry below is
 *   that panel's and the mapping is not chosen at runtime, so the face builds
 *   and runs with DISPLAY=custom but does not read correctly there.
 *
 * Sounds are not timed against their animation -- each is cued to a moment in
 * the art ("when cell 9's bottom edge lights"), so redrawing an animation
 * carries its sounds along with it instead of leaving them stranded. See the
 * cue tables in pet_face.c.
 */

// ---- Tunables ---------------------------------------------------------------

// Mood is stored in quarter tics so the 0.25 steps below stay integers.
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
#define PET_DEBUFF_BARF             1   // +0.25 tic on a barf, on top of the returned play buff

// Poo arrives this long after a pip is eaten, and then accrues at the same rate
// as passive decay until it is swept.
#define PET_POO_DELAY_SECONDS       (2 * PET_SECONDS_PER_TIC)
#define PET_POO_SECONDS_PER_QT      (PET_SECONDS_PER_QT)

// Feeding
#define PET_FOOD_MAX                4
#define PET_FEED_SETTLE_SECONDS     3   // pause after the last press before eating starts
#define PET_FEED_PIP_SECONDS        1   // between pips

// What counts as a shake. The accelerometer reports single taps in hardware,
// and one of those is a low bar -- a knock against a desk, a brisk arm swing --
// yet it used to be a whole rung of the play ladder. So the face does not play
// on a tap. It plays on a deliberate pattern of them.
//
// PET_SHAKE_TAPS taps must land within PET_SHAKE_WINDOW_SECONDS of the first,
// no two of them closer together than PET_SHAKE_GAP_TICKS. The window does not
// stretch as taps arrive: it is a burst, not a slow drum. Short of the count
// the window expires and nothing happened.
//
// The gap is what rejects one hard knock. The hardware's own quiet period is
// around 60 ms, so a single impulse ringing out can report several taps; every
// one inside the gap is that same knock still sounding.
#define PET_SHAKE_TAPS              3   // taps that make a shake ...
#define PET_SHAKE_WINDOW_SECONDS    2   // ... all within this of the first ...
#define PET_SHAKE_GAP_TICKS         2   // ... and no two inside this (0.25 s)

// Since three are wanted, each one can be easier to land than Movement's
// default: the face sets its own LIS2DW Z-axis tap threshold after enabling
// detection, in units of 1/32 of the 2 g full scale, so 62.5 mg a step.
// Movement's own value is 12, or 750 mg. Raise this if the pet plays with
// itself in a pocket, lower it if deliberate tapping goes unheard.
#define PET_SHAKE_THRESHOLD         8   // 500 mg

// Play: a ladder of three rungs, paced by the clock rather than by how many taps
// a shake happens to produce.
//
//   shake                  -> PLAY_SMALL, then deaf, then a window to shake again
//   shake in that window   -> PLAY_BIG, deaf and a window again
//   shake in that window   -> barf, then deaf once more before the session ends
//   window expires         -> the session ends where it stands
//
// Both periods are measured from the end of the animation, not from the shake.
#define PET_PLAY_DEAF_SECONDS       3   // motion ignored while the pet settles
#define PET_PLAY_WINDOW_SECONDS     5   // ... and then this long to shake again
#define PET_PLAY_STAGE_BARF         3   // the rung that makes the pet sick
// Shaking always plays with the pet, but the buff only lands once per cooldown.
#define PET_PLAY_COOLDOWN_SECONDS   (2 * 60 * 60)

// Day parts (local time, 24 h clock). The waking day is fifteen hours so that it
// divides evenly into the three sittings below, and the morning boundary is the
// first of those, so "morning" and "breakfast" mean the same window.
#define PET_HOUR_WAKE               6   // 06:00 morning starts
#define PET_HOUR_AFTERNOON          11  // 11:00 afternoon starts
#define PET_HOUR_SLEEP              21  // 21:00 night starts

// Only waking seconds count against the pet; nothing decays while it sleeps.
#define PET_AWAKE_SECONDS_PER_DAY   ((PET_HOUR_SLEEP - PET_HOUR_WAKE) * 60 * 60)

// Meals. The waking day divides into three even sittings -- breakfast at 06:00,
// lunch at 11:00, dinner at 16:00 -- and the pet keeps only PET_FEED_SEGMENT_CAP
// pips down per sitting. The next one comes straight back up and takes the
// sitting's nutrition with it; see _pet_feed_barf.
#define PET_FEED_SEGMENTS           3
#define PET_FEED_SEGMENT_HOURS      ((PET_HOUR_SLEEP - PET_HOUR_WAKE) / PET_FEED_SEGMENTS)
#define PET_FEED_SEGMENT_CAP        4   // pips kept down per sitting

// The sittings have to be equal, or the last one is short and the cap means
// something different at dinner than at breakfast.
#if (PET_HOUR_SLEEP - PET_HOUR_WAKE) % PET_FEED_SEGMENTS
#error "the waking day must divide evenly into PET_FEED_SEGMENTS sittings"
#endif

// Settling the stomach. Every barf shuts the kitchen -- overfed at the table or
// played with too hard, it makes no difference -- for this long or until the
// next sitting comes round, whichever is sooner. Until then a feed is refused:
// the pet grumbles the plate away, and it costs nothing, the barf's own debuff
// having been charged already.
//
// Settling does not hand the sitting back. Past PET_FEED_SEGMENT_CAP the pet is
// done eating until the next one either way, so a settled stomach means the
// food will be taken -- not that it will stay down.
#define PET_BARF_SETTLE_SECONDS     (30 * 60)

// How long a disturbed pet stays up before settling again.
#define PET_NIGHT_AWAKE_SECONDS     30

// The pet breathes on every turn of the sleep loop; only the first two of each
// three are voiced. Counted in breaths rather than seconds, so the snore cannot
// drift out of step with the animation.
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

// Showcase: a reel of the pet's animations and moods.
//
// Most of what the pet can do is gated behind real time -- angry takes most of a
// day of neglect, dead a day and a half, snoring waits until 21:00 -- so a
// wearer could own this face for a week without seeing half its art. These two
// holds show the whole set on demand.
//
//   LIGHT held 1.5 s   start the reel, or cancel one already running
//   ALARM held 1.5 s   push the mood up one tic, wrapping past dead back to zero
//
// The reel plays the animations back to back in a fixed order -- a life story,
// starting from the grave -- each one PET_SHOWCASE_PLAYS times before the next
// begins, and round again from the top. One-shots are looped along with the
// rest, so nothing flashes past once.
//
// A lap is 59.5 s, which is sized against Movement's shortest inactivity timeout
// of 60 s: left alone, the reel plays the whole set through once and the face
// then bows out to the clock a moment into the second lap. Nothing here enforces
// that -- the reel simply loops, and the ordinary timeout ends it -- so a watch
// set to a longer timeout gets more laps rather than a truncated one.
//
// The hold cancels it early. So does anything you actually do to the pet -- feed,
// sweep, shake -- and so does a mood step.
//
// Both holds fire their 0.5 s long-press on the way past, since Movement
// delivers that first: LIGHT spends a hug, ALARM resurrects a dead pet and
// otherwise does nothing.
//
// Set to 0 to drop the reel and its code, leaving the buttons to play the game
// and nothing else.
#define PET_SHOWCASE          1
#define PET_SHOWCASE_PLAYS    2         // passes of each animation before the next

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
// region table in _pet_layers, at the top of pet_face.c.
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
#define SEG_A   (1 << 0)
#define SEG_B   (1 << 1)
#define SEG_C   (1 << 2)
#define SEG_D   (1 << 3)
#define SEG_E   (1 << 4)
#define SEG_F   (1 << 5)
#define SEG_G   (1 << 6)
#define SEG_H   (1 << 7)
#define SEG_NONE 0

// Non-digit segments a frame can switch on. A frame may only set COLON -- see
// the layer masks in pet_face.c; BELL and SIGNAL are added by _pet_draw from
// state, and nothing lights the remaining indicators.
#define PET_FRAME_COLON     (1 << 0)
#define PET_FRAME_SIGNAL    (1 << 1)
#define PET_FRAME_BELL      (1 << 2)

// One frame of animation: what every position shows, and for how long.
typedef struct {
    uint8_t seg[10];    // segment mask for LCD positions 0-9 (SEG_A | SEG_B ...)
    uint8_t flags;      // PET_FRAME_* extras
    uint8_t hold;       // frames to hold this for, at PET_ANIM_HZ (8 = one second)
} pet_frame_t;

// ---- Layers -----------------------------------------------------------------
//
// The screen is composited from independent layers, each owning its own cells,
// so the pet's mood and what's beside it animate separately.
//
//   CHARACTER   1, 4, 5, 6, 7, 8, colon, and 9's A D E F
//               moods, eating, kissing, snoring, dying. Position 1 sits off the
//               mouth for snores and kisses.
//   STATUS      9's G B C — poo is the stem and base, barf just the puddle.
//
// Ownership is per segment rather than per position because position 9 is split
// between the two layers. Only safe because 9 has no tied segments.
//
// Composited but not animations, being a direct function of state: the food pips
// (position 3, filling B, C, F, E as the queue grows), the buff/debuff sign in
// position 0, the dinner BELL, and the SIGNAL blink.
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

// A sound cued to a moment in the art rather than to a time: the engine fires it
// on the frame where its condition first comes true, so a cue follows a redraw
// instead of silently drifting out of step with it.
typedef struct {
    uint8_t sound;              // pet_sound_id_t to play
    uint8_t position;           // the cell to watch
    uint8_t mask;               // these segments... (0 = when the animation begins)
    bool    on_clear;           // ...going dark, rather than lighting up
    bool    once;               // only the first time, per play or per loop
} pet_cue_t;

typedef struct {
    // NULL only for PET_ANIM_NONE, which is how a layer says it draws nothing;
    // Every other row has art; only NONE is allowed to draw nothing.
    const pet_frame_t *frames;
    uint8_t count;
    bool loop;
    pet_layer_id_t layer;       // which layer this animation plays on
    const pet_cue_t *cues;      // sounds, cued to frames of this animation
    uint8_t cue_count;
} pet_anim_t;

// Fill in both `frames` and `count` from one table, so the two cannot drift.
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

// Every animation the pet can play.
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
    // the status layer: what the POO and BARF scenes leave on the floor, which
    // stays put until swept. The pile has a stem, the puddle doesn't.
    PET_ANIM_PILE,
    PET_ANIM_PUDDLE,
    PET_ANIM_COUNT
} pet_anim_id_t;

// Some sounds are in two parts, the halves cued to different moments of the same
// animation -- the barf's "uh oh" over the wriggling mouth, the slide once the
// contents are on their way.
//
// The second group has no art to hang a cue on: a button press that only moves
// a counter, or a change of state whose animation loops and so would re-cue for
// ever. Those are played straight from the code that causes them.
typedef enum {
    // cued to a moment in an animation
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
    PET_SOUND_WAKE,
    PET_SOUND_RESURRECT_FADE,
    PET_SOUND_RESURRECT_RISE,
    // played directly by the interaction or transition that causes them
    PET_SOUND_FEED,
    PET_SOUND_SWEEP,
    PET_SOUND_GRUMBLE,
    PET_SOUND_DEATH,
    PET_SOUND_MOOD_UP,
    PET_SOUND_MOOD_DOWN,
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
    PET_SCENE_IDLE = 0,     // awake, resting between interactions: the mood loop
    PET_SCENE_ASLEEP,       // night, snoring
    PET_SCENE_NIGHT_AWAKE,  // night, disturbed: interactions cost tics and give nothing
    PET_SCENE_FEEDING,      // pips queued or being eaten
    PET_SCENE_PLAYING,      // mid play session: settling, or waiting to be shaken again
    PET_SCENE_DEAD
} pet_scene_t;

#define PET_QUEUE_LEN 4

typedef struct {
    // -- Pet state. Survives face switches and low-energy mode, never a reflash.
    uint8_t  quarter_tics;
    bool     has_poo;
    // A barf leaves a puddle to sweep too. Separate from has_poo: the two are
    // drawn differently, and only the poo goes on charging tics.
    bool     has_barf;
    uint32_t last_update_ts;    // decay applied up to here (UTC)
    uint32_t last_fed_ts;       // for the missed-day penalty
    uint32_t poo_due_ts;        // a poo is on its way; 0 = none pending
    uint32_t poo_since_ts;      // the current poo has been sitting since here
    // Waking seconds counted but not yet worth a whole quarter tic.
    uint16_t awake_residual;
    uint16_t poo_residual;
    uint32_t last_play_buff_ts; // the play cooldown runs from here
    // A barf shuts the kitchen until here -- or until the sitting it happened
    // in turns over, which is what barf_sitting is for. Both are _pet_sitting_now
    // values: a local day and one of its sittings, packed, so "still the same
    // sitting" is one comparison.
    uint32_t barf_until_ts;     // 0 = the pet has never been sick
    uint16_t barf_sitting;
    uint8_t  hugs_today;
    uint8_t  hug_day;           // local day-of-month hugs_today belongs to
    // The current sitting, and what has been eaten in it. seg_buff_qt is the eat
    // buff granted in this sitting and not yet thrown up, so an overfeed knows
    // exactly what to hand back -- and a second one in the same sitting knows
    // there is nothing left to.
    uint8_t  pips_this_seg;
    uint8_t  seg_buff_qt;
    uint16_t fed_sitting;       // the sitting the two above belong to
    uint8_t  woke_day;          // local day-of-month the wake animation last played

    // -- Per-visit state. Reset every activate.
    pet_scene_t scene;
    // The mood the pet last settled into. _pet_rest sounds a step whenever the
    // mood it is about to show differs, so a change earned in front of you is
    // heard; seeding this on the way in keeps a change that happened while the
    // face was closed silent.
    uint8_t  shown_mood;
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
    uint8_t  play_stage;        // rung of the play ladder: 1 small, 2 big; 0 is
                                // idle, or a barf serving out its deaf period
    bool     play_buffed;       // did this play session actually earn the buff?
    // Taps collected toward a shake: how many so far, ticks left in the window
    // they must all land in, and ticks to ignore the last one's ringing for.
    uint8_t  shake_taps;
    uint8_t  shake_ticks;
    uint8_t  shake_gap;
    bool     poo_pending;       // a poo has landed unwatched; play the scene when idle
    uint16_t night_awake_ticks;
    uint8_t  breath;            // which breath of the snore cycle we are on
    bool     tap_enabled;
    // The hug is both buttons at once, so both edges are tracked. A chord that
    // has been spent on a hug then swallows the rest of the gesture -- both
    // releases, and the holds behind them. See _pet_chord.
    bool     light_down;
    bool     alarm_down;
    bool     chord_hugged;
    // What is on the LCD right now, so a redraw only touches the cells that
    // changed. `stale` forces the next redraw to push everything.
    uint8_t  shadow[10];
    uint8_t  shadow_flags;
    bool     shadow_stale;
    bool     showcase_on;       // PET_SHOWCASE: the reel owns the screen
    uint8_t  showcase_step;     // ... which entry of the reel it is on
    uint8_t  showcase_plays;    // ... and how many passes of it have gone by
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
