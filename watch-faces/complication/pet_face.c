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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pet_face.h"
#include "watch_common_display.h"   // Classic/Custom_LCD_Display_Mapping: segment -> (com, seg)

/*
 * How this file is organised
 *
 *   1. Animations and sounds       the tables the art and audio go into
 *   2. Helpers                     time of day, mood, tic arithmetic
 *   3. Renderer                    a frame -> LCD pixels
 *   4. Animation engine            one playing animation + a short queue
 *   5. Simulation                  catching up on time that passed off-screen
 *   6. Interactions                feed, hug, sweep, resurrect, play, disturb
 *   7. Scene entry                 what happens when the face comes on screen
 *   8. Movement callbacks
 */

// ============================================================================
// 1. Animations and sounds
// ============================================================================

// TODO: convert the Procreate Dreams sketches into frames. One table per
// animation, then point the matching row of _pet_anims at it and set count.
// Until an animation has frames, its 6-character label is drawn in the main
// line instead so the whole state machine can be tested in the simulator.
//
// A frame is the segment mask for each of the ten LCD positions, extra flags,
// and how many ticks (at PET_ANIM_HZ) to hold it. The face reads SIDEWAYS —
// the rotated segment geometry and the per-cell quirks are laid out in the
// frame section of pet_face.h, and the full table is in
// _cs50ref/SEGMENT_MAP.md. In short: the pet stacks 4, 5, eyes, 6, 7 down the
// screen, and a segment pair sharing an address (6A+6D, 4A+4D, 1B+1C, 1E+1F,
// 2A+2D+2G) lights if either half is set.
//
// Layout of a table, for reference. Note PET_FRAME_COLON carrying the eyes:
// they are one segment, so open and shut is the only thing they do, and the
// blink has to come from a frame like this because the classic LCD can't blink
// them in hardware.
//
//   static const pet_frame_t _pet_frames_happy[] = {
//       //  pos 0     1         2         3         4         5         6            7         8         9          flags            hold
//       { { SEG_NONE, SEG_NONE, SEG_NONE, SEG_NONE, SEG_NONE, SEG_NONE, SEG_B|SEG_C, SEG_NONE, SEG_NONE, SEG_NONE }, PET_FRAME_COLON, 16 },  // eyes open, flat mouth low
//       { { SEG_NONE, SEG_NONE, SEG_NONE, SEG_NONE, SEG_NONE, SEG_NONE, SEG_B|SEG_C, SEG_NONE, SEG_NONE, SEG_NONE }, 0,                2 },  // eyes shut: the blink
//   };
//   ... and in _pet_anims:  [PET_ANIM_HAPPY] = { "HAPPY ", _pet_frames_happy, 2, true },

// What each layer is allowed to light. Anything a frame sets outside its own
// cells is masked off by the compositor, so one stray segment in the art can't
// invade a neighbour. Position 9 is the shared one: the character has its top
// edge and both verticals, the status layer the centre and bottom.
static const pet_layer_def_t _pet_layers[PET_LAYER_COUNT] = {
    [PET_LAYER_CHARACTER] = {
        //   0     1     2     3     4     5     6     7     8    9 (partial)
        { 0x00, 0xFF, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
          SEG_A | SEG_D | SEG_E | SEG_F },
        PET_FRAME_COLON,
    },
    [PET_LAYER_STATUS] = {
        { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
          SEG_G | SEG_B | SEG_C },
        0,
    },
};

// The food pips, lit in this order as the queue fills. Sideways these four
// segments of position 3 read as a 2x2 block: bottom right, bottom left, top
// right, top left.
static const uint8_t _pet_food_pips[PET_FOOD_MAX] = { SEG_B, SEG_C, SEG_F, SEG_E };

// Poo: sideways, G is the centre vertical and B|C the bottom edge — a stem
// standing on a base. It just sits there until swept, so one frame that loops.
static const pet_frame_t _pet_frames_poo[] = {
    //  0  1  2  3  4  5  6  7  8  9                                flags  hold
    { { 0, 0, 0, 0, 0, 0, 0, 0, 0, SEG_G | SEG_B | SEG_C },            0,  PET_ANIM_HZ },
};

// Barf: the base without the stem — a puddle rather than a pile.
static const pet_frame_t _pet_frames_barf[] = {
    { { 0, 0, 0, 0, 0, 0, 0, 0, 0, SEG_B | SEG_C },                    0,  PET_ANIM_HZ * 2 },
};

static const pet_anim_t _pet_anims[PET_ANIM_COUNT] = {
    //                          label     frames              count  loop   layer
    [PET_ANIM_NONE]       = { "      ",  NULL,                0,     false, PET_LAYER_CHARACTER },
    // moods: loop while the pet rests
    [PET_ANIM_HAPPY]      = { "HAPPY ",  NULL,                0,     true,  PET_LAYER_CHARACTER },
    [PET_ANIM_CONFUSED]   = { "CONFUS",  NULL,                0,     true,  PET_LAYER_CHARACTER },
    [PET_ANIM_UPSET]      = { "UPSET ",  NULL,                0,     true,  PET_LAYER_CHARACTER },
    [PET_ANIM_ANGRY]      = { "ANGRY ",  NULL,                0,     true,  PET_LAYER_CHARACTER },
    [PET_ANIM_DEAD]       = { "DEAD  ",  NULL,                0,     true,  PET_LAYER_CHARACTER },
    // one-shots
    [PET_ANIM_RESURRECT]  = { "GHOST ",  NULL,                0,     false, PET_LAYER_CHARACTER },
    [PET_ANIM_PLAY_SMALL] = { "PLAY 1",  NULL,                0,     false, PET_LAYER_CHARACTER },
    [PET_ANIM_PLAY_BIG]   = { "PLAY 2",  NULL,                0,     false, PET_LAYER_CHARACTER },
    [PET_ANIM_EAT]        = { "EAT   ",  NULL,                0,     false, PET_LAYER_CHARACTER },
    [PET_ANIM_KISS]       = { "KISS  ",  NULL,                0,     false, PET_LAYER_CHARACTER },
    [PET_ANIM_SNORE]      = { "SNORE ",  NULL,                0,     true,  PET_LAYER_CHARACTER },
    [PET_ANIM_WAKE]       = { "WAKE  ",  NULL,                0,     false, PET_LAYER_CHARACTER },
    // the status layer: what's on the floor, drawn beside the pet in cell 9
    [PET_ANIM_POO]        = { "POO   ",  _pet_frames_poo,     1,     true,  PET_LAYER_STATUS },
    [PET_ANIM_BARF]       = { "BARF  ",  _pet_frames_barf,    1,     false, PET_LAYER_STATUS },
};

