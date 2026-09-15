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
    ELSIE DEE
*/

// -----------
// GAME TUNING
// -----------

#define PET_QT_PER_TIC 4
#define PET_TIC(n) ((n) * PET_QT_PER_TIC)
#define PET_QT_DEAD PET_TIC(6)
#define PET_QT_CONFUSED PET_TIC(2)
#define PET_QT_UPSET PET_TIC(3)
#define PET_QT_ANGRY PET_TIC(4)
#define PET_SECONDS_PER_TIC (6 * 60 * 60)
#define PET_SECONDS_PER_QT (PET_SECONDS_PER_TIC / PET_QT_PER_TIC)
#define PET_MISSED_FEED_SECONDS (24 * 60 * 60)
#define PET_BUFF_PLAY 2
#define PET_BUFF_HUG 1
#define PET_HUG_CAP 4
#define PET_BUFF_EAT 1
#define PET_DEBUFF_DISTURB 1
#define PET_DEBUFF_BARF 1
#define PET_POO_DELAY_SECONDS (2 * PET_SECONDS_PER_TIC)
#define PET_POO_SECONDS_PER_QT (PET_SECONDS_PER_QT)

// -------
// FEEDING
// -------

#define PET_FOOD_MAX 4
#define PET_FEED_SETTLE_SECONDS 3
#define PET_FEED_PIP_SECONDS 1

// ----
// PLAY
// ----

#define PET_SHAKE_TAPS 3
#define PET_SHAKE_WINDOW_SECONDS 2
#define PET_SHAKE_GAP_TICKS 2
#define PET_SHAKE_THRESHOLD 8
#define PET_PLAY_WINDOW_SECONDS 5
#define PET_PLAY_STAGE_BARF 3
#define PET_PLAY_COOLDOWN_SECONDS (2 * 60 * 60)

// ---
// DAY
// ---

#define PET_HOUR_WAKE 6
#define PET_HOUR_AFTERNOON 11
#define PET_HOUR_SLEEP 21
#define PET_AWAKE_SECONDS_PER_DAY ((PET_HOUR_SLEEP - PET_HOUR_WAKE) * 60 * 60)

// -----
// MEALS
// -----

#define PET_FEED_SEGMENTS 3
#define PET_FEED_SEGMENT_HOURS ((PET_HOUR_SLEEP - PET_HOUR_WAKE) / PET_FEED_SEGMENTS)
#define PET_FEED_SEGMENT_CAP 4
#if (PET_HOUR_SLEEP - PET_HOUR_WAKE) % PET_FEED_SEGMENTS
#error "the waking day must divide evenly into PET_FEED_SEGMENTS sittings"
#endif
#define PET_BARF_SETTLE_SECONDS (30 * 60)

// -----
// SLEEP
// -----

#define PET_NIGHT_AWAKE_SECONDS 30
#define PET_SNORE_BREATH_CYCLE 3
#define PET_SNORE_AUDIBLE_BREATHS 2

// ----------
// ANIMATIONS
// ----------

#define PET_ANIM_HZ 8
#define PET_FLASH_TICKS (PET_ANIM_HZ)
#define PET_BELL_TICKS (PET_ANIM_HZ / 2)
#define PET_BUFF_POSITION 0
#define PET_FOOD_POSITION 3
#define SEG_A (1 << 0)
#define SEG_B (1 << 1)
#define SEG_C (1 << 2)
#define SEG_D (1 << 3)
#define SEG_E (1 << 4)
#define SEG_F (1 << 5)
#define SEG_G (1 << 6)
#define SEG_H (1 << 7)
#define SEG_NONE 0
#define PET_FRAME_COLON (1 << 0)
#define PET_FRAME_SIGNAL (1 << 1)
#define PET_FRAME_BELL (1 << 2)

// --------
// SHOWCASE
// --------

#define PET_SHOWCASE 1
#define PET_SHOWCASE_PLAYS 2

typedef struct
{
    uint8_t seg[10];
    uint8_t flags;
    uint8_t hold;
} pet_frame_t;

typedef enum
{
    PET_LAYER_CHARACTER = 0,
    PET_LAYER_STATUS,
    PET_LAYER_COUNT
} pet_layer_id_t;

typedef struct
{
    uint8_t seg[10];
    uint8_t flags;
} pet_layer_def_t;

typedef struct
{
    uint8_t sound;
    uint8_t position;
    uint8_t mask;
    bool on_clear;
    bool once;
} pet_cue_t;

