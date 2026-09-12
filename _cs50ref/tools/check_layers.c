// Checks the layer ownership invariant: no two layers may claim the same
// segment, or one layer's art silently lights another's cell. Masks copied
// verbatim from pet_face.c.
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#define SEG_A (1<<0)
#define SEG_B (1<<1)
#define SEG_C (1<<2)
#define SEG_D (1<<3)
#define SEG_E (1<<4)
#define SEG_F (1<<5)
#define SEG_G (1<<6)
#define SEG_H (1<<7)
#define PET_FRAME_COLON  (1<<0)
#define PET_FRAME_SIGNAL (1<<1)
#define PET_FRAME_BELL   (1<<2)
#define PET_BUFF_POSITION 0
#define PET_FOOD_POSITION 3
#define PET_FOOD_MAX 4

typedef struct { uint8_t seg[10]; uint8_t flags; } pet_layer_def_t;
enum { PET_LAYER_CHARACTER = 0, PET_LAYER_STATUS, PET_LAYER_COUNT };

static const pet_layer_def_t _pet_layers[PET_LAYER_COUNT] = {
    [PET_LAYER_CHARACTER] = {
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
static const uint8_t _pet_food_pips[PET_FOOD_MAX] = { SEG_B, SEG_C, SEG_F, SEG_E };

static int fails = 0;
static void ok(const char *what, bool cond) {
    if (!cond) fails++;
    printf("  %-56s %s\n", what, cond ? "ok" : "FAIL");
}

int main(void) {
    const char *names[] = { "character", "status" };
    puts("layer ownership is disjoint\n");

    // Every animated layer pair, every position.
    for (int a = 0; a < PET_LAYER_COUNT; a++)
        for (int b = a + 1; b < PET_LAYER_COUNT; b++) {
            bool clean = true;
            for (int p = 0; p < 10; p++)
                if (_pet_layers[a].seg[p] & _pet_layers[b].seg[p]) clean = false;
            if (_pet_layers[a].flags & _pet_layers[b].flags) clean = false;
            char buf[80];
            snprintf(buf, sizeof buf, "%s vs %s share no segment", names[a], names[b]);
            ok(buf, clean);
        }

    // The two non-animated regions must not collide with any layer.
    uint8_t food_mask = 0;
    for (int i = 0; i < PET_FOOD_MAX; i++) food_mask |= _pet_food_pips[i];
    uint8_t buff_mask = SEG_G | SEG_H;   // plus; minus (H) is a subset
    for (int l = 0; l < PET_LAYER_COUNT; l++) {
        char buf[80];
        snprintf(buf, sizeof buf, "food pips (pos 3) clear of %s", names[l]);
        ok(buf, !(_pet_layers[l].seg[PET_FOOD_POSITION] & food_mask));
        snprintf(buf, sizeof buf, "buff sign (pos 0) clear of %s", names[l]);
        ok(buf, !(_pet_layers[l].seg[PET_BUFF_POSITION] & buff_mask));
    }

    // Position 9 is the shared cell: together the two layers must cover all
    // seven of its segments exactly once, with none left stranded.
    uint8_t nine = _pet_layers[PET_LAYER_CHARACTER].seg[9] | _pet_layers[PET_LAYER_STATUS].seg[9];
    ok("position 9 fully allocated (A-G, no H)", nine == 0x7F);
    ok("food pips are 4 distinct segments", __builtin_popcount(food_mask) == 4);

    // Position 2 was freed when the HUD was dropped; nothing should claim it.
    bool two_free = true;
    for (int l = 0; l < PET_LAYER_COUNT; l++) if (_pet_layers[l].seg[2]) two_free = false;
    ok("position 2 unclaimed (HUD removed)", two_free);

    // Masking actually contains a misbehaving frame.
    uint8_t rogue[10]; for (int p = 0; p < 10; p++) rogue[p] = 0xFF;
    uint8_t fb[10] = {0};
    for (int p = 0; p < 10; p++) fb[p] |= rogue[p] & _pet_layers[PET_LAYER_CHARACTER].seg[p];
    ok("a character frame with every bit set can't reach pos 0/2/3",
       fb[0] == 0 && fb[2] == 0 && fb[3] == 0);
    ok("...nor position 9's poo segments",
       !(fb[9] & (SEG_G | SEG_B | SEG_C)));

    printf("\n%s\n", fails ? "FAILURES ABOVE" : "all checks passed");
    return fails != 0;
}
