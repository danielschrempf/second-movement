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

// What each layer is allowed to light. Anything a frame sets outside its own
// cells is masked off by the compositor, so one stray segment in the art can't
// invade a neighbour.
//
// Cells 0, 2 and 3 are barred to both layers: those are drawn straight from
// state -- the buff sign and the food pips -- and no animation may touch them.
//
// Cell 9 is shared rather than split. The status layer only ever wants its
// centre and bottom edge, but the character reaches across the whole of it:
// worn sideways the main line runs top to bottom, cell 9 is the far end, and
// the resurrect animation rises the spirit up from there through the whole
// line. Clipping that would cut the entrance in half. Sharing is safe
// because compositing is a straight OR -- neither layer can erase the other,
// and the two are never both drawing in cell 9 at once anyway, since the pet
// only resurrects when there is nothing on the floor.
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

// The food pips, lit in this order as the queue fills. Sideways these four
// segments of position 3 read as a 2x2 block: bottom right, bottom left, top
// right, top left.
static const uint8_t _pet_food_pips[PET_FOOD_MAX] = { SEG_B, SEG_C, SEG_F, SEG_E };

// The character animations, decoded from the drawn exports rather than written
// by hand: see _cs50ref/tools/decode.sh, which also checks that no frame lights
// half of a tied segment pair, and reports which cells the drawing touches.
//
// Frames are (segments per position, flags, hold), where hold is in ticks at
// PET_ANIM_HZ -- 8 is one second. Consecutive identical poses are collapsed
// into one held frame by the decoder, so these tables are shorter than the
// exports they came from.

// The resting mood. The mouth holds; the eyes -- the colon, one
// segment, so open and shut is all they do -- carry the blink.
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

// The brow drops and the mouth turns down. Upset and angry share a mouth --
// cell 6's top edge and both verticals, an open frown once rotated -- and
// differ in the brow above it, which gains its verticals as a scowl.
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

// A tombstone. Nothing moves, so one pose.
static const pet_frame_t _pet_frames_dead[] = {
    //  0         1         2         3         4         5         6         7         8         9            flags            hold
    { { SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_NONE , SEG_B|SEG_C|SEG_G, SEG_G    , SEG_A|SEG_D|SEG_E|SEG_F, SEG_A|SEG_D }, 0              ,  4 },
};
// 4 frames -> 1 poses

// The tombstone shrinks away and the spirit drifts back in from the
// right, growing into the pet. It sweeps the whole main line, cell 9 included.
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

// What stays on the floor once the poo animation has played out: the stem and
// its base, cell 9's centre and bottom edge. Sits there until swept, so one
// frame that loops. This is the status layer's whole vocabulary.
static const pet_frame_t _pet_frames_pile[] = {
    //  0  1  2  3  4  5  6  7  8  9                                flags  hold
    { { 0, 0, 0, 0, 0, 0, 0, 0, 0, SEG_G | SEG_B | SEG_C },            0,  PET_ANIM_HZ },
};

// Where each sound belongs, written as the moment rather than the timing. A cue
// with no mask fires as the animation begins; otherwise it fires on the frame
// where those segments light (or, with on_clear, go dark).
//
// Every condition here is checked against the art by tools/check_sounds.py --
// a cue whose moment never arrives is as silent a failure as one that drifted.

// Breathe in as the loop starts, out as the puff appears beside the mouth.
static const pet_cue_t _pet_cues_snore[] = {
    { PET_SOUND_SNORE_IN,  0, 0,     false, true  },
    { PET_SOUND_SNORE_OUT, 1, SEG_D, false, true  },
};

// The pucker is the first frame to light cell 6's centre.
static const pet_cue_t _pet_cues_kiss[] = {
    { PET_SOUND_KISS, 6, SEG_G, false, true },
};

// The pip sits in cell 7 until it is swallowed, and the jaw works in cell 6 --
// the chew is the one cue that deliberately repeats, so it follows the drawing
// however many times the jaw is animated.
static const pet_cue_t _pet_cues_eat[] = {
    { PET_SOUND_EAT_GULP, 7, 0xFF,  true,  true  },
    { PET_SOUND_EAT_CHEW, 6, SEG_G, false, false },
};

// Uh oh as it starts; the slide once the contents reach the minutes-ones cell.
// The slide fires once: cell 7 flickers as the contents tumble through it, and
// without that the ramp would restart partway down and cut itself off.
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