typedef struct
{
    const pet_frame_t *frames;
    uint8_t count;
    bool loop;
    pet_layer_id_t layer;
    const pet_cue_t *cues;
    uint8_t cue_count;
} pet_anim_t;

#define PET_FRAMES(t) (t), (uint8_t)(sizeof(t) / sizeof((t)[0]))
#define PET_NO_FRAMES NULL, 0
#define PET_CUES(t) (t), (uint8_t)(sizeof(t) / sizeof((t)[0]))
#define PET_NO_CUES NULL, 0

typedef struct
{
    uint8_t anim;
    uint8_t idle;
    uint8_t frame;
    uint8_t hold_left;
    uint8_t cues_fired;
} pet_layer_t;

typedef enum
{
    PET_ANIM_NONE = 0,
    PET_ANIM_HAPPY,
    PET_ANIM_CONFUSED,
    PET_ANIM_UPSET,
    PET_ANIM_ANGRY,
    PET_ANIM_DEAD,
    PET_ANIM_RESURRECT,
    PET_ANIM_POO,
    PET_ANIM_PLAY_SMALL,
    PET_ANIM_PLAY_BIG,
    PET_ANIM_BARF,
    PET_ANIM_EAT,
    PET_ANIM_KISS,
    PET_ANIM_SNORE,
    PET_ANIM_WAKE,
    PET_ANIM_PILE,
    PET_ANIM_PUDDLE,
    PET_ANIM_COUNT
} pet_anim_id_t;

typedef enum
{
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
    PET_SOUND_FEED,
    PET_SOUND_SWEEP,
    PET_SOUND_GRUMBLE,
    PET_SOUND_DEATH,
    PET_SOUND_MOOD_UP,
    PET_SOUND_MOOD_DOWN,
    PET_SOUND_COUNT
} pet_sound_id_t;

typedef enum
{
    PET_MOOD_HAPPY = 0,
    PET_MOOD_CONFUSED,
    PET_MOOD_UPSET,
    PET_MOOD_ANGRY,
    PET_MOOD_DEAD
} pet_mood_t;

typedef enum
{
    PET_DAYPART_MORNING = 0,
    PET_DAYPART_AFTERNOON,
    PET_DAYPART_NIGHT
} pet_daypart_t;

typedef enum
{
    PET_SCENE_IDLE = 0,
    PET_SCENE_ASLEEP,
    PET_SCENE_NIGHT_AWAKE,
    PET_SCENE_FEEDING,
    PET_SCENE_PLAYING,
    PET_SCENE_DEAD
} pet_scene_t;

#define PET_QUEUE_LEN 4

typedef struct
{
    uint8_t quarter_tics;
    bool has_poo;
    bool has_barf;
    uint32_t last_update_ts;
    uint32_t last_fed_ts;
    uint32_t poo_due_ts;
    uint32_t poo_since_ts;
    uint16_t awake_residual;
    uint16_t poo_residual;
    uint32_t last_play_buff_ts;
    uint32_t barf_until_ts;
    uint16_t barf_sitting;
    uint8_t hugs_today;
    uint8_t hug_day;
    uint8_t pips_this_seg;
    uint8_t seg_buff_qt;
    uint16_t fed_sitting;
    uint8_t woke_day;
    pet_scene_t scene;
    uint8_t shown_mood;
    pet_layer_t layer[PET_LAYER_COUNT];
    pet_anim_id_t queue[PET_QUEUE_LEN];
    uint8_t queue_len;
    uint8_t buff_ticks;
    uint8_t debuff_ticks;
    uint8_t bell_ticks;
    uint8_t signal_ticks;
    uint8_t food_queue;
    uint16_t feed_ticks;
    uint16_t play_ticks;
    uint8_t play_stage;
    bool play_buffed;
    uint8_t shake_taps;
    uint8_t shake_ticks;
    uint8_t shake_gap;
    bool poo_pending;
    uint16_t night_awake_ticks;
    uint8_t breath;
    bool tap_enabled;
    bool light_down;
    bool alarm_down;
    bool chord_hugged;
    uint8_t shadow[10];
    uint8_t shadow_flags;
    bool shadow_stale;
    bool showcase_on;
    uint8_t showcase_step;
    uint8_t showcase_plays;
} pet_state_t;

void pet_face_setup(uint8_t watch_face_index, void **context_ptr);
void pet_face_activate(void *context);
bool pet_face_loop(movement_event_t event, void *context);
void pet_face_resign(void *context);

#define pet_face ((const watch_face_t){ \
    pet_face_setup,                     \
    pet_face_activate,                  \
    pet_face_loop,                      \
    pet_face_resign,                    \
    NULL,                               \
})