// TODO: compose the four sounds. Format: note, duration, note, duration, ...,
// 0. Durations are in 1/64 s. A negative number rewinds that many notes; the
// value after it is the repeat count. Every note in watch_tcc.h.
// These single beeps only exist so you can hear which hook fired.
static int8_t _pet_sound_snore[] = { BUZZER_NOTE_C4, 16, 0 };
static int8_t _pet_sound_kiss[]  = { BUZZER_NOTE_C7,  6, 0 };
static int8_t _pet_sound_barf[]  = { BUZZER_NOTE_E4, 16, 0 };
static int8_t _pet_sound_eat[]   = { BUZZER_NOTE_C6,  4, 0 };

static int8_t *_pet_sounds[PET_SOUND_COUNT] = {
    [PET_SOUND_SNORE] = _pet_sound_snore,
    [PET_SOUND_KISS]  = _pet_sound_kiss,
    [PET_SOUND_BARF]  = _pet_sound_barf,
    [PET_SOUND_EAT]   = _pet_sound_eat,
};

// Every sound goes through here, so this is also where the SIGNAL indicator
// gets flashed: the watch is often kept silent, and a pet whose whole
// personality is sound would otherwise lose half of itself.
static void _pet_play_sound(pet_state_t *s, pet_sound_id_t id) {
    movement_play_sequence(_pet_sounds[id], BUZZER_PRIORITY_SIGNAL);
    s->signal_ticks = PET_FLASH_TICKS;
}

// ============================================================================
// 2. Helpers
// ============================================================================

static uint32_t _pet_now(void) {
    return movement_get_utc_timestamp();
}

static pet_daypart_t _pet_daypart(uint8_t hour) {
    if (hour >= PET_HOUR_SLEEP || hour < PET_HOUR_WAKE) return PET_DAYPART_NIGHT;
    if (hour < PET_HOUR_AFTERNOON) return PET_DAYPART_MORNING;
    return PET_DAYPART_AFTERNOON;
}

static pet_mood_t _pet_mood(const pet_state_t *s) {
    if (s->quarter_tics >= PET_QT_DEAD)     return PET_MOOD_DEAD;
    if (s->quarter_tics >= PET_QT_ANGRY)    return PET_MOOD_ANGRY;
    if (s->quarter_tics >= PET_QT_UPSET)    return PET_MOOD_UPSET;
    if (s->quarter_tics >= PET_QT_CONFUSED) return PET_MOOD_CONFUSED;
    return PET_MOOD_HAPPY;
}

static pet_anim_id_t _pet_mood_anim(pet_mood_t mood) {
    switch (mood) {
        case PET_MOOD_CONFUSED: return PET_ANIM_CONFUSED;
        case PET_MOOD_UPSET:    return PET_ANIM_UPSET;
        case PET_MOOD_ANGRY:    return PET_ANIM_ANGRY;
        case PET_MOOD_DEAD:     return PET_ANIM_DEAD;
        default:                return PET_ANIM_HAPPY;
    }
}

// Add (or subtract) quarter tics, pinned to 0 .. dead.
static void _pet_add_qt(pet_state_t *s, int16_t delta) {
    int16_t v = (int16_t) s->quarter_tics + delta;
    if (v < 0) v = 0;
    if (v > PET_QT_DEAD) v = PET_QT_DEAD;
    s->quarter_tics = (uint8_t) v;
}

// -- Waking time --------------------------------------------------------------
//
// Decay is charged in waking seconds, not wall seconds: the pet sleeps
// PET_HOUR_SLEEP..PET_HOUR_WAKE and nothing accrues against it while it does.
// Catching up on an arbitrary stretch of time spent off-screen therefore means
// counting the waking seconds at each end and subtracting.

static uint32_t _pet_local_ts(uint32_t utc_ts) {
    int32_t offset = movement_get_current_timezone_offset();
    if (offset < 0) {
        uint32_t behind = (uint32_t) -offset;
        return (utc_ts > behind) ? utc_ts - behind : 0;
    }
    return utc_ts + (uint32_t) offset;
}

// Waking seconds from the epoch up to local time local_ts. Monotonic, so the
// waking seconds in any span are just the difference of its two ends, however
// many nights fall in between.
static uint32_t _pet_awake_seconds(uint32_t local_ts) {
    uint32_t days = local_ts / 86400;
    uint32_t rem  = local_ts % 86400;
    uint32_t partial;

    if (rem < (uint32_t) PET_HOUR_WAKE * 3600) {
        partial = 0;                            // still in last night
    } else if (rem < (uint32_t) PET_HOUR_SLEEP * 3600) {
        partial = rem - (uint32_t) PET_HOUR_WAKE * 3600;
    } else {
        partial = PET_AWAKE_SECONDS_PER_DAY;    // already gone to bed
    }
    return days * (uint32_t) PET_AWAKE_SECONDS_PER_DAY + partial;
}

