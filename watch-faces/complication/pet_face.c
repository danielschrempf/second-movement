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

#include <stdlib.h>
#include <string.h>
#include "pet_face.h"
#include "watch_common_display.h"   // Classic_LCD_Display_Mapping: segment -> (com, seg)

// The art is drawn on the classic F-91W's segment geometry, so _pet_draw indexes
// that panel's mapping directly rather than choosing one at runtime. The face
// builds and runs on the custom LCD, but the pet is drawn from the wrong
// geometry there and will not read as intended; supporting that panel properly
// means redrawing every animation, not picking a different table.

/*
 * How this file is organised
 *
 *   1. Animations and sounds       the tables the art and audio go into
 *   2. Helpers                     time of day, mood, tic arithmetic
 *   3. Compositor                  layers -> LCD pixels
 *   4. Layer engine                per-layer playback, plus the rest state
 *   5. Simulation                  catching up on time that passed off-screen
 *   6. Interactions                feed, hug, sweep, resurrect, play, disturb
 *   7. Scene entry                 what happens when the face comes on screen
 *   8. Movement callbacks
 */

// ============================================================================
// 1. Animations and sounds
// ============================================================================

// What each layer is allowed to light; the compositor masks off anything a
// frame sets outside its own cells. Cells 0, 2 and 3 are barred to both layers
// -- they are drawn straight from state -- and cell 9 is in both masks.
static const pet_layer_def_t _pet_layers[PET_LAYER_COUNT] = {
    [PET_LAYER_CHARACTER] = {
        //   0     1     2     3     4     5     6     7     8     9
        { 0x00, 0xFF, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
        PET_FRAME_COLON,
    },
    [PET_LAYER_STATUS] = {
        { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
          SEG_G | SEG_B | SEG_C },
        0,
    },
};

// Position 3's pips, lit in this order as the food queue fills.
static const uint8_t _pet_food_pips[PET_FOOD_MAX] = { SEG_B, SEG_C, SEG_F, SEG_E };

// The animation tables. These are generated from segment art rather than
// written by hand. Frames are (segments per position, flags, hold),
// with hold in ticks at PET_ANIM_HZ; runs of identical poses are collapsed into
// one held frame.

// Resting mood: the mouth holds in cell 6, the colon carries the blink.
static const pet_frame_t _pet_frames_happy[] = {
    //  0         1         2         3         4         5         6         7         8         9            flags            hold
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_A|SEG_B|SEG_C|SEG_D, SEG_NONE , SEG_NONE , SEG_NONE  }, PET_FRAME_COLON,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_A|SEG_B|SEG_C|SEG_D, SEG_NONE , SEG_NONE , SEG_NONE  }, 0              ,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_A|SEG_B|SEG_C|SEG_D, SEG_NONE , SEG_NONE , SEG_NONE  }, PET_FRAME_COLON,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_A|SEG_B|SEG_C|SEG_D, SEG_NONE , SEG_NONE , SEG_NONE  }, 0              ,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_A|SEG_B|SEG_C|SEG_D, SEG_NONE , SEG_NONE , SEG_NONE  }, PET_FRAME_COLON,  6 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_A|SEG_B|SEG_C|SEG_D, SEG_NONE , SEG_NONE , SEG_NONE  }, 0              ,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_A|SEG_B|SEG_C|SEG_D, SEG_NONE , SEG_NONE , SEG_NONE  }, PET_FRAME_COLON,  5 },
};
// 16 frames -> 7 poses

static const pet_frame_t _pet_frames_confused[] = {
    //  0         1         2         3         4         5         6         7         8         9            flags            hold
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_B|SEG_C, SEG_B|SEG_C, SEG_NONE , SEG_NONE , SEG_NONE  }, PET_FRAME_COLON,  8 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_C|SEG_F, SEG_B|SEG_C, SEG_NONE , SEG_NONE , SEG_NONE  }, PET_FRAME_COLON,  8 },
};
// 16 frames -> 2 poses

// Brow in cell 5, frown in cell 6. Angry shares the frown and scowls the brow.
static const pet_frame_t _pet_frames_upset[] = {
    //  0         1         2         3         4         5         6         7         8         9            flags            hold
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_B|SEG_C, SEG_A|SEG_D|SEG_E|SEG_F, SEG_NONE , SEG_NONE , SEG_NONE  }, PET_FRAME_COLON,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_B|SEG_C, SEG_A|SEG_D|SEG_E|SEG_F, SEG_NONE , SEG_NONE , SEG_NONE  }, 0              ,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_B|SEG_C, SEG_A|SEG_D|SEG_E|SEG_F, SEG_NONE , SEG_NONE , SEG_NONE  }, PET_FRAME_COLON,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_B|SEG_C, SEG_A|SEG_D|SEG_E|SEG_F, SEG_NONE , SEG_NONE , SEG_NONE  }, 0              ,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_B|SEG_C, SEG_A|SEG_D|SEG_E|SEG_F, SEG_NONE , SEG_NONE , SEG_NONE  }, PET_FRAME_COLON,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_B|SEG_C, SEG_A|SEG_D|SEG_E|SEG_F, SEG_NONE , SEG_NONE , SEG_NONE  }, 0              ,  3 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_B|SEG_C, SEG_A|SEG_D|SEG_E|SEG_F, SEG_NONE , SEG_NONE , SEG_NONE  }, PET_FRAME_COLON,  8 },
};
// 16 frames -> 7 poses

static const pet_frame_t _pet_frames_angry[] = {
    //  0         1         2         3         4         5         6         7         8         9            flags            hold
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_A|SEG_B|SEG_C|SEG_D, SEG_A|SEG_D|SEG_E|SEG_F, SEG_NONE , SEG_NONE , SEG_NONE  }, PET_FRAME_COLON,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_A|SEG_B|SEG_C|SEG_D, SEG_A|SEG_D|SEG_E|SEG_F, SEG_NONE , SEG_NONE , SEG_NONE  }, 0              ,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_A|SEG_B|SEG_C|SEG_D, SEG_A|SEG_D|SEG_E|SEG_F, SEG_NONE , SEG_NONE , SEG_NONE  }, PET_FRAME_COLON,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_A|SEG_B|SEG_C|SEG_D, SEG_A|SEG_D|SEG_E|SEG_F, SEG_NONE , SEG_NONE , SEG_NONE  }, 0              ,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_A|SEG_B|SEG_C|SEG_D, SEG_A|SEG_D|SEG_E|SEG_F, SEG_NONE , SEG_NONE , SEG_NONE  }, PET_FRAME_COLON,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_A|SEG_B|SEG_C|SEG_D, SEG_A|SEG_D|SEG_E|SEG_F, SEG_NONE , SEG_NONE , SEG_NONE  }, 0              ,  3 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_A|SEG_B|SEG_C|SEG_D, SEG_A|SEG_D|SEG_E|SEG_F, SEG_NONE , SEG_NONE , SEG_NONE  }, PET_FRAME_COLON,  8 },
};
// 16 frames -> 7 poses

// A tombstone: one pose, nothing moves.
static const pet_frame_t _pet_frames_dead[] = {
    //  0         1         2         3         4         5         6         7         8         9            flags            hold
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_B|SEG_C|SEG_G, SEG_G    , SEG_A|SEG_D|SEG_E|SEG_F, SEG_A|SEG_D }, 0              ,  4 },
};
// 4 frames -> 1 poses

// The tombstone shrinks away, then the pet grows back in from cell 9.
static const pet_frame_t _pet_frames_resurrect[] = {
    //  0         1         2         3         4         5         6         7         8         9            flags            hold
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_B|SEG_C|SEG_G, SEG_G    , SEG_A|SEG_D|SEG_E|SEG_F, SEG_A|SEG_D }, 0              ,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_B|SEG_C|SEG_G, SEG_G    , SEG_A|SEG_D|SEG_E|SEG_F }, 0              ,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_B|SEG_C|SEG_G, SEG_G     }, 0              ,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_B|SEG_C|SEG_G }, 0              ,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_G     }, 0              ,  3 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_E|SEG_G }, 0              ,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_D    , SEG_E|SEG_G }, 0              ,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_D|SEG_E, SEG_E     }, 0              ,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_D|SEG_E|SEG_F, SEG_NONE  }, 0              ,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_B    , SEG_E|SEG_F, SEG_NONE  }, 0              ,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_A|SEG_B, SEG_F    , SEG_NONE  }, 0              ,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_A|SEG_B|SEG_F, SEG_NONE , SEG_NONE  }, 0              ,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_A|SEG_E|SEG_F, SEG_NONE , SEG_NONE  }, 0              ,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_A|SEG_D, SEG_E|SEG_F, SEG_NONE , SEG_NONE  }, 0              ,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_B|SEG_E, SEG_E    , SEG_NONE , SEG_NONE  }, 0              ,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_C|SEG_F, SEG_NONE , SEG_NONE , SEG_NONE  }, 0              ,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_A|SEG_D, SEG_NONE , SEG_NONE , SEG_NONE  }, 0              ,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_B|SEG_C, SEG_NONE , SEG_NONE , SEG_NONE  }, 0              ,  6 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_A|SEG_B|SEG_C|SEG_D, SEG_NONE , SEG_NONE , SEG_NONE  }, PET_FRAME_COLON,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_A|SEG_B|SEG_C|SEG_D, SEG_NONE , SEG_NONE , SEG_NONE  }, 0              ,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_A|SEG_B|SEG_C|SEG_D, SEG_NONE , SEG_NONE , SEG_NONE  }, PET_FRAME_COLON,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_A|SEG_B|SEG_C|SEG_D, SEG_NONE , SEG_NONE , SEG_NONE  }, 0              ,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_A|SEG_B|SEG_C|SEG_D, SEG_NONE , SEG_NONE , SEG_NONE  }, PET_FRAME_COLON,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_A|SEG_B|SEG_C|SEG_D, SEG_NONE , SEG_NONE , SEG_NONE  }, 0              ,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_A|SEG_B|SEG_C|SEG_D, SEG_NONE , SEG_NONE , SEG_NONE  }, PET_FRAME_COLON,  1 },
};
// 32 frames -> 25 poses

