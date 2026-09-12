// Finds the LCD segments in Dan's animation export.
//
// A pixel that is dark in some frames and light in others is part of a segment
// that animates; a pixel that never changes is background or an unlit ghost.
// Connected runs of those toggling pixels are the individual segments.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 480
#define H 480
#define NPX (W * H)
#define DARK 128            // below this counts as lit

static unsigned char *fr;   // one frame
static unsigned char toggles[NPX];
static int label[NPX];
static int stack[NPX];

int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : "all.gray";
    int nframes = argc > 2 ? atoi(argv[2]) : 233;

    FILE *f = fopen(path, "rb");
    if (!f) { perror("open"); return 1; }
    fr = malloc(NPX);

    unsigned char *mn = malloc(NPX), *mx = malloc(NPX);
    memset(mn, 255, NPX); memset(mx, 0, NPX);
    int *lit_count = calloc(NPX, sizeof(int));

    for (int n = 0; n < nframes; n++) {
        if (fread(fr, 1, NPX, f) != NPX) { nframes = n; break; }
        for (int i = 0; i < NPX; i++) {
            if (fr[i] < mn[i]) mn[i] = fr[i];
            if (fr[i] > mx[i]) mx[i] = fr[i];
            if (fr[i] < DARK) lit_count[i]++;
        }
    }
    fclose(f);

    // A segment pixel: goes properly dark at some point.
    int n_toggle = 0, n_always = 0;
    for (int i = 0; i < NPX; i++) {
        toggles[i] = (mn[i] < DARK) ? 1 : 0;
        if (toggles[i]) {
            n_toggle++;
            if (lit_count[i] == nframes) n_always++;
        }
    }
    fprintf(stderr, "frames=%d  segment pixels=%d  (always lit=%d)\n",
            nframes, n_toggle, n_always);

    // Connected components, 8-connected, iterative flood fill.
    memset(label, 0, sizeof(label));
    int next = 0;
    typedef struct { int x0, y0, x1, y1, area, id; } box_t;
    box_t *boxes = calloc(4096, sizeof(box_t));

    for (int p = 0; p < NPX; p++) {
        if (!toggles[p] || label[p]) continue;
        next++;
        int sp = 0; stack[sp++] = p; label[p] = next;
        int x0 = p % W, x1 = x0, y0 = p / W, y1 = y0, area = 0;
        while (sp) {
            int q = stack[--sp]; area++;
            int qx = q % W, qy = q / W;
            if (qx < x0) x0 = qx; if (qx > x1) x1 = qx;
            if (qy < y0) y0 = qy; if (qy > y1) y1 = qy;
            for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) {
                int nx = qx + dx, ny = qy + dy;
                if (nx < 0 || ny < 0 || nx >= W || ny >= H) continue;
                int r = ny * W + nx;
                if (toggles[r] && !label[r]) { label[r] = next; stack[sp++] = r; }
            }
        }
        if (next < 4096) {
            boxes[next].x0 = x0; boxes[next].y0 = y0;
            boxes[next].x1 = x1; boxes[next].y1 = y1;
            boxes[next].area = area; boxes[next].id = next;
        }
    }

    // Report, discarding specks.
    printf("# id  x0   y0   x1   y1    w    h   area   cx   cy\n");
    int kept = 0;
    for (int i = 1; i <= next && i < 4096; i++) {
        if (boxes[i].area < 20) continue;
        kept++;
        printf("%4d %4d %4d %4d %4d %4d %4d %6d %4d %4d\n", boxes[i].id,
               boxes[i].x0, boxes[i].y0, boxes[i].x1, boxes[i].y1,
               boxes[i].x1 - boxes[i].x0 + 1, boxes[i].y1 - boxes[i].y0 + 1,
               boxes[i].area, (boxes[i].x0 + boxes[i].x1) / 2,
               (boxes[i].y0 + boxes[i].y1) / 2);
    }
    fprintf(stderr, "components=%d (kept %d with area>=20)\n", next, kept);
    return 0;
}