// Waking seconds between two UTC timestamps. The current UTC offset is applied
// to both ends, so a span straddling a DST change is off by one hour, once —
// at most two thirds of a quarter tic, and only twice a year.
static uint32_t _pet_awake_between(uint32_t from_ts, uint32_t to_ts) {
    if (to_ts <= from_ts) return 0;
    return _pet_awake_seconds(_pet_local_ts(to_ts))
         - _pet_awake_seconds(_pet_local_ts(from_ts));
}

// ============================================================================
// 3. Renderer
// ============================================================================

// Light exactly the segments in mask at one LCD position. Clears everything
// first so a tied pair (e.g. 6A/6D) ends up on if either half was asked for.
static void _pet_draw_position(uint8_t position, uint8_t mask) {
    const digit_mapping_t *map = (watch_get_lcd_type() == WATCH_LCD_TYPE_CUSTOM)
        ? &Custom_LCD_Display_Mapping[position]
        : &Classic_LCD_Display_Mapping[position];

    for (uint8_t i = 0; i < 8; i++) {
        if (map->segment[i].value == segment_does_not_exist) continue;
        watch_clear_pixel(map->segment[i].address.com, map->segment[i].address.seg);
    }
    for (uint8_t i = 0; i < 8; i++) {
        if (!(mask & (1 << i))) continue;
        if (map->segment[i].value == segment_does_not_exist) continue;
        watch_set_pixel(map->segment[i].address.com, map->segment[i].address.seg);
    }
}

static void _pet_draw_flags(uint8_t flags) {
    if (flags & PET_FRAME_COLON) watch_set_colon(); else watch_clear_colon();
    if (flags & PET_FRAME_SIGNAL) watch_set_indicator(WATCH_INDICATOR_SIGNAL); else watch_clear_indicator(WATCH_INDICATOR_SIGNAL);
    if (flags & PET_FRAME_BELL)   watch_set_indicator(WATCH_INDICATOR_BELL);   else watch_clear_indicator(WATCH_INDICATOR_BELL);
    if (flags & PET_FRAME_PM)     watch_set_indicator(WATCH_INDICATOR_PM);     else watch_clear_indicator(WATCH_INDICATOR_PM);
    if (flags & PET_FRAME_24H)    watch_set_indicator(WATCH_INDICATOR_24H);    else watch_clear_indicator(WATCH_INDICATOR_24H);
    if (flags & PET_FRAME_LAP)    watch_set_indicator(WATCH_INDICATOR_LAP);    else watch_clear_indicator(WATCH_INDICATOR_LAP);
}

// Composite every layer into one framebuffer and push it to the LCD.
//
// Each layer contributes only the segments it owns — a frame's stray bits are
// masked off rather than trusted — so the layers can be drawn, timed and
// changed completely independently of each other. The food pips and the
// transient marks aren't animations at all; they're a direct read of state.
static void _pet_draw(const pet_state_t *s) {
    uint8_t fb[10] = { 0 };
    uint8_t flags = 0;
    const char *label = NULL;

    for (uint8_t l = 0; l < PET_LAYER_COUNT; l++) {
        const pet_anim_t *a = &_pet_anims[s->layer[l].anim];
        if (s->layer[l].anim == PET_ANIM_NONE) continue;
        if (a->frames == NULL) {
            // No art for this one yet. The character's label stands in below;
            // any other layer simply draws nothing.
            if (l == PET_LAYER_CHARACTER) label = a->label;
            continue;
        }
        const pet_frame_t *f = &a->frames[s->layer[l].frame];
        const pet_layer_def_t *d = &_pet_layers[l];
        for (uint8_t p = 0; p < 10; p++) fb[p] |= f->seg[p] & d->seg[p];
        flags |= f->flags & d->flags;
    }

    // Food pips: however many are queued, lit in order.
    for (uint8_t i = 0; i < s->food_queue && i < PET_FOOD_MAX; i++) {
        fb[PET_FOOD_POSITION] |= _pet_food_pips[i];
    }

    // Transient marks. Sideways, H is the horizontal stroke and G the vertical,
    // so G|H is a plus and H alone is a minus — the opposite way round from the
    // firmware's own upright '+' and '-'.
    if (s->buff_ticks)   fb[PET_BUFF_POSITION] |= SEG_G | SEG_H;
    if (s->debuff_ticks) fb[PET_BUFF_POSITION] |= SEG_H;
    if (s->bell_ticks)   flags |= PET_FRAME_BELL;
    if (s->signal_ticks) flags |= PET_FRAME_SIGNAL;

    for (uint8_t p = 0; p < 10; p++) _pet_draw_position(p, fb[p]);
    _pet_draw_flags(flags);

    // Scaffold until the character art lands: its name in positions 4-8. Cell 9
    // is left alone so the poo beside it still shows.
    if (label) {
        for (uint8_t i = 0; i < 5 && label[i]; i++) watch_display_character(label[i], 4 + i);
    }
}

// ============================================================================
// 4. Animation engine
// ============================================================================
//
// Each layer plays its own animation on its own clock, so the pet's mood and
// what's on the floor beside it advance independently. The character layer
// additionally has a short queue, which lets a scene say "wake, then the mood"
// and have them run back to back; when it empties, the pet rests (the spec's
// "blink"). Every other layer settles to its idle animation instead.

static void _pet_rest(pet_state_t *s);

static uint8_t _pet_frame_hold(const pet_anim_t *a, uint8_t frame) {
    // Label-only animations show for one second.
    return a->frames ? a->frames[frame].hold : PET_ANIM_HZ;
}

// True while a one-shot is still on screen. The looping animations (the moods,
// snore) are the resting state, so they never count as busy. Scene timers use
// this to wait their turn instead of cutting an animation off part-way —
// otherwise an eat animation longer than PET_FEED_PIP_SECONDS would be
// truncated by the next pip.
static bool _pet_anim_busy(const pet_state_t *s) {
    uint8_t id = s->layer[PET_LAYER_CHARACTER].anim;
    return id != PET_ANIM_NONE && !_pet_anims[id].loop;
}