static const pet_frame_t _pet_frames_play_small[] = {
    //  0         1         2         3         4         5         6         7         8         9            flags            hold
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_A|SEG_B|SEG_C|SEG_D, SEG_NONE , SEG_NONE , SEG_NONE  }, PET_FRAME_COLON,  3 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_A|SEG_D, SEG_NONE , SEG_NONE , SEG_NONE  }, PET_FRAME_COLON,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_A    , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE  }, PET_FRAME_COLON,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_F    , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE  }, PET_FRAME_COLON,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_E    , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE  }, PET_FRAME_COLON,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_D    , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE  }, 0              ,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_A|SEG_D, SEG_NONE , SEG_NONE , SEG_NONE  }, PET_FRAME_COLON,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_A|SEG_B|SEG_C|SEG_D|SEG_G, SEG_NONE , SEG_NONE , SEG_NONE  }, 0              ,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_A|SEG_B|SEG_C|SEG_D|SEG_G, SEG_NONE , SEG_NONE , SEG_NONE  }, PET_FRAME_COLON,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_A|SEG_B|SEG_C|SEG_D|SEG_G, SEG_NONE , SEG_NONE , SEG_NONE  }, 0              ,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_A|SEG_B|SEG_C|SEG_D|SEG_G, SEG_NONE , SEG_NONE , SEG_NONE  }, PET_FRAME_COLON,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_A|SEG_B|SEG_C|SEG_D|SEG_G, SEG_NONE , SEG_NONE , SEG_NONE  }, 0              ,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_A|SEG_B|SEG_C|SEG_D|SEG_G, SEG_NONE , SEG_NONE , SEG_NONE  }, PET_FRAME_COLON,  2 },
};
// 16 frames -> 13 poses

static const pet_frame_t _pet_frames_play_big[] = {
    //  0         1         2         3         4         5         6         7         8         9            flags            hold
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_A|SEG_B|SEG_C|SEG_D, SEG_NONE , SEG_NONE , SEG_NONE  }, PET_FRAME_COLON,  2 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_A|SEG_D, SEG_NONE , SEG_NONE , SEG_NONE  }, PET_FRAME_COLON,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_A|SEG_D, SEG_NONE , SEG_NONE  }, PET_FRAME_COLON,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_A    , SEG_NONE , SEG_B    , SEG_NONE , SEG_NONE  }, PET_FRAME_COLON,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_F    , SEG_NONE , SEG_A    , SEG_NONE , SEG_NONE  }, PET_FRAME_COLON,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_A|SEG_E, SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE  }, PET_FRAME_COLON,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_D|SEG_F, SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE  }, PET_FRAME_COLON,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_E    , SEG_NONE , SEG_D    , SEG_NONE , SEG_NONE  }, PET_FRAME_COLON,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_D    , SEG_NONE , SEG_C    , SEG_NONE , SEG_NONE  }, 0              ,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_B|SEG_D, SEG_NONE , SEG_NONE  }, PET_FRAME_COLON,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_A|SEG_D, SEG_NONE , SEG_NONE  }, 0              ,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_B|SEG_C, SEG_NONE , SEG_NONE , SEG_NONE  }, PET_FRAME_COLON,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_B|SEG_C, SEG_NONE , SEG_NONE , SEG_NONE  }, 0              ,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_B|SEG_C, SEG_NONE , SEG_NONE , SEG_NONE  }, PET_FRAME_COLON,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_B|SEG_C, SEG_NONE , SEG_NONE , SEG_NONE  }, 0              ,  1 },
};
// 16 frames -> 15 poses

static const pet_frame_t _pet_frames_eat[] = {
    //  0         1         2         3         4         5         6         7         8         9            flags            hold
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_A|SEG_B|SEG_C|SEG_D, SEG_NONE , SEG_NONE , SEG_NONE  }, PET_FRAME_COLON,  4 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_B|SEG_C, SEG_NONE , SEG_NONE , SEG_NONE  }, PET_FRAME_COLON,  2 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_B|SEG_C, SEG_E|SEG_F, SEG_NONE , SEG_NONE  }, PET_FRAME_COLON,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_A|SEG_D|SEG_E|SEG_F, SEG_A|SEG_B|SEG_C|SEG_D, SEG_NONE , SEG_NONE  }, PET_FRAME_COLON,  2 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_A|SEG_B|SEG_C|SEG_D|SEG_E|SEG_F, SEG_NONE , SEG_NONE , SEG_NONE  }, 0              ,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_B|SEG_C|SEG_E|SEG_F|SEG_G, SEG_NONE , SEG_NONE , SEG_NONE  }, PET_FRAME_COLON,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_A|SEG_B|SEG_C|SEG_D|SEG_E|SEG_F, SEG_NONE , SEG_NONE , SEG_NONE  }, PET_FRAME_COLON,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_B|SEG_C|SEG_E|SEG_F|SEG_G, SEG_NONE , SEG_NONE , SEG_NONE  }, PET_FRAME_COLON,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_A|SEG_B|SEG_C|SEG_D|SEG_E|SEG_F, SEG_NONE , SEG_NONE , SEG_NONE  }, PET_FRAME_COLON,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_B|SEG_C|SEG_E|SEG_F|SEG_G, SEG_NONE , SEG_NONE , SEG_NONE  }, PET_FRAME_COLON,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_A|SEG_B|SEG_C|SEG_D|SEG_E|SEG_F, SEG_NONE , SEG_NONE , SEG_NONE  }, PET_FRAME_COLON,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_A|SEG_B|SEG_C|SEG_D, SEG_NONE , SEG_NONE , SEG_NONE  }, PET_FRAME_COLON,  5 },
};
// 21 frames -> 12 poses

static const pet_frame_t _pet_frames_kiss[] = {
    //  0         1         2         3         4         5         6         7         8         9            flags            hold
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_A|SEG_B|SEG_C|SEG_D, SEG_NONE , SEG_NONE , SEG_NONE  }, PET_FRAME_COLON,  4 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_B|SEG_C|SEG_E|SEG_F, SEG_NONE , SEG_NONE , SEG_NONE  }, PET_FRAME_COLON,  2 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_B|SEG_F|SEG_G, SEG_NONE , SEG_NONE , SEG_NONE  }, 0              ,  1 },
    { { SEG_NONE , SEG_D    , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_B|SEG_F|SEG_G, SEG_NONE , SEG_NONE , SEG_NONE  }, 0              ,  2 },
    { { SEG_NONE , SEG_D|SEG_G, SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_B|SEG_F|SEG_G, SEG_NONE , SEG_NONE , SEG_NONE  }, 0              ,  1 },
    { { SEG_NONE , SEG_A|SEG_G, SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_B|SEG_F|SEG_G, SEG_NONE , SEG_NONE , SEG_NONE  }, 0              ,  1 },
    { { SEG_NONE , SEG_A    , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_B|SEG_C, SEG_NONE , SEG_NONE , SEG_NONE  }, PET_FRAME_COLON,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_B|SEG_C, SEG_NONE , SEG_NONE , SEG_NONE  }, PET_FRAME_COLON,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_A|SEG_B|SEG_C|SEG_D, SEG_NONE , SEG_NONE , SEG_NONE  }, PET_FRAME_COLON,  3 },
};
// 16 frames -> 9 poses

static const pet_frame_t _pet_frames_snore[] = {
    //  0         1         2         3         4         5         6         7         8         9            flags            hold
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_F|SEG_G, SEG_A|SEG_D|SEG_E|SEG_F, SEG_A|SEG_B|SEG_C|SEG_D|SEG_E|SEG_F, SEG_NONE , SEG_NONE , SEG_NONE  }, 0              ,  8 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_E|SEG_G, SEG_A|SEG_D|SEG_E|SEG_F, SEG_B|SEG_F|SEG_G, SEG_NONE , SEG_NONE , SEG_NONE  }, 0              ,  1 },
    { { SEG_NONE , SEG_D    , SEG_NONE , SEG_NONE , SEG_E|SEG_G, SEG_A|SEG_D|SEG_E|SEG_F, SEG_B|SEG_F|SEG_G, SEG_NONE , SEG_NONE , SEG_NONE  }, 0              ,  1 },
    { { SEG_NONE , SEG_D|SEG_G, SEG_NONE , SEG_NONE , SEG_E|SEG_G, SEG_A|SEG_D|SEG_E|SEG_F, SEG_B|SEG_F|SEG_G, SEG_NONE , SEG_NONE , SEG_NONE  }, 0              ,  1 },
    { { SEG_NONE , SEG_A|SEG_G, SEG_NONE , SEG_NONE , SEG_E|SEG_G, SEG_A|SEG_D|SEG_E|SEG_F, SEG_B|SEG_F|SEG_G, SEG_NONE , SEG_NONE , SEG_NONE  }, 0              ,  1 },
    { { SEG_NONE , SEG_A    , SEG_NONE , SEG_NONE , SEG_E|SEG_G, SEG_A|SEG_D|SEG_E|SEG_F, SEG_B|SEG_F|SEG_G, SEG_NONE , SEG_NONE , SEG_NONE  }, 0              ,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_E|SEG_G, SEG_A|SEG_D|SEG_E|SEG_F, SEG_B|SEG_F|SEG_G, SEG_NONE , SEG_NONE , SEG_NONE  }, 0              ,  3 },
};
// 16 frames -> 7 poses

