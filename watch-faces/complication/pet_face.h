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
 * A virtual pet watch face. The pet has one mood meter, measured in "tics":
 * it climbs while the pet is neglected and drops when you look after it.
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
 * The pet sleeps 21:00–05:00. Disturbing it costs tics and earns no buff.
 * While you're on another face nothing runs; time is caught up lazily
 * the next time the face is activated.
 *
 * Spec: _cs50ref/CS50x Final Project.md
 *
 * Markers used in pet_face.c:
 *   TODO    work that has to happen before this can run for real
 *   DECIDE  the spec is ambiguous or silent; a default is in place
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

// Passive time: "accumulate 1 tic every 6 hrs" while resting.
#define PET_SECONDS_PER_TIC         (6 * 60 * 60)
#define PET_SECONDS_PER_QT          (PET_SECONDS_PER_TIC / PET_QT_PER_TIC)   // 1.5 h

// "1 tic every missed day" without feeding.
#define PET_MISSED_FEED_SECONDS     (24 * 60 * 60)

// Buffs and debuffs, in quarter tics.
#define PET_BUFF_PLAY               2   // -0.5 tic
#define PET_BUFF_HUG                1   // -0.25 tic per hug ...
#define PET_HUG_CAP                 4   // ... up to -1.0 tic (DECIDE: per day, see pet_face.c)
#define PET_BUFF_EAT                1   // -0.25 tic per pip eaten
#define PET_DEBUFF_DISTURB          1   // +0.25 tic each time sleep is disturbed

// Poo appears this long after a pip is eaten. DECIDE: the spec says both
// "0.25 tic after every feed" (1.5 h) and "poo countdown (2 tic)" (12 h).
#define PET_POO_DELAY_SECONDS       (PET_SECONDS_PER_QT)
// An unswept poo adds +1 tic per tic of time — same rate as passive decay,
// so a poo left sitting doubles the rate.
#define PET_POO_SECONDS_PER_QT      (PET_SECONDS_PER_QT)

// Feeding
#define PET_FOOD_MAX                4
#define PET_FEED_SETTLE_SECONDS     3   // pause after the last press before eating starts
#define PET_FEED_PIP_SECONDS        1   // between pips

// Play
#define PET_PLAY_WINDOW_SECONDS     5
#define PET_NAUSEA_LIMIT            3   // more motion than this inside the window -> barf

// Day parts (local time, 24 h clock)
#define PET_HOUR_WAKE               5   // 05:00 morning starts
#define PET_HOUR_AFTERNOON          10  // 10:00 afternoon starts
#define PET_HOUR_SLEEP              21  // 21:00 night starts

// DECIDE: the spec doesn't say when a disturbed pet goes back to sleep.
#define PET_NIGHT_AWAKE_SECONDS     30

// Tick rate while the face is on screen. Every timer above is counted in
// these ticks; the pet is drawn at most this often.
#define PET_ANIM_HZ                 8

// Development HUD: top-left shows the scene code, top-right the quarter tics.
// Set to 0 once the art needs positions 0-3.
#define PET_DEBUG_HUD               1

// ---- Frames -----------------------------------------------------------------

// Segment bits for a frame, per LCD position. Standard 7-segment lettering,
// H is the extra diagonal that only positions 0 and 1 have.
//
//      AAA
//     F   B
//      GGG
//     E   C
//      DDD
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

typedef struct {
    const char *label;          // drawn instead of frames while frames == NULL
    const pet_frame_t *frames;
    uint8_t count;
    bool loop;
} pet_anim_t;

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
    // -- Pet state. Lives in RAM across face switches and sleep mode; lost on
    //    reset. TODO: persist (see _pet_save / _pet_load in pet_face.c).
    uint8_t  quarter_tics;
    bool     has_poo;
    uint32_t last_update_ts;    // passive decay applied up to here (UTC)
    uint32_t last_fed_ts;       // for the missed-day penalty
    uint32_t poo_due_ts;        // a poo is on its way; 0 = none pending
    uint32_t poo_since_ts;      // the current poo has been sitting since here
    uint8_t  hugs_today;
    uint8_t  hug_day;           // local day-of-month hugs_today belongs to
    uint8_t  woke_day;          // local day-of-month the wake animation last played

    // -- Per-visit state. Reset every activate.
    pet_scene_t scene;
    pet_anim_id_t anim;
    uint8_t  frame;
    uint8_t  hold_left;
    pet_anim_id_t queue[PET_QUEUE_LEN];
    uint8_t  queue_len;
    const pet_frame_t *overlay; // drawn on top of every frame (the static poo)
    uint8_t  food_queue;
    uint16_t feed_ticks;
    uint16_t play_ticks;
    uint8_t  nausea;
    uint16_t night_awake_ticks;
    bool     tap_enabled;
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