static const pet_anim_t _pet_anims[PET_ANIM_COUNT] = {
    //                          label     frames and count                    loop   layer                cues and count
    [PET_ANIM_NONE]       = { "      ",  PET_NO_FRAMES,                       false, PET_LAYER_CHARACTER, PET_NO_CUES },
    // moods: loop while the pet rests
    [PET_ANIM_HAPPY]      = { "HAPPY ",  PET_FRAMES(_pet_frames_happy),       true,  PET_LAYER_CHARACTER, PET_NO_CUES },
    [PET_ANIM_CONFUSED]   = { "CONFUS",  PET_FRAMES(_pet_frames_confused),    true,  PET_LAYER_CHARACTER, PET_NO_CUES },
    [PET_ANIM_UPSET]      = { "UPSET ",  PET_FRAMES(_pet_frames_upset),       true,  PET_LAYER_CHARACTER, PET_NO_CUES },
    [PET_ANIM_ANGRY]      = { "ANGRY ",  PET_FRAMES(_pet_frames_angry),       true,  PET_LAYER_CHARACTER, PET_NO_CUES },
    [PET_ANIM_DEAD]       = { "DEAD  ",  PET_FRAMES(_pet_frames_dead),        true,  PET_LAYER_CHARACTER, PET_NO_CUES },
    // one-shots
    [PET_ANIM_RESURRECT]  = { "GHOST ",  PET_FRAMES(_pet_frames_resurrect),   false, PET_LAYER_CHARACTER, PET_NO_CUES },
    [PET_ANIM_PLAY_SMALL] = { "PLAY 1",  PET_FRAMES(_pet_frames_play_small),  false, PET_LAYER_CHARACTER, PET_CUES(_pet_cues_play_small) },
    [PET_ANIM_PLAY_BIG]   = { "PLAY 2",  PET_FRAMES(_pet_frames_play_big),    false, PET_LAYER_CHARACTER, PET_CUES(_pet_cues_play_big) },
    [PET_ANIM_EAT]        = { "EAT   ",  PET_FRAMES(_pet_frames_eat),         false, PET_LAYER_CHARACTER, PET_CUES(_pet_cues_eat) },
    [PET_ANIM_KISS]       = { "KISS  ",  PET_FRAMES(_pet_frames_kiss),        false, PET_LAYER_CHARACTER, PET_CUES(_pet_cues_kiss) },
    [PET_ANIM_SNORE]      = { "SNORE ",  PET_FRAMES(_pet_frames_snore),       true,  PET_LAYER_CHARACTER, PET_CUES(_pet_cues_snore) },
    [PET_ANIM_WAKE]       = { "WAKE  ",  PET_FRAMES(_pet_frames_wake),        false, PET_LAYER_CHARACTER, PET_NO_CUES },
    // Scenes: the pet does something and leaves cell 9 changed. Drawn as whole
    // scenes rather than as a detached blob, so they play on the character
    // layer; the status layer keeps whatever is left on the floor afterwards.
    [PET_ANIM_POO]        = { "POO   ",  PET_FRAMES(_pet_frames_poo),         false, PET_LAYER_CHARACTER, PET_CUES(_pet_cues_poo) },
    [PET_ANIM_BARF]       = { "BARF  ",  PET_FRAMES(_pet_frames_barf),        false, PET_LAYER_CHARACTER, PET_CUES(_pet_cues_barf) },
    // the status layer: what stays on the floor
    [PET_ANIM_PILE]       = { "PILE  ",  PET_FRAMES(_pet_frames_pile),        true,  PET_LAYER_STATUS,    PET_NO_CUES },
};


// The sounds. Format is note, duration, ..., 0, with durations in 1/64 s; a
// negative value rewinds that many notes and the value after it is the repeat
// count. Notes are in watch_tcc.h.
//
// These are phrases, not timelines: none of them waits for anything. Where a
// sound belongs to a particular moment of an animation, the waiting is the
// cue's job -- see _pet_cues_* below -- so nothing here needs re-timing when
// art is redrawn.
//
// Everything also flashes SIGNAL, so on a silent watch the timing still reads.

static int8_t _pet_sound_snore_in[]  = { BUZZER_NOTE_C6, 32, 0 };
static int8_t _pet_sound_snore_out[] = { BUZZER_NOTE_C5, 32, 0 };