static const pet_frame_t _pet_frames_wake[] = {
    //  0         1         2         3         4         5         6         7         8         9            flags            hold
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_E|SEG_G, SEG_A|SEG_D|SEG_E|SEG_F, SEG_B|SEG_C, SEG_NONE , SEG_NONE , SEG_NONE  }, PET_FRAME_COLON,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_E|SEG_G, SEG_A|SEG_D|SEG_E|SEG_F, SEG_B|SEG_C, SEG_NONE , SEG_NONE , SEG_NONE  }, 0              ,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_E|SEG_G, SEG_A|SEG_D|SEG_E|SEG_F, SEG_B|SEG_C, SEG_NONE , SEG_NONE , SEG_NONE  }, PET_FRAME_COLON,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_E|SEG_G, SEG_A|SEG_D|SEG_E|SEG_F, SEG_B|SEG_C, SEG_NONE , SEG_NONE , SEG_NONE  }, 0              ,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_E|SEG_G, SEG_A|SEG_D|SEG_E|SEG_F, SEG_B|SEG_C, SEG_NONE , SEG_NONE , SEG_NONE  }, PET_FRAME_COLON,  3 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_E|SEG_G, SEG_A|SEG_D|SEG_E|SEG_F, SEG_B|SEG_C, SEG_E    , SEG_NONE , SEG_NONE  }, 0              ,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_E|SEG_G, SEG_A|SEG_D|SEG_E|SEG_F, SEG_A|SEG_D|SEG_E|SEG_F, SEG_A|SEG_B|SEG_C|SEG_D, SEG_NONE , SEG_NONE  }, 0              ,  1 },
    { { SEG_NONE , SEG_D    , SEG_NONE , SEG_NONE , SEG_E|SEG_G, SEG_A|SEG_D|SEG_E|SEG_F, SEG_A|SEG_D|SEG_E|SEG_F, SEG_A|SEG_B|SEG_C|SEG_D, SEG_NONE , SEG_NONE  }, 0              ,  1 },
    { { SEG_NONE , SEG_D|SEG_G, SEG_NONE , SEG_NONE , SEG_E|SEG_G, SEG_A|SEG_D|SEG_E|SEG_F, SEG_A|SEG_D|SEG_E|SEG_F, SEG_A|SEG_B|SEG_C|SEG_D, SEG_NONE , SEG_NONE  }, 0              ,  1 },
    { { SEG_NONE , SEG_G    , SEG_NONE , SEG_NONE , SEG_E|SEG_G, SEG_A|SEG_D|SEG_E|SEG_F, SEG_A|SEG_D|SEG_E|SEG_F, SEG_A|SEG_B|SEG_C|SEG_D, SEG_NONE , SEG_NONE  }, 0              ,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_E|SEG_G, SEG_A|SEG_D|SEG_E|SEG_F, SEG_A|SEG_D|SEG_E|SEG_F, SEG_A|SEG_B|SEG_C|SEG_D, SEG_NONE , SEG_NONE  }, 0              ,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_E|SEG_G, SEG_A|SEG_D|SEG_E|SEG_F, SEG_B|SEG_C, SEG_E    , SEG_NONE , SEG_NONE  }, PET_FRAME_COLON,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_E|SEG_G, SEG_A|SEG_D|SEG_E|SEG_F, SEG_B|SEG_C, SEG_NONE , SEG_NONE , SEG_NONE  }, PET_FRAME_COLON,  3 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_G    , SEG_A|SEG_D|SEG_E|SEG_F, SEG_B|SEG_C, SEG_NONE , SEG_NONE , SEG_NONE  }, PET_FRAME_COLON,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_A|SEG_D|SEG_E|SEG_F, SEG_B|SEG_C, SEG_NONE , SEG_NONE , SEG_NONE  }, PET_FRAME_COLON,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_B|SEG_C, SEG_B|SEG_C, SEG_NONE , SEG_NONE , SEG_NONE  }, PET_FRAME_COLON,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_B|SEG_C, SEG_NONE , SEG_NONE , SEG_NONE  }, PET_FRAME_COLON,  1 },
};
// 21 frames -> 17 poses

// The pet squats and the pile lands in cell 9. The character layer
// draws the squat, the status layer keeps the pile afterwards -- see
// _pet_frames_pile, whose single pose is this animation's last.
static const pet_frame_t _pet_frames_poo[] = {
    //  0         1         2         3         4         5         6         7         8         9            flags            hold
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_A|SEG_B|SEG_C|SEG_D, SEG_NONE , SEG_NONE , SEG_NONE  }, PET_FRAME_COLON,  2 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_B|SEG_C, SEG_NONE , SEG_NONE , SEG_NONE  }, PET_FRAME_COLON,  3 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_B|SEG_C, SEG_NONE , SEG_NONE , SEG_NONE  }, 0              ,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_B|SEG_C, SEG_G    , SEG_NONE , SEG_NONE  }, 0              ,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_B|SEG_C, SEG_NONE , SEG_G    , SEG_NONE  }, 0              ,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_B|SEG_C, SEG_NONE , SEG_NONE , SEG_G     }, 0              ,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_B|SEG_C, SEG_G    , SEG_NONE , SEG_B|SEG_C }, 0              ,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_B|SEG_C, SEG_NONE , SEG_G    , SEG_B|SEG_C }, 0              ,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_B|SEG_C, SEG_NONE , SEG_NONE , SEG_B|SEG_C|SEG_G }, PET_FRAME_COLON,  3 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_A|SEG_B|SEG_C|SEG_D, SEG_NONE , SEG_NONE , SEG_B|SEG_C|SEG_G }, PET_FRAME_COLON,  2 },
};
// 16 frames -> 10 poses

static const pet_frame_t _pet_frames_barf[] = {
    //  0         1         2         3         4         5         6         7         8         9            flags            hold
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_A|SEG_B|SEG_D|SEG_E|SEG_G, SEG_NONE , SEG_NONE , SEG_NONE  }, PET_FRAME_COLON,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_A|SEG_C|SEG_D|SEG_F|SEG_G, SEG_NONE , SEG_NONE , SEG_NONE  }, PET_FRAME_COLON,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_A|SEG_B|SEG_D|SEG_E|SEG_G, SEG_NONE , SEG_NONE , SEG_NONE  }, PET_FRAME_COLON,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_A|SEG_C|SEG_D|SEG_F|SEG_G, SEG_NONE , SEG_NONE , SEG_NONE  }, PET_FRAME_COLON,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_A|SEG_B|SEG_D|SEG_E|SEG_G, SEG_NONE , SEG_NONE , SEG_NONE  }, PET_FRAME_COLON,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_A|SEG_C|SEG_D|SEG_F|SEG_G, SEG_NONE , SEG_NONE , SEG_NONE  }, PET_FRAME_COLON,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_E|SEG_F, SEG_NONE , SEG_NONE , SEG_NONE  }, 0              ,  3 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_A|SEG_D|SEG_E|SEG_F, SEG_D    , SEG_NONE , SEG_NONE  }, PET_FRAME_COLON,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_A|SEG_D|SEG_E|SEG_F, SEG_D|SEG_G, SEG_D    , SEG_NONE  }, PET_FRAME_COLON,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_A|SEG_D|SEG_E|SEG_F, SEG_G    , SEG_D|SEG_G, SEG_D     }, PET_FRAME_COLON,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_A|SEG_D|SEG_E|SEG_F, SEG_D    , SEG_NONE , SEG_C|SEG_D|SEG_G }, PET_FRAME_COLON,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_A|SEG_D|SEG_E|SEG_F, SEG_D    , SEG_NONE , SEG_B|SEG_C|SEG_G }, PET_FRAME_COLON,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_A|SEG_D|SEG_E|SEG_F, SEG_D|SEG_G, SEG_D    , SEG_B|SEG_C }, PET_FRAME_COLON,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_A|SEG_D|SEG_E|SEG_F, SEG_G    , SEG_D|SEG_G, SEG_B|SEG_C|SEG_D }, PET_FRAME_COLON,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_B|SEG_C, SEG_NONE , SEG_G    , SEG_B|SEG_C|SEG_D|SEG_G }, PET_FRAME_COLON,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_B|SEG_C, SEG_NONE , SEG_NONE , SEG_B|SEG_C|SEG_G }, PET_FRAME_COLON,  1 },
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_B|SEG_C, SEG_NONE , SEG_NONE , SEG_B|SEG_C }, PET_FRAME_COLON,  2 },
};
// 20 frames -> 17 poses

// What the poo scene leaves on the floor: cell 9's centre and bottom edge. One
// looping frame, held until swept.
static const pet_frame_t _pet_frames_pile[] = {
    //  0  1  2  3  4  5  6  7  8  9                                flags  hold
    { { 0, 0, 0, 0, 0, 0, 0, 0, 0, SEG_G | SEG_B | SEG_C },            0,  PET_ANIM_HZ },
};

// What the barf scene leaves: the base without the stem. The pile is this plus
// SEG_G, so drawing the pile alone covers both.
static const pet_frame_t _pet_frames_puddle[] = {
    //  0  1  2  3  4  5  6  7  8  9                                flags  hold
    { { 0, 0, 0, 0, 0, 0, 0, 0, 0, SEG_B | SEG_C },                    0,  PET_ANIM_HZ },
};