// Point a layer at an animation, from frame zero.
static void _pet_layer_play(pet_state_t *s, pet_layer_id_t l, pet_anim_id_t id) {
    pet_layer_t *L = &s->layer[l];
    L->anim = (uint8_t) id;
    L->frame = 0;
    L->hold_left = _pet_frame_hold(&_pet_anims[id], 0);
}

// Play an animation on whichever layer owns it. Which layer that is comes from
// the animation table, so callers never have to think about it.
static void _pet_start_anim(pet_state_t *s, pet_anim_id_t id) {
    _pet_layer_play(s, _pet_anims[id].layer, id);
    _pet_draw(s);
}

static void _pet_queue_anim(pet_state_t *s, pet_anim_id_t id) {
    if (s->queue_len < PET_QUEUE_LEN) s->queue[s->queue_len++] = id;
}

static bool _pet_start_next_queued(pet_state_t *s) {
    if (s->queue_len == 0) return false;
    pet_anim_id_t id = s->queue[0];
    s->queue_len--;
    memmove(&s->queue[0], &s->queue[1], s->queue_len * sizeof(s->queue[0]));
    _pet_start_anim(s, id);
    return true;
}

// The status layer is a direct read of what's on the floor. A barf already
// playing isn't interrupted — it settles to whatever this wanted afterwards.
static void _pet_set_status(pet_state_t *s) {
    pet_anim_id_t want = (s->has_poo && _pet_mood(s) != PET_MOOD_DEAD)
                       ? PET_ANIM_POO : PET_ANIM_NONE;
    pet_layer_t *L = &s->layer[PET_LAYER_STATUS];
    L->idle = (uint8_t) want;
    if (L->anim != PET_ANIM_BARF) _pet_layer_play(s, PET_LAYER_STATUS, want);
}

static void _pet_layer_tick(pet_state_t *s, pet_layer_id_t l) {
    pet_layer_t *L = &s->layer[l];
    if (L->anim == PET_ANIM_NONE) return;

    const pet_anim_t *a = &_pet_anims[L->anim];
    uint8_t count = a->frames ? a->count : 1;
    bool loop_here = a->loop;
    bool queued = (l == PET_LAYER_CHARACTER) && (s->queue_len > 0);

#if PET_DEBUG_CONTROLS
    // Hold whatever is being previewed on screen, one-shots included, so it can
    // be looked at for as long as it takes rather than flashing past once.
    if (s->debug_preview) { loop_here = true; queued = false; }
#endif

    if (L->hold_left > 1) {
        L->hold_left--;
        return;
    }

    L->frame++;
    if (L->frame < count) {
        L->hold_left = _pet_frame_hold(a, L->frame);
        _pet_draw(s);
        return;
    }

    // The animation just ended.
    if (loop_here && !queued) {
        L->frame = 0;
        L->hold_left = _pet_frame_hold(a, 0);
        _pet_draw(s);
        return;
    }
    if (queued) {
        _pet_start_next_queued(s);
        return;
    }
    if (l == PET_LAYER_CHARACTER) {
        _pet_rest(s);
        return;
    }
    _pet_layer_play(s, l, (pet_anim_id_t) L->idle);
    _pet_draw(s);
}

static void _pet_anim_tick(pet_state_t *s) {
    for (uint8_t l = 0; l < PET_LAYER_COUNT; l++) _pet_layer_tick(s, (pet_layer_id_t) l);
}

// Transient marks age out on their own; redraw only when one actually expires.
static void _pet_flash_tick(pet_state_t *s) {
    bool expired = false;
    if (s->buff_ticks   && --s->buff_ticks   == 0) expired = true;
    if (s->debuff_ticks && --s->debuff_ticks == 0) expired = true;
    if (s->bell_ticks   && --s->bell_ticks   == 0) expired = true;
    if (s->signal_ticks && --s->signal_ticks == 0) expired = true;
    if (expired) _pet_draw(s);
}

// A plus or a minus in position 0, whenever the pet gains or loses ground.
static void _pet_flash_buff(pet_state_t *s, bool gained) {
    s->buff_ticks   = gained ? PET_FLASH_TICKS : 0;
    s->debuff_ticks = gained ? 0 : PET_FLASH_TICKS;
}

// The spec's "blink": settle into the looping mood animation, poo on top if
// there is one. Scenes with their own timer (feeding, playing, awake at night)
// keep their scene; anything else becomes idle.
static void _pet_rest(pet_state_t *s) {
    pet_mood_t mood = _pet_mood(s);
    s->queue_len = 0;
    _pet_set_status(s);

    if (mood == PET_MOOD_DEAD) {
        s->scene = PET_SCENE_DEAD;
        _pet_start_anim(s, PET_ANIM_DEAD);
        return;
    }

    switch (s->scene) {
        case PET_SCENE_ASLEEP:
            _pet_start_anim(s, PET_ANIM_SNORE);
            return;
        case PET_SCENE_FEEDING:
        case PET_SCENE_PLAYING:
        case PET_SCENE_NIGHT_AWAKE:
            break;
        default:
            s->scene = PET_SCENE_IDLE;
            break;
    }
    _pet_start_anim(s, _pet_mood_anim(mood));
}

#if PET_DEBUG_CONTROLS
// -- Development controls -----------------------------------------------------
//
// The pet's whole point is fourteen animations, but most of them only appear
// when the clock says so: the Angry face wants most of a day of neglect, the
// dead one a day and a half, snoring wants it to be 21:00. These two walk
// through the lot on demand so art can be checked as it lands. See the button
// map next to PET_DEBUG_CONTROLS in pet_face.h.