// Chromatic, up and quick, riding the kiss out of the pucker.
static int8_t _pet_sound_kiss[] = {
    BUZZER_NOTE_C6,              3,
    BUZZER_NOTE_C6SHARP_D6FLAT,  3,
    BUZZER_NOTE_D6,              3,
    BUZZER_NOTE_D6SHARP_E6FLAT,  3,
    BUZZER_NOTE_E6,              3,
    BUZZER_NOTE_F6,              4,
    0
};

static int8_t _pet_sound_eat_gulp[] = { BUZZER_NOTE_E6, 6, 0 };
static int8_t _pet_sound_eat_chew[] = { BUZZER_NOTE_G5, 6, 0 };

// Two falling notes over the wriggling mouth: the pet knows what is coming.
static int8_t _pet_sound_barf_uhoh[] = {
    BUZZER_NOTE_A5, 24,
    BUZZER_NOTE_F5, 24,
    0
};

// ... and then a chromatic octave down, travelling with it.
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

// One short low knock, nothing more.
static int8_t _pet_sound_poo[] = { BUZZER_NOTE_C4, 5, 0 };

// Playing is a reward. Big is the same shape a whole tone up.
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
};

// The one funnel every sound passes through, so SIGNAL is flashed here too —
// the watch is often kept silent, and the pet would otherwise lose half its
// personality.
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
// Decay is charged in waking seconds, not wall seconds, so catching up on time
// spent off-screen means counting the waking seconds at each end of the gap and
// subtracting.

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
// 3. Compositor
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