// Where each sound belongs. A cue with no mask fires as the animation begins;
// otherwise it fires on the frame where those segments light, or with on_clear
// go dark. Naming a moment in the art rather than a time means a redrawn
// animation carries its sounds with it; the cost is that a cue whose condition
// the art never reaches is silent rather than wrong, so each one below names the
// frame it is waiting for.

// Breathe in as the loop starts, out as the puff appears beside the mouth.
static const pet_cue_t _pet_cues_snore[] = {
    { PET_SOUND_SNORE_IN,  0, 0,     false, true  },
    { PET_SOUND_SNORE_OUT, 1, SEG_D, false, true  },
};

// The pucker is the first frame to light cell 6's centre.
static const pet_cue_t _pet_cues_kiss[] = {
    { PET_SOUND_KISS, 6, SEG_G, false, true },
};

// Gulp when the pip clears cell 7, once; chew on every turn of the jaw in
// cell 6, as many times as it is drawn.
static const pet_cue_t _pet_cues_eat[] = {
    { PET_SOUND_EAT_GULP, 7, 0xFF,  true,  true  },
    { PET_SOUND_EAT_CHEW, 6, SEG_G, false, false },
};

// Uh oh as it starts; the slide once the contents reach cell 7. The slide fires
// once, since cell 7 flickers as they tumble through it.
static const pet_cue_t _pet_cues_barf[] = {
    { PET_SOUND_BARF_UHOH,  0, 0,     false, true },
    { PET_SOUND_BARF_SLIDE, 7, SEG_D, false, true },
};

// The knock lands when the pile reaches the bottom edge of cell 9.
static const pet_cue_t _pet_cues_poo[] = {
    { PET_SOUND_POO, 9, SEG_B | SEG_C, false, true },
};

static const pet_cue_t _pet_cues_play_small[] = {
    { PET_SOUND_PLAY_SMALL, 0, 0, false, true },
};

static const pet_cue_t _pet_cues_play_big[] = {
    { PET_SOUND_PLAY_BIG, 0, 0, false, true },
};

// The yawn, on the frame cell 7 fills as the mouth opens wide. Woken by a poke
// rather than by the morning, the pet grumbles instead -- see _pet_cue_audible.
static const pet_cue_t _pet_cues_wake[] = {
    { PET_SOUND_WAKE, 7, SEG_A | SEG_B | SEG_C | SEG_D, false, true },
};

// Two halves either side of the pet being gone: the fade under the shrinking
// tombstone, the rise when cell 6 draws the first stroke of a face again. Cell 6
// carries the tombstone's own B|C|G in frame 0, so the rise waits on SEG_A,
// which only the returning face lights.
static const pet_cue_t _pet_cues_resurrect[] = {
    { PET_SOUND_RESURRECT_FADE, 0, 0,     false, true },
    { PET_SOUND_RESURRECT_RISE, 6, SEG_A, false, true },
};

static const pet_anim_t _pet_anims[PET_ANIM_COUNT] = {
    //                        frames and count                      loop   layer                cues and count
    [PET_ANIM_NONE]       = { PET_NO_FRAMES,                       false, PET_LAYER_CHARACTER, PET_NO_CUES },
    // moods: loop while the pet rests
    [PET_ANIM_HAPPY]      = { PET_FRAMES(_pet_frames_happy),       true,  PET_LAYER_CHARACTER, PET_NO_CUES },
    [PET_ANIM_CONFUSED]   = { PET_FRAMES(_pet_frames_confused),    true,  PET_LAYER_CHARACTER, PET_NO_CUES },
    [PET_ANIM_UPSET]      = { PET_FRAMES(_pet_frames_upset),       true,  PET_LAYER_CHARACTER, PET_NO_CUES },
    [PET_ANIM_ANGRY]      = { PET_FRAMES(_pet_frames_angry),       true,  PET_LAYER_CHARACTER, PET_NO_CUES },
    [PET_ANIM_DEAD]       = { PET_FRAMES(_pet_frames_dead),        true,  PET_LAYER_CHARACTER, PET_NO_CUES },
    // one-shots
    [PET_ANIM_RESURRECT]  = { PET_FRAMES(_pet_frames_resurrect),   false, PET_LAYER_CHARACTER, PET_CUES(_pet_cues_resurrect) },
    [PET_ANIM_PLAY_SMALL] = { PET_FRAMES(_pet_frames_play_small),  false, PET_LAYER_CHARACTER, PET_CUES(_pet_cues_play_small) },
    [PET_ANIM_PLAY_BIG]   = { PET_FRAMES(_pet_frames_play_big),    false, PET_LAYER_CHARACTER, PET_CUES(_pet_cues_play_big) },
    [PET_ANIM_EAT]        = { PET_FRAMES(_pet_frames_eat),         false, PET_LAYER_CHARACTER, PET_CUES(_pet_cues_eat) },
    [PET_ANIM_KISS]       = { PET_FRAMES(_pet_frames_kiss),        false, PET_LAYER_CHARACTER, PET_CUES(_pet_cues_kiss) },
    [PET_ANIM_SNORE]      = { PET_FRAMES(_pet_frames_snore),       true,  PET_LAYER_CHARACTER, PET_CUES(_pet_cues_snore) },
    [PET_ANIM_WAKE]       = { PET_FRAMES(_pet_frames_wake),        false, PET_LAYER_CHARACTER, PET_CUES(_pet_cues_wake) },
    // scenes: play on the character layer and leave the floor changed
    [PET_ANIM_POO]        = { PET_FRAMES(_pet_frames_poo),         false, PET_LAYER_CHARACTER, PET_CUES(_pet_cues_poo) },
    [PET_ANIM_BARF]       = { PET_FRAMES(_pet_frames_barf),        false, PET_LAYER_CHARACTER, PET_CUES(_pet_cues_barf) },
    // the status layer: what stays on the floor
    [PET_ANIM_PILE]       = { PET_FRAMES(_pet_frames_pile),        true,  PET_LAYER_STATUS,    PET_NO_CUES },
    [PET_ANIM_PUDDLE]     = { PET_FRAMES(_pet_frames_puddle),      true,  PET_LAYER_STATUS,    PET_NO_CUES },
};


// The sounds. Format is note, duration, ..., 0, with durations in 1/64 s; a
// negative value rewinds that many notes and the value after it is the repeat
// count. Notes are in watch_tcc.h. None of them waits for anything: lining a
// sound up with a moment of an animation is the cue's job, above.

static int8_t _pet_sound_snore_in[]  = { BUZZER_NOTE_C6, 32, 0 };
static int8_t _pet_sound_snore_out[] = { BUZZER_NOTE_C5, 32, 0 };

// Four semitones up, 1/32 s each.
static int8_t _pet_sound_kiss[] = {
    BUZZER_NOTE_D6,              2,
    BUZZER_NOTE_D6SHARP_E6FLAT,  2,
    BUZZER_NOTE_E6,              2,
    BUZZER_NOTE_F6,              2,
    0
};

static int8_t _pet_sound_eat_gulp[] = { BUZZER_NOTE_E6, 6, 0 };
static int8_t _pet_sound_eat_chew[] = { BUZZER_NOTE_G5, 6, 0 };

// Two falling notes over the wriggling mouth.
static int8_t _pet_sound_barf_uhoh[] = {
    BUZZER_NOTE_A5, 24,
    BUZZER_NOTE_F5, 24,
    0
};

// A chromatic octave down, travelling with the contents.
static int8_t _pet_sound_barf_slide[] = {
    BUZZER_NOTE_C6,              6,
    BUZZER_NOTE_B5,              6,
    BUZZER_NOTE_A5SHARP_B5FLAT,  6,
    BUZZER_NOTE_A5,              6,
    BUZZER_NOTE_G5SHARP_A5FLAT,  6,
    BUZZER_NOTE_G5,              6,
    BUZZER_NOTE_F5SHARP_G5FLAT,  6,
    BUZZER_NOTE_F5,              6,
    BUZZER_NOTE_E5,              6,
    BUZZER_NOTE_D5SHARP_E5FLAT,  6,
    BUZZER_NOTE_D5,              6,
    BUZZER_NOTE_C5SHARP_D5FLAT,  6,
    0
};

// One short low knock.
static int8_t _pet_sound_poo[] = { BUZZER_NOTE_C4, 5, 0 };

// Big is the same arpeggio a whole tone up.
static int8_t _pet_sound_play_small[] = {
    BUZZER_NOTE_C5, 5, BUZZER_NOTE_E5, 5, BUZZER_NOTE_G5, 5, BUZZER_NOTE_C6, 5,
    BUZZER_NOTE_G5, 5, BUZZER_NOTE_E5, 5, BUZZER_NOTE_C5, 5,
    0
};

static int8_t _pet_sound_play_big[] = {
    BUZZER_NOTE_D5, 5, BUZZER_NOTE_F5SHARP_G5FLAT, 5, BUZZER_NOTE_A5, 5, BUZZER_NOTE_D6, 5,
    BUZZER_NOTE_A5, 5, BUZZER_NOTE_F5SHARP_G5FLAT, 5, BUZZER_NOTE_D5, 5,
    0
};

// A yawn: up into the stretch, then settling back.
static int8_t _pet_sound_wake[] = {
    BUZZER_NOTE_E5, 12,
    BUZZER_NOTE_G5, 12,
    BUZZER_NOTE_C6, 16,
    BUZZER_NOTE_A5, 20,
    0
};