// Step to the next animation and hold it. Walking off the end of the list hands
// the screen back to the live pet, so repeated presses cycle through everything
// and return, rather than sticking in preview with no way out.
static void _pet_debug_next_anim(pet_state_t *s) {
    pet_anim_id_t next = s->debug_preview ? (pet_anim_id_t) (s->preview_anim + 1)
                                          : PET_ANIM_HAPPY;
    if (next >= PET_ANIM_COUNT) {
        s->debug_preview = false;
        _pet_rest(s);
        return;
    }
    s->debug_preview = true;
    s->preview_anim = (uint8_t) next;
    s->queue_len = 0;
    // Clear both layers first: stepping onto a status animation shouldn't leave
    // the character standing there, or vice versa.
    _pet_layer_play(s, PET_LAYER_CHARACTER, PET_ANIM_NONE);
    _pet_layer_play(s, PET_LAYER_STATUS, PET_ANIM_NONE);
    _pet_start_anim(s, next);
}

// Push the mood up a tic at a time, wrapping past dead back to blissful, so
// every threshold can be seen in order without waiting a day for each one.
// _pet_rest sorts out the scene, including climbing back out of PET_SCENE_DEAD.
static void _pet_debug_step_mood(pet_state_t *s) {
    uint8_t next = s->quarter_tics + PET_TIC(1);
    s->debug_preview = false;
    s->quarter_tics = (next > PET_QT_DEAD) ? 0 : next;
    s->awake_residual = 0;
    _pet_rest(s);
}
#endif

// ============================================================================
// 5. Simulation
// ============================================================================

// Persistence: deliberately not implemented. Decided 2026-09-12 after working
// out what actually threatens the pet's RAM.
//
// The state lives in a malloc'd struct, so it survives switching faces and —
// the part that matters — Movement's low-energy mode, which calls
// watch_enter_sleep_mode(). That mode disables pins and peripherals but leaves
// RAM alone. The mode that would wipe it is BACKUP, and watch_enter_backup_mode()
// is never called anywhere in this firmware (there's also an erratum that makes
// it impractical on current silicon). So in ordinary daily wear the pet persists
// indefinitely.
//
// What does lose it: a battery pull or a flat battery, a reflash, or a crash.
// Flashing means taking the watch apart to reach the board, so none of those
// happen by accident on the wrist — and starting a fresh pet after a battery
// change is a fair reading of the fiction anyway.
//
// If that changes, the two routes are: RTC backup registers 2-6 (claim with
// movement_claim_backup_register(); survives reset and reflash, not a battery
// pull; 160 bits total, so the four timestamps have to be stored as
// minute-resolution offsets from one absolute anchor), or a littlefs file via
// filesystem_write_file() in the RWWEE area, which survives everything
// including a battery swap at the cost of flash writes.
static void _pet_load(pet_state_t *s) {
    (void) s;
}

static void _pet_save(const pet_state_t *s) {
    (void) s;
}

// Apply everything that should have happened since the last update: passive
// decay, the missed-feed penalty, the poo arriving, the poo sitting there.
// Called on every activate ("If Mode hit: return to rest, accumulate 1 tic
// every 6 hrs") so nothing has to run while the face is in the background.
static void _pet_catch_up(pet_state_t *s) {
    uint32_t now = _pet_now();
    watch_date_time_t local = movement_get_local_date_time();

    // The clock moved backwards (time was set): re-anchor rather than punish.
    if (now < s->last_update_ts) s->last_update_ts = now;
    if (now < s->last_fed_ts)    s->last_fed_ts = now;
    if (now < s->poo_since_ts)   s->poo_since_ts = now;
    // Same idea for the play cooldown, except the forgiving end is zero: clear
    // it so the pet can be played with now, rather than parking the cooldown
    // two hours into a clock that just jumped.
    if (now < s->last_play_buff_ts) s->last_play_buff_ts = 0;

    if (s->quarter_tics < PET_QT_DEAD) {
        // Passive: +1 tic per 6 h of waking time. Whole quarter tics only; the
        // leftover seconds carry in awake_residual, because waking time does
        // not divide evenly into wall time and rounding it away every visit
        // would let a diligent owner outrun decay by opening the face often.
        uint32_t awake = _pet_awake_between(s->last_update_ts, now) + s->awake_residual;
        uint32_t qt = awake / PET_SECONDS_PER_QT;
        s->awake_residual = (uint16_t) (awake % PET_SECONDS_PER_QT);
        s->last_update_ts = now;
        if (qt > 0) {
            if (qt > PET_QT_DEAD) qt = PET_QT_DEAD;
            _pet_add_qt(s, (int16_t) qt);
        }

        // "1 tic every missed day" without eating. Decided 2026-09-12: this
        // stacks on top of passive decay, so a fully neglected day costs
        // 4 + 1 = 5 tics. Counted in wall-clock days rather than waking hours —
        // a missed day is a missed day.
        uint32_t days = (now - s->last_fed_ts) / PET_MISSED_FEED_SECONDS;
        if (days > 0) {
            s->last_fed_ts += days * PET_MISSED_FEED_SECONDS;
            if (days > 6) days = 6;
            _pet_add_qt(s, (int16_t) PET_TIC(days));
        }

        // A poo that was on its way has arrived. Max one at a time. This
        // countdown is wall clock: digestion doesn't stop overnight, only the
        // penalty for leaving the result lying there does.
        if (!s->has_poo && s->poo_due_ts != 0 && now >= s->poo_due_ts) {
            s->has_poo = true;
            s->poo_since_ts = s->poo_due_ts;
            s->poo_residual = 0;
            s->poo_due_ts = 0;
        }

        // An unswept poo keeps adding tics, in waking time like passive decay.
        if (s->has_poo) {
            uint32_t poo_awake = _pet_awake_between(s->poo_since_ts, now) + s->poo_residual;
            uint32_t pqt = poo_awake / PET_POO_SECONDS_PER_QT;
            s->poo_residual = (uint16_t) (poo_awake % PET_POO_SECONDS_PER_QT);
            s->poo_since_ts = now;
            if (pqt > 0) {
                if (pqt > PET_QT_DEAD) pqt = PET_QT_DEAD;
                _pet_add_qt(s, (int16_t) pqt);
            }
        }
    }

    // Decided 2026-09-12: the hug cap ("up to -1.0 tic, 0.25 at a time") resets
    // with the calendar day. Per visit would be trivially gamed by switching
    // face and coming straight back for four more hugs.
    if (s->hug_day != local.unit.day) {
        s->hug_day = local.unit.day;
        s->hugs_today = 0;
    }
}