// Composite every layer into one framebuffer and push it to the LCD. Each
// layer contributes only the segments it owns, so they can be timed and changed
// independently; stray bits in a frame are masked off rather than trusted.
static void _pet_draw(const pet_state_t *s) {
    uint8_t fb[10] = { 0 };
    uint8_t flags = 0;
    const char *label = NULL;

    for (uint8_t l = 0; l < PET_LAYER_COUNT; l++) {
        const pet_anim_t *a = &_pet_anims[s->layer[l].anim];
        if (s->layer[l].anim == PET_ANIM_NONE) continue;
        if (a->frames == NULL) {
            // Every animation has art now, so nothing reaches this. It stays as
            // the fallback for a row added to _pet_anims before its frames are
            // drawn: the character's name stands in below rather than the face
            // going blank, and any other layer simply draws nothing.
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

    // The fallback above: the animation's name in positions 4-8. Cell 9 is left
    // alone so the pile still shows beside it.
    if (label) {
        for (uint8_t i = 0; i < 5 && label[i]; i++) watch_display_character(label[i], 4 + i);
    }
}

// ============================================================================
// 4. Layer engine
// ============================================================================
//
// Each layer plays its own animation on its own clock. The character layer also
// has a short queue, so a scene can say "wake, then the mood" and have them run
// back to back; when it empties the pet rests (the spec's "blink"). Other layers
// settle to their idle animation instead.

static void _pet_rest(pet_state_t *s);

static uint8_t _pet_frame_hold(const pet_anim_t *a, uint8_t frame) {
    // Label-only animations show for one second.
    return a->frames ? a->frames[frame].hold : PET_ANIM_HZ;
}

// True while a one-shot is still on screen; the looping animations are the
// resting state and never count as busy. Scene timers wait on this rather than
// cutting an animation short — an eat animation longer than
// PET_FEED_PIP_SECONDS would otherwise be truncated by the next pip.
static bool _pet_anim_busy(const pet_state_t *s) {
    uint8_t id = s->layer[PET_LAYER_CHARACTER].anim;
    return id != PET_ANIM_NONE && !_pet_anims[id].loop;
}

// Some cues are rate-limited by the scene rather than by the art. The pet
// breathes on every turn of the sleep loop, but snoring on every one of them
// never lets up; voicing two breaths in three is what gives it a rhythm.
static bool _pet_cue_audible(const pet_state_t *s, pet_sound_id_t id) {
    if (id == PET_SOUND_SNORE_IN || id == PET_SOUND_SNORE_OUT) {
        return s->breath < PET_SNORE_AUDIBLE_BREATHS;
    }
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
            // A condition can come true more than once in a pass -- a segment
            // flickers as something moves through its cell -- and restarting a
            // long sound partway would cut it off.
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
    // Nothing was on screen a moment ago as far as this animation is concerned,
    // so a condition already true in frame 0 counts as having just come true.
    _pet_fire_cues(s, L, a, _pet_no_segments, _pet_frame_segs(a, 0), true);
}

// Play an animation on whichever layer owns it, per the animation table, so
// callers never route by hand.
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

// The status layer is a direct read of what's on the floor: the pile is there
// or it isn't. The scenes that put it there play on the character layer, so
// nothing here can interrupt them.
static void _pet_set_status(pet_state_t *s) {
    pet_anim_id_t want = (s->has_poo && _pet_mood(s) != PET_MOOD_DEAD)
                       ? PET_ANIM_PILE : PET_ANIM_NONE;
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

#if PET_DEBUG_CONTROLS
    // Hold whatever is being previewed on screen, one-shots included, so it can
    // be looked at for as long as it takes rather than flashing past once.
    if (s->debug_preview) { loop_here = true; queued = false; }
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

// A plus or a minus in position 0, whenever the pet gains or loses ground.
static void _pet_flash_buff(pet_state_t *s, bool gained) {
    s->buff_ticks   = gained ? PET_FLASH_TICKS : 0;
    s->debuff_ticks = gained ? 0 : PET_FLASH_TICKS;
}

// The spec's "blink": settle into the looping mood animation. Scenes with their
// own timer (feeding, playing, awake at night) keep their scene; anything else
// becomes idle.
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
// Most animations only appear when the clock says so — Angry wants most of a
// day of neglect, dead a day and a half, snoring wants it to be 21:00. These
// walk the list on demand. Button map is next to PET_DEBUG_CONTROLS.

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

// Persistence: deliberately not implemented.
//
// The state is a malloc'd struct, so it survives face switches and — the part
// that matters — Movement's low-energy mode, which calls
// watch_enter_sleep_mode() and leaves RAM alone. Only BACKUP mode would wipe
// it, and watch_enter_backup_mode() is never called anywhere in this firmware.
// So in daily wear the pet persists indefinitely; only a battery pull, a
// reflash or a crash hatches a new one, and reaching the reset button means
// opening the case.
//
// If that changes, the two routes are RTC backup registers 2-6 (claim with
// movement_claim_backup_register(); survives reset and reflash but not a
// battery pull, and 160 bits means the timestamps need packing), or a littlefs
// file in the RWWEE area, which survives everything at the cost of flash wear.
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
    // Same for the play cooldown, except zero is the forgiving end: clear it so
    // the pet is playable now rather than two hours into a clock that jumped.
    if (now < s->last_play_buff_ts) s->last_play_buff_ts = 0;

    if (s->quarter_tics < PET_QT_DEAD) {
        // Passive: +1 tic per 6 h of waking time. Leftover seconds carry in
        // awake_residual; rounding them away each visit would let a diligent
        // owner outrun decay just by opening the face often.
        uint32_t awake = _pet_awake_between(s->last_update_ts, now) + s->awake_residual;
        uint32_t qt = awake / PET_SECONDS_PER_QT;
        s->awake_residual = (uint16_t) (awake % PET_SECONDS_PER_QT);
        s->last_update_ts = now;
        if (qt > 0) {
            if (qt > PET_QT_DEAD) qt = PET_QT_DEAD;
            _pet_add_qt(s, (int16_t) qt);
        }

        // "1 tic every missed day" without eating, stacking on passive decay so
        // a neglected day costs 5 tics. Wall-clock days, not waking hours — a
        // missed day is a missed day.
        uint32_t days = (now - s->last_fed_ts) / PET_MISSED_FEED_SECONDS;
        if (days > 0) {
            s->last_fed_ts += days * PET_MISSED_FEED_SECONDS;
            if (days > 6) days = 6;
            _pet_add_qt(s, (int16_t) PET_TIC(days));
        }

        // A poo on its way has arrived; max one at a time. This countdown is
        // wall clock — digestion doesn't stop overnight, only the penalty for
        // leaving the result lying there.
        if (!s->has_poo && s->poo_due_ts != 0 && now >= s->poo_due_ts) {
            // Only worth animating if it is happening now. Catching up after a
            // day away lands a poo that arrived hours ago, and playing the pet
            // squatting over it then would be a small lie; the pile just shows.
            s->poo_pending = (now - s->poo_due_ts) < PET_POO_FRESH_SECONDS;
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

    // The hug cap resets with the calendar day. Per visit would be trivially
    // gamed by switching face and coming straight back for four more.
    if (s->hug_day != local.unit.day) {
        s->hug_day = local.unit.day;
        s->hugs_today = 0;
    }
}

// ============================================================================
// 6. Interactions
// ============================================================================

// Any feed, hug or play while the pet is asleep or freshly woken: +0.25 tic and
// no buff. The first plays the wake animation; the rest just keep it up.
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
    // Start on a voiced breath, so every route into sleep sounds the same.
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

// Feed(): each press queues a pip (max 4). Eating starts 3 s after the last
// press, one pip per second; a press during eating restarts the 3 s wait.
static void _pet_feed_press(pet_state_t *s) {
    if (_pet_blocked(s)) return;
    if (s->food_queue < PET_FOOD_MAX) s->food_queue++;
    s->feed_ticks = PET_FEED_SETTLE_SECONDS * PET_ANIM_HZ;
    s->scene = PET_SCENE_FEEDING;
    // Pips composite straight from food_queue, so the queue is countable.
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
    // Past the cap the pet is still kissed, it just doesn't help: a button that
    // silently does nothing reads as broken, and the absent plus sign is the
    // tell that the cap is spent.
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
    s->poo_pending = false;
    _pet_set_status(s);
    // No sweep animation in the checklist, so the acknowledgement is the mood
    // restarting from frame 0. Not while asleep: that would cut off the snore.
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
    // The poo does not survive death; resurrection is a clean slate.
    s->has_poo = false;
    s->poo_due_ts = 0;
    s->poo_residual = 0;
    s->poo_pending = false;
    // Hugs and the play cooldown reset too: coming back on the same
    // day-of-month you last hugged would otherwise find the cap already spent.
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
    if (s->play_ticks > 0) {
        s->play_ticks--;
        return;
    }
    s->scene = PET_SCENE_IDLE;
    if (s->nausea > PET_NAUSEA_LIMIT) {
        // Barfing returns the buff and costs more on top, so over-shaking is
        // worse than not playing. Only a buff actually granted is returned; the
        // cooldown stays spent, since a pet just made sick won't go again.
        _pet_add_qt(s, (s->play_buffed ? PET_BUFF_PLAY : 0) + PET_DEBUFF_BARF);
        _pet_flash_buff(s, false);
        _pet_start_anim(s, PET_ANIM_BARF);
    } else if (s->nausea > 0) {
        // Nothing in the spec says what triggers PLAY_BIG, so it's a session
        // with some shaking that stayed under the nausea limit: the reward for
        // playing enthusiastically but not madly.
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
            // Wake on the first visit of the morning, then the mood. The spec
            // sequences the poo here too, but that predates the layers — it has
            // its own cell now, and queueing it would stall the character layer
            // since the queue only feeds that one.
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

// While the face stays open across a day-part boundary: nod off at 21:00, wake
// at 05:00. The spec only describes entry, but watching the pet go to bed beats
// seeing it frozen awake.
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
            // Nothing to do: the sleep animation loops, and its cues voice two
            // breaths in every three. See _pet_cue_audible.
            break;
        default:
            break;
    }
    if (subsecond == 0) _pet_check_daypart(s);
    // A poo that has just landed gets its scene, once the pet is free to play
    // it: never mid-interaction, and never at night, where it would talk over
    // the snore. Either way the pile is already on the status layer.
    if (s->poo_pending) {
        if (s->scene == PET_SCENE_IDLE && !_pet_anim_busy(s)) {
            s->poo_pending = false;
            _pet_start_anim(s, PET_ANIM_POO);
        } else if (s->scene == PET_SCENE_ASLEEP || s->scene == PET_SCENE_DEAD) {
            s->poo_pending = false;
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
    // Shake -> EVENT_SINGLE_TAP / EVENT_DOUBLE_TAP, and only while this face is
    // on screen, since tap detection runs the accelerometer at 400 Hz.
    // EVENT_ACCELEROMETER_WAKE is never delivered (its callback is commented out
    // in movement.c), so tap events are the only motion source.
    s->tap_enabled = movement_enable_tap_detection_if_available(true);
}

bool pet_face_loop(movement_event_t event, void *context) {
    pet_state_t *s = (pet_state_t *) context;

#if PET_DEBUG_CONTROLS
    // Any real interaction drops out of preview, so there's nothing to remember
    // about escaping it.
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
            // Back to the clock when Movement calls time (the deadline is a
            // user setting: 60, 120, 300 or 1800 s). Staying would keep the
            // 8 Hz tick and the 400 Hz accelerometer running — and resigning is
            // what turns tap detection off, so this is also what stops the pet
            // being shaken awake all night by an arm rolling over in bed.
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