// Two low notes under the vanishing tombstone, then a major arpeggio climbing
// two octaves as the pet reassembles.
static int8_t _pet_sound_resurrect_fade[] = {
    BUZZER_NOTE_G4, 12,
    BUZZER_NOTE_C4, 16,
    0
};

static int8_t _pet_sound_resurrect_rise[] = {
    BUZZER_NOTE_C5, 5,
    BUZZER_NOTE_E5, 5,
    BUZZER_NOTE_G5, 5,
    BUZZER_NOTE_C6, 5,
    BUZZER_NOTE_E6, 5,
    BUZZER_NOTE_G6, 14,
    0
};

// The sounds below answer something with no art to cue against -- a button that
// only moves a counter, or a change of state whose animation loops and so would
// re-cue for ever. They are played straight from the code that causes them.

// One short blip per press, so a burst of them ratchets. A press past
// PET_FOOD_MAX still blips: it restarted the settle timer, so it was not
// ignored, it just had no pip left to add.
static int8_t _pet_sound_feed[] = { BUZZER_NOTE_D6, 3, 0 };

// A brisk brush downwards.
static int8_t _pet_sound_sweep[] = {
    BUZZER_NOTE_G5, 2,
    BUZZER_NOTE_E5, 2,
    BUZZER_NOTE_C5, 3,
    0
};

// Woken when it wanted to be asleep. Low and curt, and the lowest thing the pet
// says, so a penalty never reads as a reward.
static int8_t _pet_sound_grumble[] = {
    BUZZER_NOTE_E4, 10,
    BUZZER_NOTE_C4, 14,
    0
};

// Four falling notes, slow enough to land as an ending.
static int8_t _pet_sound_death[] = {
    BUZZER_NOTE_C5, 16,
    BUZZER_NOTE_A4, 16,
    BUZZER_NOTE_F4, 16,
    BUZZER_NOTE_D4, 28,
    0
};

// A whole tic gained or lost while you were watching. Two notes only, so the
// step reads as punctuation after the interaction's own sound rather than as
// another event.
static int8_t _pet_sound_mood_up[]   = { BUZZER_NOTE_G5, 4, BUZZER_NOTE_C6, 7, 0 };
static int8_t _pet_sound_mood_down[] = { BUZZER_NOTE_C6, 4, BUZZER_NOTE_G5, 7, 0 };

static int8_t *_pet_sounds[PET_SOUND_COUNT] = {
    [PET_SOUND_SNORE_IN]   = _pet_sound_snore_in,
    [PET_SOUND_SNORE_OUT]  = _pet_sound_snore_out,
    [PET_SOUND_KISS]       = _pet_sound_kiss,
    [PET_SOUND_BARF_UHOH]  = _pet_sound_barf_uhoh,
    [PET_SOUND_BARF_SLIDE] = _pet_sound_barf_slide,
    [PET_SOUND_EAT_GULP]   = _pet_sound_eat_gulp,
    [PET_SOUND_EAT_CHEW]   = _pet_sound_eat_chew,
    [PET_SOUND_POO]        = _pet_sound_poo,
    [PET_SOUND_PLAY_SMALL] = _pet_sound_play_small,
    [PET_SOUND_PLAY_BIG]   = _pet_sound_play_big,
    [PET_SOUND_WAKE]       = _pet_sound_wake,
    [PET_SOUND_RESURRECT_FADE] = _pet_sound_resurrect_fade,
    [PET_SOUND_RESURRECT_RISE] = _pet_sound_resurrect_rise,
    [PET_SOUND_FEED]       = _pet_sound_feed,
    [PET_SOUND_SWEEP]      = _pet_sound_sweep,
    [PET_SOUND_GRUMBLE]    = _pet_sound_grumble,
    [PET_SOUND_DEATH]      = _pet_sound_death,
    [PET_SOUND_MOOD_UP]    = _pet_sound_mood_up,
    [PET_SOUND_MOOD_DOWN]  = _pet_sound_mood_down,
};