// ============================================================================
// 6. Interactions
// ============================================================================

// Any feed / hug / play while the pet is asleep or was just woken up: +0.25
// tic, no buff. The first one plays the wake animation; the rest just keep it
// awake ("play animation based on current tic" — the mood is already on
// screen).
static void _pet_disturb(pet_state_t *s) {
    _pet_add_qt(s, PET_DEBUFF_DISTURB);
    _pet_flash_buff(s, false);
    s->night_awake_ticks = PET_NIGHT_AWAKE_SECONDS * PET_ANIM_HZ;
    if (s->scene == PET_SCENE_ASLEEP) {
        s->scene = PET_SCENE_NIGHT_AWAKE;
        _pet_start_anim(s, PET_ANIM_WAKE);   // then rests into the mood, scene kept
    } else {
        _pet_draw(s);
    }
}

static void _pet_fall_asleep(pet_state_t *s) {
    s->scene = PET_SCENE_ASLEEP;
    // Zero means "snore on the next tick"; _pet_tick handles the repeat from
    // there, so every route into sleep sounds the same.
    s->snore_ticks = 0;
    _pet_rest(s);
}

// True if the interaction was absorbed by sleep/death and should go no further.
static bool _pet_blocked(pet_state_t *s) {
    switch (s->scene) {
        case PET_SCENE_DEAD:
            return true;
        case PET_SCENE_ASLEEP:
        case PET_SCENE_NIGHT_AWAKE:
            _pet_disturb(s);
            return true;
        default:
            return false;
    }
}

// Feed(): each press queues a pip (max 4). Eating starts 3 s after the last
// press, one pip per second; a press during eating restarts the 3 s wait.
static void _pet_feed_press(pet_state_t *s) {
    if (_pet_blocked(s)) return;
    if (s->food_queue < PET_FOOD_MAX) s->food_queue++;
    s->feed_ticks = PET_FEED_SETTLE_SECONDS * PET_ANIM_HZ;
    s->scene = PET_SCENE_FEEDING;
    // The pips are composited straight from food_queue, so the wearer can count
    // what's waiting. The bell rings on each press — a dinner bell.
    s->bell_ticks = PET_BELL_TICKS;
    _pet_draw(s);
}

static void _pet_feed_tick(pet_state_t *s) {
    // Let the eat animation finish before the timer moves on to the next pip.
    if (_pet_anim_busy(s)) return;
    if (s->feed_ticks > 0) {
        s->feed_ticks--;
        return;
    }
    if (s->food_queue == 0) {
        s->scene = PET_SCENE_IDLE;
        _pet_rest(s);
        return;
    }

    // Eat one pip.
    uint32_t now = _pet_now();
    s->food_queue--;
    s->last_fed_ts = now;
    _pet_add_qt(s, -PET_BUFF_EAT);
    if (!s->has_poo && s->poo_due_ts == 0) {
        s->poo_due_ts = now + PET_POO_DELAY_SECONDS;
    }
    _pet_flash_buff(s, true);
    _pet_play_sound(s, PET_SOUND_EAT);
    _pet_start_anim(s, PET_ANIM_EAT);
    s->feed_ticks = PET_FEED_PIP_SECONDS * PET_ANIM_HZ;
}

static void _pet_hug(pet_state_t *s) {
    if (_pet_blocked(s)) return;
    if (s->hugs_today < PET_HUG_CAP) {
        s->hugs_today++;
        _pet_add_qt(s, -PET_BUFF_HUG);
        _pet_flash_buff(s, true);
    }
    // Decided 2026-09-12: past the cap the pet still gets kissed, it just
    // doesn't help. A button that silently does nothing reads as broken — and
    // the missing plus sign is what tells you the cap is spent.
    _pet_play_sound(s, PET_SOUND_KISS);
    _pet_start_anim(s, PET_ANIM_KISS);
}

// "Sweep clears all": the poo on screen and any that's on its way. Works at
// night without waking the pet.
static void _pet_sweep(pet_state_t *s) {
    if (s->scene == PET_SCENE_DEAD) return;
    bool had_something = s->has_poo || s->poo_due_ts != 0;
    s->has_poo = false;
    s->poo_due_ts = 0;
    s->poo_residual = 0;
    _pet_set_status(s);
    // There's no sweep animation in the checklist, so the acknowledgement is
    // the mood animation restarting from frame 0 — enough to show the press
    // landed. Not while the pet is asleep: that would cut off the snoring.
    if (had_something && s->scene != PET_SCENE_ASLEEP) {
        _pet_start_anim(s, _pet_mood_anim(_pet_mood(s)));
    } else {
        _pet_draw(s);
    }
}

static void _pet_resurrect(pet_state_t *s) {
    if (s->scene != PET_SCENE_DEAD) return;
    uint32_t now = _pet_now();
    s->quarter_tics = 0;
    s->last_update_ts = now;
    s->last_fed_ts = now;
    s->awake_residual = 0;
    // Decided 2026-09-12: the poo does not survive death. The spec resets the
    // tics to 0, so resurrection is a clean slate, not an inherited mess.
    s->has_poo = false;
    s->poo_due_ts = 0;
    s->poo_residual = 0;
    // A resurrected pet gets its hugs back too, otherwise coming back on the
    // same day-of-month you last hugged it would leave the cap already spent.
    // Likewise the play cooldown: a pet brought back to life should be playable
    // straight away, not two hours from now.
    s->hugs_today = 0;
    s->last_play_buff_ts = 0;
    s->scene = PET_SCENE_IDLE;
    _pet_start_anim(s, PET_ANIM_RESURRECT);   // then rests into the mood ("blink")
}