// Play a sound and flash SIGNAL. movement_button_should_sound() is the watch's
// own BTN beep setting, so N in the settings face mutes the pet; the SIGNAL
// flash stands in for the sound either way.
static void _pet_play_sound(pet_state_t *s, pet_sound_id_t id) {
    if (movement_button_should_sound()) {
        movement_play_sequence(_pet_sounds[id], BUZZER_PRIORITY_BUTTON);
    }
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

// Which of the day's sittings a local hour falls in. Hours outside the waking
// day clamp to the nearest one: feeding is blocked at night, but a plate queued
// before PET_HOUR_SLEEP can still be draining after it, and those last pips
// belong to dinner rather than to a fourth sitting that does not exist.
static uint8_t _pet_meal_segment(uint8_t hour) {
    if (hour < PET_HOUR_WAKE) return 0;
    uint8_t seg = (uint8_t) ((hour - PET_HOUR_WAKE) / PET_FEED_SEGMENT_HOURS);
    return seg < PET_FEED_SEGMENTS ? seg : PET_FEED_SEGMENTS - 1;
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

// Turn tap detection on or off, touching the hardware only when the answer
// changes. It runs the accelerometer at 400 Hz in low-noise mode, so a pet that
// ignores motion — a dead one — should not have it on.
static void _pet_set_tap_detection(pet_state_t *s, bool want) {
    if (want == s->tap_enabled) return;
    if (want) movement_enable_tap_detection_if_available(false);
    else      movement_disable_tap_detection_if_available();
    s->tap_enabled = want;
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
// Decay is charged in waking seconds, not wall seconds.

static uint32_t _pet_local_ts(uint32_t utc_ts) {
    int32_t offset = movement_get_current_timezone_offset();
    if (offset < 0) {
        uint32_t behind = (uint32_t) -offset;
        return (utc_ts > behind) ? utc_ts - behind : 0;
    }
    return utc_ts + (uint32_t) offset;
}

// Waking seconds from the epoch up to local time local_ts. Monotonic, so the
// waking seconds in any span are the difference of its two ends.
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

// Waking seconds between two UTC timestamps, applying the current UTC offset to
// both ends.
static uint32_t _pet_awake_between(uint32_t from_ts, uint32_t to_ts) {
    if (to_ts <= from_ts) return 0;
    return _pet_awake_seconds(_pet_local_ts(to_ts))
         - _pet_awake_seconds(_pet_local_ts(from_ts));
}

// ============================================================================
// 3. Compositor
// ============================================================================

// Light exactly the segments in mask at one LCD position. Clears everything
// first so a tied pair (e.g. 6A/6D) ends up on if either half was asked for.
static void _pet_draw_position(const digit_mapping_t *maps, uint8_t position, uint8_t mask) {
    const digit_mapping_t *map = &maps[position];

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
    if (flags & PET_FRAME_COLON)  watch_set_colon();                           else watch_clear_colon();
    if (flags & PET_FRAME_SIGNAL) watch_set_indicator(WATCH_INDICATOR_SIGNAL); else watch_clear_indicator(WATCH_INDICATOR_SIGNAL);
    if (flags & PET_FRAME_BELL)   watch_set_indicator(WATCH_INDICATOR_BELL);   else watch_clear_indicator(WATCH_INDICATOR_BELL);
}

// Composite every layer through its mask into one framebuffer, then push only
// the cells that differ from the shadow.
static void _pet_draw(pet_state_t *s) {
    uint8_t fb[10] = { 0 };
    uint8_t flags = 0;

    for (uint8_t l = 0; l < PET_LAYER_COUNT; l++) {
        const pet_anim_t *a = &_pet_anims[s->layer[l].anim];
        if (s->layer[l].anim == PET_ANIM_NONE) continue;
        const pet_frame_t *f = &a->frames[s->layer[l].frame];
        const pet_layer_def_t *d = &_pet_layers[l];
        for (uint8_t p = 0; p < 10; p++) fb[p] |= f->seg[p] & d->seg[p];
        flags |= f->flags & d->flags;
    }

    // Food pips: however many are queued, lit in order.
    for (uint8_t i = 0; i < s->food_queue && i < PET_FOOD_MAX; i++) {
        fb[PET_FOOD_POSITION] |= _pet_food_pips[i];
    }

    // Transient marks. Sideways, G|H reads as a plus and H alone as a minus.
    if (s->buff_ticks)   fb[PET_BUFF_POSITION] |= SEG_G | SEG_H;
    if (s->debuff_ticks) fb[PET_BUFF_POSITION] |= SEG_H;
    if (s->bell_ticks)   flags |= PET_FRAME_BELL;
    if (s->signal_ticks) flags |= PET_FRAME_SIGNAL;

    for (uint8_t p = 0; p < 10; p++) {
        if (fb[p] == s->shadow[p] && !s->shadow_stale) continue;
        _pet_draw_position(Classic_LCD_Display_Mapping, p, fb[p]);
        s->shadow[p] = fb[p];
    }
    if (flags != s->shadow_flags || s->shadow_stale) {
        _pet_draw_flags(flags);
        s->shadow_flags = flags;
    }
    s->shadow_stale = false;
}

// Force the next redraw to push every cell, for when something outside this
// face — a face switch, low-energy mode — has cleared the display.
static void _pet_invalidate(pet_state_t *s) {
    s->shadow_stale = true;
}

// ============================================================================
// 4. Layer engine
// ============================================================================
//
// Each layer plays its own animation on its own clock. The character layer has
// a short queue, so a scene can say "wake, then the mood"; when it empties the
// pet rests. Other layers settle to their idle animation instead.

static void _pet_rest(pet_state_t *s);

static uint8_t _pet_frame_hold(const pet_anim_t *a, uint8_t frame) {
    // Label-only animations show for one second.
    return a->frames ? a->frames[frame].hold : PET_ANIM_HZ;
}

// True while a one-shot is still on screen. Looping animations are the resting
// state and never count as busy; scene timers wait on this before moving on.
static bool _pet_anim_busy(const pet_state_t *s) {
    uint8_t id = s->layer[PET_LAYER_CHARACTER].anim;
    return id != PET_ANIM_NONE && !_pet_anims[id].loop;
}

// Cues the scene rate-limits rather than the art: the pet breathes on every turn
// of the sleep loop, but only the first PET_SNORE_AUDIBLE_BREATHS are voiced.
static bool _pet_cue_audible(const pet_state_t *s, pet_sound_id_t id) {
    if (id == PET_SOUND_SNORE_IN || id == PET_SOUND_SNORE_OUT) {
        return s->breath < PET_SNORE_AUDIBLE_BREATHS;
    }
    // PET_ANIM_WAKE plays for the morning and for a poke in the night. Only the
    // morning yawns; _pet_disturb has already grumbled for the poke, and the
    // scene is moved before the animation starts so this can tell them apart.
    if (id == PET_SOUND_WAKE) return s->scene != PET_SCENE_NIGHT_AWAKE;
    return true;
}

// Fire any cue whose moment has just arrived, comparing the frame being left
// against the one being entered. `begun` is true when the animation is starting
// or looping round, which is what a cue with no mask waits for.
static void _pet_fire_cues(pet_state_t *s, pet_layer_t *L, const pet_anim_t *a,
                           const uint8_t *prev, const uint8_t *cur, bool begun) {
    for (uint8_t i = 0; i < a->cue_count && i < 8; i++) {
        const pet_cue_t *c = &a->cues[i];
        bool fire;
        if (c->mask == 0) {
            fire = begun;
        } else if (c->on_clear) {
            fire = (prev[c->position] & c->mask) && !(cur[c->position] & c->mask);
        } else {
            fire = !(prev[c->position] & c->mask) && (cur[c->position] & c->mask);
        }
        if (!fire) continue;
        if (c->once) {
            // The condition can come true more than once in a pass.
            if (L->cues_fired & (1 << i)) continue;
            L->cues_fired |= (uint8_t) (1 << i);
        }
        if (_pet_cue_audible(s, (pet_sound_id_t) c->sound)) {
            _pet_play_sound(s, (pet_sound_id_t) c->sound);
        }
    }
}

static const uint8_t _pet_no_segments[10] = { 0 };

// The segments a frame of this animation shows, or nothing if it has no art.
static const uint8_t *_pet_frame_segs(const pet_anim_t *a, uint8_t frame) {
    return a->frames ? a->frames[frame].seg : _pet_no_segments;
}

// Point a layer at an animation, from frame zero.
static void _pet_layer_play(pet_state_t *s, pet_layer_id_t l, pet_anim_id_t id) {
    pet_layer_t *L = &s->layer[l];
    const pet_anim_t *a = &_pet_anims[id];
    L->anim = (uint8_t) id;
    L->frame = 0;
    L->hold_left = _pet_frame_hold(a, 0);
    L->cues_fired = 0;
    // Frame 0 is compared against a blank screen, so a cue already true in it
    // counts as having just come true.
    _pet_fire_cues(s, L, a, _pet_no_segments, _pet_frame_segs(a, 0), true);
}

// Play an animation on whichever layer the animation table assigns it to.
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

// Point the status layer at whatever is on the floor.
static void _pet_set_status(pet_state_t *s) {
    pet_anim_id_t want = PET_ANIM_NONE;
    if (_pet_mood(s) != PET_MOOD_DEAD) {
        // A poo still owed its scene leaves the floor clean until it plays. The
        // pile is the puddle plus its stem, so the pile alone covers both.
        if (s->has_poo && !s->poo_pending) want = PET_ANIM_PILE;
        else if (s->has_barf)              want = PET_ANIM_PUDDLE;
    }
    pet_layer_t *L = &s->layer[PET_LAYER_STATUS];
    L->idle = (uint8_t) want;
    _pet_layer_play(s, PET_LAYER_STATUS, want);
}

static void _pet_layer_tick(pet_state_t *s, pet_layer_id_t l) {
    pet_layer_t *L = &s->layer[l];
    if (L->anim == PET_ANIM_NONE) return;

    const pet_anim_t *a = &_pet_anims[L->anim];
    uint8_t count = a->frames ? a->count : 1;
    bool loop_here = a->loop;
    bool queued = (l == PET_LAYER_CHARACTER) && (s->queue_len > 0);

#if PET_SHOWCASE
    // The showcase loops whatever it is holding, one-shots included.
    if (s->showcase_on) { loop_here = true; queued = false; }
#endif

    if (L->hold_left > 1) {
        L->hold_left--;
        return;
    }

    uint8_t leaving = L->frame;
    L->frame++;
    if (L->frame < count) {
        L->hold_left = _pet_frame_hold(a, L->frame);
        _pet_fire_cues(s, L, a, _pet_frame_segs(a, leaving), _pet_frame_segs(a, L->frame), false);
        _pet_draw(s);
        return;
    }

    // The animation just ended.
    if (loop_here && !queued) {
        L->frame = 0;
        L->hold_left = _pet_frame_hold(a, 0);
        // A turn of the sleep loop is a breath, whether or not it is voiced.
        if (L->anim == PET_ANIM_SNORE) {
            s->breath = (uint8_t) ((s->breath + 1) % PET_SNORE_BREATH_CYCLE);
        }
        L->cues_fired = 0;      // a fresh turn of the loop cues afresh
        _pet_fire_cues(s, L, a, _pet_frame_segs(a, leaving), _pet_frame_segs(a, 0), true);
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

// Put a plus or a minus in position 0 for PET_FLASH_TICKS.
static void _pet_flash_buff(pet_state_t *s, bool gained) {
    s->buff_ticks   = gained ? PET_FLASH_TICKS : 0;
    s->debuff_ticks = gained ? 0 : PET_FLASH_TICKS;
}

// Settle into the looping mood animation. Scenes with their own timer (feeding,
// playing, awake at night) keep their scene; anything else becomes idle.
static void _pet_rest(pet_state_t *s) {
    pet_mood_t mood = _pet_mood(s);
    s->queue_len = 0;
    _pet_set_status(s);
    // Every route in and out of death passes through here.
    _pet_set_tap_detection(s, mood != PET_MOOD_DEAD);

    if (mood == PET_MOOD_DEAD) {
        // The knell belongs to the moment of death, not to every settle that
        // finds the pet already gone -- PET_ANIM_DEAD loops, so a cue on it
        // would toll for ever.
        if (s->scene != PET_SCENE_DEAD) _pet_play_sound(s, PET_SOUND_DEATH);
        s->shown_mood = (uint8_t) mood;
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
    // A mood the pet reached in front of you gets a step as the new face goes
    // on: the moods are declared best to worst, so the comparison is the
    // direction. The mood animations loop, which is why this is an edge here
    // rather than a cue on them.
    if ((pet_mood_t) s->shown_mood != mood) {
        _pet_play_sound(s, mood < (pet_mood_t) s->shown_mood ? PET_SOUND_MOOD_UP
                                                             : PET_SOUND_MOOD_DOWN);
        s->shown_mood = (uint8_t) mood;
    }
    _pet_start_anim(s, _pet_mood_anim(mood));
}

#if PET_SHOWCASE
// -- Showcase -----------------------------------------------------------------
//
// The gallery: walk the animations and moods on demand rather than waiting for
// the clock to produce them. Button map and rationale are next to PET_SHOWCASE.
//
// It borrows the live pet's screen, so the important part is giving it back --
// every exit routes through _pet_showcase_exit, which rests the pet into its
// real mood. See the note there.

// Step to the next animation and hold it; walking off the end of the list hands
// the screen back to the live pet. The cursor lives in showcase_anim rather than
// showcase_on, which the 0.5 s long press has always already cleared by now.
static void _pet_showcase_next(pet_state_t *s) {
    pet_anim_id_t next = s->showcase_anim ? (pet_anim_id_t) (s->showcase_anim + 1)
                                          : PET_ANIM_HAPPY;
    if (next >= PET_ANIM_COUNT) {
        s->showcase_on = false;
        s->showcase_anim = PET_ANIM_NONE;   // the next hold starts the walk over
        _pet_rest(s);
        return;
    }
    s->showcase_on = true;
    s->showcase_anim = (uint8_t) next;
    s->queue_len = 0;
    // Clear both layers, so stepping between the two doesn't leave the other up.
    _pet_layer_play(s, PET_LAYER_CHARACTER, PET_ANIM_NONE);
    _pet_layer_play(s, PET_LAYER_STATUS, PET_ANIM_NONE);
    _pet_start_anim(s, next);
}

// Hand the screen back to the live pet.
//
// Clearing showcase_on is not enough on its own. It only stops _pet_layer_tick
// forcing a one-shot to loop, so a one-shot ends and rests of its own accord --
// but a looping animation keeps looping, and a status animation leaves the
// character layer on PET_ANIM_NONE, which the tick skips entirely. Either way
// nothing reaches _pet_rest, and the showcased animation stays on screen for
// good: a healthy pet stuck confused, snoring at noon, dead, or gone altogether
// behind a pile it never made.
//
// Only rests if the showcase actually had the screen, since _pet_rest would
// otherwise cut short whatever the pet was doing.
static void _pet_showcase_exit(pet_state_t *s, bool keep_cursor) {
    bool had_screen = s->showcase_on;
    s->showcase_on = false;
    if (!keep_cursor) s->showcase_anim = PET_ANIM_NONE;
    if (had_screen) _pet_rest(s);
}

// Push the mood up one tic, wrapping past dead back to zero. _pet_rest sorts out
// the scene, including climbing back out of PET_SCENE_DEAD.
static void _pet_showcase_step_mood(pet_state_t *s) {
    uint8_t next = s->quarter_tics + PET_TIC(1);
    s->showcase_on = false;
    s->quarter_tics = (next > PET_QT_DEAD) ? 0 : next;
    s->awake_residual = 0;
    _pet_rest(s);
}
#endif

// ============================================================================
// 5. Simulation
// ============================================================================

// Land a poo whose wall-clock countdown has expired; one at a time. Called from
// the tick as well as from the catch-up, so one coming due while the face is on
// screen is noticed.
static bool _pet_poo_arrives(pet_state_t *s, uint32_t now) {
    if (s->quarter_tics >= PET_QT_DEAD) return false;    // a grave does not digest
    if (s->has_poo || s->poo_due_ts == 0 || now < s->poo_due_ts) return false;
    s->has_poo = true;
    // The scene is owed to whoever next has the face open, however stale.
    s->poo_pending = true;
    s->poo_since_ts = s->poo_due_ts;
    s->poo_residual = 0;
    s->poo_due_ts = 0;
    return true;
}

// Apply everything that should have happened since the last update: passive
// decay, the missed-feed penalty, the poo arriving, the poo sitting there.
// Called on every activate, so nothing runs while the face is in the background.
static void _pet_catch_up(pet_state_t *s) {
    uint32_t now = _pet_now();
    watch_date_time_t local = movement_get_local_date_time();

    // The clock moved backwards (time was set): re-anchor rather than punish.
    if (now < s->last_update_ts) s->last_update_ts = now;
    if (now < s->last_fed_ts)    s->last_fed_ts = now;
    if (now < s->poo_since_ts)   s->poo_since_ts = now;
    // Same for the play cooldown, except zero is the forgiving end.
    if (now < s->last_play_buff_ts) s->last_play_buff_ts = 0;

    if (s->quarter_tics < PET_QT_DEAD) {
        // Passive: +1 tic per 6 h of waking time, with the leftover seconds
        // carried in awake_residual rather than rounded away.
        uint32_t awake = _pet_awake_between(s->last_update_ts, now) + s->awake_residual;
        uint32_t qt = awake / PET_SECONDS_PER_QT;
        s->awake_residual = (uint16_t) (awake % PET_SECONDS_PER_QT);
        s->last_update_ts = now;
        if (qt > 0) {
            if (qt > PET_QT_DEAD) qt = PET_QT_DEAD;
            _pet_add_qt(s, (int16_t) qt);
        }

        // +1 tic per missed wall-clock day without eating, on top of passive
        // decay.
        uint32_t days = (now - s->last_fed_ts) / PET_MISSED_FEED_SECONDS;
        if (days > 0) {
            s->last_fed_ts += days * PET_MISSED_FEED_SECONDS;
            if (days > 6) days = 6;
            _pet_add_qt(s, (int16_t) PET_TIC(days));
        }

        _pet_poo_arrives(s, now);

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

    // The hug cap resets with the local calendar day.
    if (s->hug_day != local.unit.day) {
        s->hug_day = local.unit.day;
        s->hugs_today = 0;
    }
}

// ============================================================================
// 6. Interactions
// ============================================================================

// Any feed, hug or play while the pet is asleep or freshly woken: +0.25 tic and
// no buff. The first plays the wake animation, the rest just keep it up.
static void _pet_disturb(pet_state_t *s) {
    _pet_add_qt(s, PET_DEBUFF_DISTURB);
    _pet_flash_buff(s, false);
    s->night_awake_ticks = PET_NIGHT_AWAKE_SECONDS * PET_ANIM_HZ;
    bool was_asleep = s->scene == PET_SCENE_ASLEEP;
    // The scene moves first even when it is already here: _pet_cue_audible
    // reads it to keep PET_ANIM_WAKE's yawn off a waking nobody asked for, and
    // the animation starts below. _pet_blocked only calls this from the two
    // night scenes, so nothing else can be overwritten.
    s->scene = PET_SCENE_NIGHT_AWAKE;
    _pet_play_sound(s, PET_SOUND_GRUMBLE);
    if (was_asleep) {
        _pet_start_anim(s, PET_ANIM_WAKE);   // then rests into the mood, scene kept
    } else {
        _pet_draw(s);
    }
}

static void _pet_fall_asleep(pet_state_t *s) {
    s->scene = PET_SCENE_ASLEEP;
    // Start on a voiced breath.
    s->breath = 0;
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

// What every barf leaves behind, whatever brought it on: the minus sign, the
// animation, and a puddle to sweep. The tic arithmetic differs and stays with
// the caller.
static void _pet_barf_scene(pet_state_t *s) {
    _pet_flash_buff(s, false);
    // _pet_set_status runs when the scene rests, so the puddle appears as the
    // animation finishes rather than before it has started.
    s->has_barf = true;
    _pet_start_anim(s, PET_ANIM_BARF);
}

// Overfed. The pip comes straight back up and takes the sitting's nutrition
// with it: everything the kept-down pips were worth is handed back, plus the
// same penalty a play barf charges, so throwing up costs a little more than
// never having eaten at all. The plate is cleared -- the meal is over.
//
// pips_this_seg stays at the cap rather than resetting, so the pet is done
// eating until the next sitting. Feeding it again there is allowed and barfs
// again, but by then seg_buff_qt is zero and it costs only the penalty.
static void _pet_feed_barf(pet_state_t *s) {
    _pet_add_qt(s, (int16_t) (s->seg_buff_qt + PET_DEBUFF_BARF));
    s->seg_buff_qt = 0;
    s->food_queue = 0;
    s->feed_ticks = 0;
    _pet_barf_scene(s);
}

// Feed: each press queues a pip, up to PET_FOOD_MAX. Eating starts
// PET_FEED_SETTLE_SECONDS after the last press, one pip per
// PET_FEED_PIP_SECONDS; a press during eating restarts the wait.
static void _pet_feed_press(pet_state_t *s) {
    if (_pet_blocked(s)) return;
    if (s->food_queue < PET_FOOD_MAX) s->food_queue++;
    s->feed_ticks = PET_FEED_SETTLE_SECONDS * PET_ANIM_HZ;
    s->scene = PET_SCENE_FEEDING;
    s->bell_ticks = PET_BELL_TICKS;
    // The eating is seconds away yet, so the press answers for itself.
    _pet_play_sound(s, PET_SOUND_FEED);
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
    watch_date_time_t local = movement_get_local_date_time();
    uint8_t seg = _pet_meal_segment(local.unit.hour);
    // A new sitting starts the count and the nutrition over. Checked here rather
    // than in the catch-up so a sitting that turns over while the face is open
    // is noticed too.
    if (s->fed_day != local.unit.day || s->fed_seg != seg) {
        s->fed_day = local.unit.day;
        s->fed_seg = seg;
        s->pips_this_seg = 0;
        s->seg_buff_qt = 0;
    }
    s->food_queue--;
    // The pip was eaten either way, so the missed-day clock restarts either way.
    s->last_fed_ts = now;

    if (s->pips_this_seg >= PET_FEED_SEGMENT_CAP) {
        _pet_feed_barf(s);
        return;
    }

    s->pips_this_seg++;
    s->seg_buff_qt = (uint8_t) (s->seg_buff_qt + PET_BUFF_EAT);
    _pet_add_qt(s, -PET_BUFF_EAT);
    if (!s->has_poo && s->poo_due_ts == 0) {
        s->poo_due_ts = now + PET_POO_DELAY_SECONDS;
    }
    _pet_flash_buff(s, true);
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
    // Past the cap the pet is still kissed, it just earns no buff.
    _pet_start_anim(s, PET_ANIM_KISS);
}

// Clear everything on the floor, pile and puddle together. Works at night
// without waking the pet, and leaves a poo still on its way alone.
static void _pet_sweep(pet_state_t *s) {
    if (s->scene == PET_SCENE_DEAD) return;
    bool had_something = s->has_poo || s->has_barf;
    s->has_poo = false;
    s->has_barf = false;
    s->poo_residual = 0;
    s->poo_pending = false;
    _pet_set_status(s);
    // The brush is the sweeper's, not the pet's, so it sounds at night too --
    // the pet is not what made the noise. On a clean floor the button really has
    // done nothing, and stays silent to say so.
    if (had_something) _pet_play_sound(s, PET_SOUND_SWEEP);
    // Acknowledge by restarting the mood from frame 0 — not while asleep, which
    // would cut off the snore.
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
    // PET_ANIM_RESURRECT carries its own rise; without this the settle that
    // follows would add a mood step on top of it.
    s->shown_mood = (uint8_t) PET_MOOD_HAPPY;
    s->last_update_ts = now;
    s->last_fed_ts = now;
    s->awake_residual = 0;
    // The floor does not survive death.
    s->has_poo = false;
    s->has_barf = false;
    s->poo_due_ts = 0;
    s->poo_residual = 0;
    s->poo_pending = false;
    // Hugs, the play cooldown and the sitting reset too -- a pet that died
    // having been overfed would otherwise come back unable to eat.
    s->hugs_today = 0;
    s->last_play_buff_ts = 0;
    s->pips_this_seg = 0;
    s->seg_buff_qt = 0;
    s->scene = PET_SCENE_IDLE;
    _pet_start_anim(s, PET_ANIM_RESURRECT);   // then rests into the mood
}

// Play: each shake climbs one rung of the ladder documented next to
// PET_PLAY_DEAF_SECONDS. Both halves of the pause live in one countdown — above
// PET_PLAY_OPEN_TICKS the pet is deaf, below it a shake is heard — and it does
// not start until the flourish is off screen, see _pet_play_tick.
#define PET_PLAY_WAIT_TICKS ((PET_PLAY_DEAF_SECONDS + PET_PLAY_WINDOW_SECONDS) * PET_ANIM_HZ)
#define PET_PLAY_OPEN_TICKS (PET_PLAY_WINDOW_SECONDS * PET_ANIM_HZ)
#define PET_PLAY_DEAF_TICKS (PET_PLAY_DEAF_SECONDS * PET_ANIM_HZ)

static void _pet_barf(pet_state_t *s) {
    // Return the buff this session granted, if any, and charge the barf on top.
    // The cooldown stays spent.
    _pet_add_qt(s, (s->play_buffed ? PET_BUFF_PLAY : 0) + PET_DEBUFF_BARF);
    // Stage 0 ends the ladder but keeps the scene, so _pet_play_tick serves out
    // one more deaf period before resting. Dropping straight to PET_SCENE_IDLE
    // let the rest of the same shake start a fresh session, which replaced the
    // barf animation with PLAY_SMALL.
    s->play_stage = 0;
    s->play_ticks = PET_PLAY_DEAF_TICKS;
    _pet_barf_scene(s);
}

static void _pet_on_motion(pet_state_t *s) {
    if (s->scene == PET_SCENE_PLAYING) {
        // Deaf while the pet settles, and from a barf until the scene ends.
        if (s->play_stage == 0 || s->play_ticks > PET_PLAY_OPEN_TICKS) return;
        s->play_stage++;
        if (s->play_stage >= PET_PLAY_STAGE_BARF) {
            _pet_barf(s);
        } else {
            // Shaken again inside the window: the next rung.
            s->play_ticks = PET_PLAY_WAIT_TICKS;
            _pet_start_anim(s, PET_ANIM_PLAY_BIG);
        }
        return;
    }
    if (_pet_blocked(s)) return;

    uint32_t now = _pet_now();
    s->scene = PET_SCENE_PLAYING;
    s->play_stage = 1;
    s->play_ticks = PET_PLAY_WAIT_TICKS;

    // The pet always plays along, but the buff only lands once per cooldown.
    s->play_buffed = (now - s->last_play_buff_ts) >= PET_PLAY_COOLDOWN_SECONDS;
    if (s->play_buffed) {
        s->last_play_buff_ts = now;
        _pet_add_qt(s, -PET_BUFF_PLAY);
        _pet_flash_buff(s, true);
    }
    _pet_start_anim(s, PET_ANIM_PLAY_SMALL);
}

static void _pet_play_tick(pet_state_t *s) {
    // Hold the countdown until the flourish has played out.
    if (_pet_anim_busy(s)) return;
    if (s->play_ticks > 0) {
        s->play_ticks--;
        return;
    }
    // Nobody shook again: the session ends on whichever rung it reached.
    s->scene = PET_SCENE_IDLE;
    s->play_stage = 0;
    _pet_rest(s);
}

// ============================================================================
// 7. Scene entry
// ============================================================================

// The face just came on screen: catch up on lost time, then pick the opening
// scene from the day part.
static void _pet_enter(pet_state_t *s) {
    _pet_catch_up(s);

    // Movement cleared the display on the way in.
    _pet_invalidate(s);

    s->queue_len = 0;
    s->food_queue = 0;
    s->play_stage = 0;
    s->play_buffed = false;
    s->buff_ticks = s->debuff_ticks = s->bell_ticks = s->signal_ticks = 0;
    _pet_layer_play(s, PET_LAYER_CHARACTER, PET_ANIM_NONE);
    _pet_layer_play(s, PET_LAYER_STATUS, PET_ANIM_NONE);
#if PET_SHOWCASE
    s->showcase_on = false;
    s->showcase_anim = PET_ANIM_NONE;   // a fresh visit starts the walk over
#endif
    // Put the floor up from the first frame, rather than waiting for _pet_rest.
    _pet_set_status(s);

    pet_mood_t mood = _pet_mood(s);
    // Whatever the catch-up just did to the mood happened while the face was
    // closed. You were not there for it, so the pet opens without a step.
    s->shown_mood = (uint8_t) mood;
    if (mood == PET_MOOD_DEAD) {
        // Hold the tombstone until resurrected. This path skips _pet_rest, so it
        // turns the accelerometer down itself -- and tolls for itself, once:
        // the scene survives the visit, so coming back to the same tombstone is
        // quiet.
        if (s->scene != PET_SCENE_DEAD) _pet_play_sound(s, PET_SOUND_DEATH);
        s->scene = PET_SCENE_DEAD;
        _pet_set_tap_detection(s, false);
        _pet_start_anim(s, PET_ANIM_DEAD);
        return;
    }
    _pet_set_tap_detection(s, true);

    watch_date_time_t local = movement_get_local_date_time();
    switch (_pet_daypart(local.unit.hour)) {
        case PET_DAYPART_MORNING:
            // Wake on the first visit of the morning, then the mood.
            s->scene = PET_SCENE_IDLE;
            if (s->woke_day != local.unit.day) {
                s->woke_day = local.unit.day;
                _pet_queue_anim(s, PET_ANIM_WAKE);
            }
            _pet_queue_anim(s, _pet_mood_anim(mood));
            break;
        case PET_DAYPART_AFTERNOON:
            // Straight to the mood.
            s->scene = PET_SCENE_IDLE;
            break;
        case PET_DAYPART_NIGHT:
            s->scene = PET_SCENE_ASLEEP;
            s->breath = 0;          // start on a voiced breath
            break;
    }
    if (!_pet_start_next_queued(s)) _pet_rest(s);
}

// While the face stays open across a day-part boundary: nod off at
// PET_HOUR_SLEEP, wake at PET_HOUR_WAKE.
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
            // Nothing to do: the sleep animation loops and cues its own breaths.
            break;
        default:
            break;
    }
    if (subsecond == 0) {
        _pet_check_daypart(s);
        // Once a second is enough for a twelve-hour countdown.
        _pet_poo_arrives(s, _pet_now());
    }
#if PET_SHOWCASE
    // The showcase owns the screen while it is up.
    bool free_to_play = !s->showcase_on;
#else
    const bool free_to_play = true;
#endif
    // A poo that landed unwatched gets its scene once the pet is idle and free.
    // Asleep or dead there is no scene to play, so the pile just goes down.
    if (s->poo_pending && free_to_play) {
        if (s->scene == PET_SCENE_IDLE && !_pet_anim_busy(s)) {
            s->poo_pending = false;
            _pet_start_anim(s, PET_ANIM_POO);   // and _pet_rest leaves the pile
        } else if (s->scene == PET_SCENE_ASLEEP || s->scene == PET_SCENE_DEAD) {
            s->poo_pending = false;
            _pet_set_status(s);
            _pet_draw(s);
        }
    }
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
        // A fresh pet is born now, fed now.
        uint32_t now = _pet_now();
        s->last_update_ts = now;
        s->last_fed_ts = now;
    }
}

void pet_face_activate(void *context) {
    pet_state_t *s = (pet_state_t *) context;
    movement_request_tick_frequency(PET_ANIM_HZ);
    // A shake arrives as EVENT_SINGLE_TAP. Tap detection is switched on from
    // _pet_enter rather than here, once the catch-up has said whether the pet is
    // alive to shake.
    s->tap_enabled = false;
}

bool pet_face_loop(movement_event_t event, void *context) {
    pet_state_t *s = (pet_state_t *) context;

#if PET_SHOWCASE
    // Any real interaction drops out of the showcase.
    switch (event.event_type) {
        // Feed, sweep or shake: hand the screen back and forget the cursor, so
        // the next hold starts the walk over.
        case EVENT_LIGHT_BUTTON_UP:
        case EVENT_ALARM_BUTTON_UP:
        case EVENT_SINGLE_TAP:
        case EVENT_DOUBLE_TAP:
            _pet_showcase_exit(s, false);
            break;
        // The 0.5 s press arrives on the way to every 1.5 s hold: hand the
        // screen back, which the hug's kiss needs, but keep the cursor.
        case EVENT_LIGHT_LONG_PRESS:
        case EVENT_ALARM_LONG_PRESS:
            _pet_showcase_exit(s, true);
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
#if PET_SHOWCASE
            _pet_showcase_next(s);
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
#if PET_SHOWCASE
            _pet_showcase_step_mood(s);
#endif
            break;

        case EVENT_SINGLE_TAP:
        case EVENT_DOUBLE_TAP:
        case EVENT_ACCELEROMETER_WAKE:
            _pet_on_motion(s);
            break;

        case EVENT_TIMEOUT:
            // Back to the clock when Movement calls time. Resigning is what
            // drops the 8 Hz tick and turns tap detection back off.
            movement_move_to_face(0);
            break;
        case EVENT_LOW_ENERGY_UPDATE:
            // The shadow doesn't know what low-energy mode drew: push it all.
            _pet_invalidate(s);
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
    _pet_set_tap_detection(s, false);
}