// Play(): motion opens a 5 s window. The first shake is the play; every shake
// after it inside the window is nausea.
static void _pet_on_motion(pet_state_t *s) {
    if (s->scene == PET_SCENE_PLAYING) {
        if (s->nausea < 255) s->nausea++;
        return;
    }
    if (_pet_blocked(s)) return;

    uint32_t now = _pet_now();
    s->scene = PET_SCENE_PLAYING;
    s->nausea = 0;
    s->play_ticks = PET_PLAY_WINDOW_SECONDS * PET_ANIM_HZ;

    // The pet always plays along — the animation is the point — but the buff
    // only lands once per PET_PLAY_COOLDOWN_SECONDS. Play was otherwise the
    // one uncapped source of relief, and a shake every five seconds healed the
    // pet from death's door in a minute flat.
    s->play_buffed = (now - s->last_play_buff_ts) >= PET_PLAY_COOLDOWN_SECONDS;
    if (s->play_buffed) {
        s->last_play_buff_ts = now;
        _pet_add_qt(s, -PET_BUFF_PLAY);
        _pet_flash_buff(s, true);
    }
    _pet_start_anim(s, PET_ANIM_PLAY_SMALL);
}

static void _pet_play_tick(pet_state_t *s) {
    if (s->play_ticks > 0) {
        s->play_ticks--;
        return;
    }
    s->scene = PET_SCENE_IDLE;
    if (s->nausea > PET_NAUSEA_LIMIT) {
        // Decided 2026-09-12: barfing hands back the play buff just earned and
        // costs PET_DEBUFF_BARF on top, so shaking the watch senseless ends up
        // worse than never having played at all. Only give back a buff that was
        // actually granted — a session inside the cooldown earned nothing to
        // lose. The cooldown itself stays spent: a pet that has just been made
        // sick is not in the mood to play again.
        _pet_add_qt(s, (s->play_buffed ? PET_BUFF_PLAY : 0) + PET_DEBUFF_BARF);
        _pet_flash_buff(s, false);
        _pet_play_sound(s, PET_SOUND_BARF);
        _pet_start_anim(s, PET_ANIM_BARF);
    } else if (s->nausea > 0) {
        // Decided 2026-09-12: nothing in the spec says what triggers PLAY_BIG,
        // so it's a session with some shaking in it that stayed under the
        // nausea limit — the reward for playing enthusiastically but not madly.
        _pet_start_anim(s, PET_ANIM_PLAY_BIG);
    } else {
        _pet_rest(s);
    }
}

// ============================================================================
// 7. Scene entry
// ============================================================================

// The face just came on screen: catch up on lost time, then follow the
// spec's day/night tree.
static void _pet_enter(pet_state_t *s) {
    _pet_catch_up(s);
    _pet_save(s);

    s->queue_len = 0;
    s->food_queue = 0;
    s->nausea = 0;
    s->play_buffed = false;
    s->buff_ticks = s->debuff_ticks = s->bell_ticks = s->signal_ticks = 0;
    _pet_layer_play(s, PET_LAYER_CHARACTER, PET_ANIM_NONE);
    _pet_layer_play(s, PET_LAYER_STATUS, PET_ANIM_NONE);
#if PET_DEBUG_CONTROLS
    s->debug_preview = false;
#endif
    // Whatever is on the floor shows from the first frame, rather than waiting
    // for the wake/mood queue to drain and _pet_rest to get around to it.
    _pet_set_status(s);

    pet_mood_t mood = _pet_mood(s);
    if (mood == PET_MOOD_DEAD) {
        // Hold the tombstone until resurrected.
        s->scene = PET_SCENE_DEAD;
        _pet_start_anim(s, PET_ANIM_DEAD);
        return;
    }

    watch_date_time_t local = movement_get_local_date_time();
    switch (_pet_daypart(local.unit.hour)) {
        case PET_DAYPART_MORNING:
            // wake on the first visit of the morning, then the mood.
            // The spec also sequences the poo here, but that predates the
            // layers: it now lives in its own cell and simply appears beside
            // the pet, so there's nothing to queue. Queueing it would in fact
            // stall the character layer, since the queue only feeds that one.
            s->scene = PET_SCENE_IDLE;
            if (s->woke_day != local.unit.day) {
                s->woke_day = local.unit.day;
                _pet_queue_anim(s, PET_ANIM_WAKE);
            }
            _pet_queue_anim(s, _pet_mood_anim(mood));
            break;
        case PET_DAYPART_AFTERNOON:
            // static poo, mood, then rest
            s->scene = PET_SCENE_IDLE;
            break;
        case PET_DAYPART_NIGHT:
            // static poo, snore
            s->scene = PET_SCENE_ASLEEP;
            s->snore_ticks = 0;     // snore on the next tick
            break;
    }
    if (!_pet_start_next_queued(s)) _pet_rest(s);
}

// While the face stays open across a day-part boundary: nod off at 21:00, wake
// at 05:00. The spec only describes what happens on entry; decided 2026-09-12
// that watching the pet go to bed is better than seeing it frozen awake.
static void _pet_check_daypart(pet_state_t *s) {
    watch_date_time_t local = movement_get_local_date_time();
    bool night = _pet_daypart(local.unit.hour) == PET_DAYPART_NIGHT;
    if (night && s->scene == PET_SCENE_IDLE) {
        _pet_fall_asleep(s);
    } else if (!night && (s->scene == PET_SCENE_ASLEEP || s->scene == PET_SCENE_NIGHT_AWAKE)) {
        s->scene = PET_SCENE_IDLE;
        s->woke_day = local.unit.day;
        _pet_start_anim(s, PET_ANIM_WAKE);
    }
}

static void _pet_tick(pet_state_t *s, uint8_t subsecond) {
    switch (s->scene) {
        case PET_SCENE_FEEDING:
            _pet_feed_tick(s);
            break;
        case PET_SCENE_PLAYING:
            _pet_play_tick(s);
            break;
        case PET_SCENE_NIGHT_AWAKE:
            if (s->night_awake_ticks > 0 && --s->night_awake_ticks == 0) _pet_fall_asleep(s);
            break;
        case PET_SCENE_ASLEEP:
            // Snore on a timer rather than once on nodding off, so the pet is
            // audibly asleep the whole time you're looking at it.
            if (s->snore_ticks == 0) {
                _pet_play_sound(s, PET_SOUND_SNORE);
                s->snore_ticks = PET_SNORE_PERIOD_SECONDS * PET_ANIM_HZ;
            } else {
                s->snore_ticks--;
            }
            break;
        default:
            break;
    }
    if (subsecond == 0) _pet_check_daypart(s);
    _pet_flash_tick(s);
    _pet_anim_tick(s);
}

// ============================================================================
// 8. Movement callbacks
// ============================================================================

void pet_face_setup(uint8_t watch_face_index, void ** context_ptr) {
    (void) watch_face_index;
    if (*context_ptr == NULL) {
        *context_ptr = malloc(sizeof(pet_state_t));
        memset(*context_ptr, 0, sizeof(pet_state_t));
        pet_state_t *s = (pet_state_t *) *context_ptr;
        // A fresh pet is born now, fed now. Without this the first catch-up
        // would count every second since 1970.
        uint32_t now = _pet_now();
        s->last_update_ts = now;
        s->last_fed_ts = now;
        _pet_load(s);
    }
}

void pet_face_activate(void *context) {
    pet_state_t *s = (pet_state_t *) context;
    movement_request_tick_frequency(PET_ANIM_HZ);
    // Shake -> EVENT_SINGLE_TAP / EVENT_DOUBLE_TAP. Only while this face is on
    // screen: tap detection runs the accelerometer at 400 Hz.
    // (EVENT_ACCELEROMETER_WAKE is never delivered — its callback is commented
    // out in movement.c — so tap events are the motion source.)
    // TODO: measure battery impact; gate behind a shorter window if it hurts.
    s->tap_enabled = movement_enable_tap_detection_if_available(true);
}

bool pet_face_loop(movement_event_t event, void *context) {
    pet_state_t *s = (pet_state_t *) context;

#if PET_DEBUG_CONTROLS
    // Any real interaction drops out of animation preview and hands the screen
    // back to the pet, so you never have to remember how to escape it.
    switch (event.event_type) {
        case EVENT_LIGHT_BUTTON_UP:
        case EVENT_LIGHT_LONG_PRESS:
        case EVENT_ALARM_BUTTON_UP:
        case EVENT_ALARM_LONG_PRESS:
        case EVENT_SINGLE_TAP:
        case EVENT_DOUBLE_TAP:
            s->debug_preview = false;
            break;
        default:
            break;
    }
#endif

    switch (event.event_type) {
        case EVENT_ACTIVATE:
            _pet_enter(s);
            break;
        case EVENT_TICK:
            _pet_tick(s, event.subsecond);
            break;

        // Light: short = feed, long = hug. The empty cases keep Movement from
        // lighting the LED on press and from reacting to the long release.
        case EVENT_LIGHT_BUTTON_DOWN:
        case EVENT_LIGHT_LONG_UP:
            break;
        case EVENT_LIGHT_REALLY_LONG_PRESS:
#if PET_DEBUG_CONTROLS
            _pet_debug_next_anim(s);
#endif
            break;
        case EVENT_LIGHT_BUTTON_UP:
            _pet_feed_press(s);
            break;
        case EVENT_LIGHT_LONG_PRESS:
            _pet_hug(s);
            break;

        // Alarm: short = sweep, long = resurrect.
        case EVENT_ALARM_BUTTON_UP:
            _pet_sweep(s);
            break;
        case EVENT_ALARM_LONG_PRESS:
            if (s->scene == PET_SCENE_DEAD) {
                _pet_resurrect(s);
            }
#ifdef __EMSCRIPTEN__
            else {
                // The simulator has no accelerometer: stand in for a shake.
                _pet_on_motion(s);
            }
#endif
            break;
        case EVENT_ALARM_LONG_UP:
            break;
        case EVENT_ALARM_REALLY_LONG_PRESS:
#if PET_DEBUG_CONTROLS
            _pet_debug_step_mood(s);
#endif
            break;

        case EVENT_SINGLE_TAP:
        case EVENT_DOUBLE_TAP:
        case EVENT_ACCELEROMETER_WAKE:
            _pet_on_motion(s);
            break;

        case EVENT_TIMEOUT:
            // Decided 2026-09-12: when Movement calls time (the inactivity
            // deadline is a user setting — 60, 120, 300 or 1800 s), go back to
            // the clock. Staying would keep the 8 Hz tick and the 400 Hz
            // accelerometer running. This is also what stops the pet being
            // shaken awake all night by an arm rolling over in bed: resigning
            // turns tap detection off, so no wrist movement can reach it.
            movement_move_to_face(0);
            break;
        case EVENT_LOW_ENERGY_UPDATE:
            _pet_draw(s);
            break;

        default:
            // Mode button and everything else: Movement's defaults.
            return movement_default_loop_handler(event);
    }

    return true;
}

void pet_face_resign(void *context) {
    pet_state_t *s = (pet_state_t *) context;
    movement_request_tick_frequency(1);
    if (s->tap_enabled) {
        movement_disable_tap_detection_if_available();
        s->tap_enabled = false;
    }
    _pet_save(s);
}
