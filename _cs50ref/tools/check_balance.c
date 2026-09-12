// Simulates the settled pet_face rules over a week for different care routines.
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#define H(x) ((uint32_t)((x) * 3600))
#define PET_HOUR_WAKE 5
#define PET_HOUR_SLEEP 21
#define AWAKE_PER_DAY ((PET_HOUR_SLEEP - PET_HOUR_WAKE) * 3600)
#define QT_DEAD 24
#define SEC_PER_QT 5400
#define POO_DELAY H(12)
#define MISSED_FEED H(24)
#define PLAY_COOLDOWN H(2)
#define HUG_CAP 4

static uint32_t awake_f(uint32_t t) {
    uint32_t d = t / 86400, r = t % 86400, p;
    if (r < H(PET_HOUR_WAKE)) p = 0;
    else if (r < H(PET_HOUR_SLEEP)) p = r - H(PET_HOUR_WAKE);
    else p = AWAKE_PER_DAY;
    return d * (uint32_t)AWAKE_PER_DAY + p;
}
static uint32_t awake_between(uint32_t a, uint32_t b) {
    return b <= a ? 0 : awake_f(b) - awake_f(a);
}

typedef struct {
    int qt; bool has_poo;
    uint32_t last, fed, poo_due, poo_since, last_play;
    uint16_t resid, poo_resid;
    int hugs_today; int hug_day;
} pet_t;

static void add(pet_t *p, int d) { p->qt += d; if (p->qt < 0) p->qt = 0; if (p->qt > QT_DEAD) p->qt = QT_DEAD; }

static void catch_up(pet_t *p, uint32_t now) {
    if (p->qt >= QT_DEAD) return;
    uint32_t a = awake_between(p->last, now) + p->resid;
    add(p, a / SEC_PER_QT); p->resid = a % SEC_PER_QT; p->last = now;
    uint32_t days = (now - p->fed) / MISSED_FEED;
    if (days) { p->fed += days * MISSED_FEED; add(p, 4 * days); }
    if (!p->has_poo && p->poo_due && now >= p->poo_due) {
        p->has_poo = true; p->poo_since = p->poo_due; p->poo_resid = 0; p->poo_due = 0;
    }
    if (p->has_poo) {
        uint32_t pa = awake_between(p->poo_since, now) + p->poo_resid;
        add(p, pa / SEC_PER_QT); p->poo_resid = pa % SEC_PER_QT; p->poo_since = now;
    }
    int day = now / 86400;
    if (p->hug_day != day) { p->hug_day = day; p->hugs_today = 0; }
}

// One check-in: sweep, hug to the cap, optionally feed, play if off cooldown.
static void visit(pet_t *p, uint32_t now, bool feed) {
    catch_up(p, now);
    if (p->qt >= QT_DEAD) return;
    p->has_poo = false; p->poo_due = 0; p->poo_resid = 0;          // sweep
    while (p->hugs_today < HUG_CAP) { p->hugs_today++; add(p, -1); } // hug
    if (feed) { for (int i = 0; i < 4; i++) { add(p, -1); p->fed = now; if (!p->poo_due) p->poo_due = now + POO_DELAY; } }
    if ((now - p->last_play) >= PLAY_COOLDOWN) { p->last_play = now; add(p, -2); } // play
}

static void run(const char *name, const int *hours, int n_hours, int feed_at) {
    pet_t p = {0};
    p.last = p.fed = H(7); p.hug_day = 0; p.last_play = 0;
    printf("  %-34s", name);
    for (int day = 0; day < 7; day++) {
        for (int i = 0; i < n_hours; i++) {
            visit(&p, day * 86400 + H(hours[i]), feed_at < 0 ? true : (i == feed_at));
        }
        catch_up(&p, day * 86400 + H(23));
        printf(" %2d", p.qt);
    }
    printf("   %s\n", p.qt >= QT_DEAD ? "DEAD" : (p.qt >= 12 ? "struggling" : "healthy"));
}

int main(void) {
    int three[] = {8, 13, 19};
    int two[]   = {8, 19};
    int one[]   = {19};
    puts("quarter tics at end of each day (0 = blissful, 24 = dead). Death at 24.\n");
    puts("                                    d1 d2 d3 d4 d5 d6 d7");
    run("3/day, feed at 08:00 (morning)", three, 3, 0);
    run("3/day, feed at 13:00 (midday)",  three, 3, 1);
    run("3/day, feed at 19:00 (evening)", three, 3, 2);
    run("2/day (08:00,19:00), feed morning", two, 2, 0);
    run("2/day (08:00,19:00), feed evening", two, 2, 1);
    run("1/day (evening only)",           one,   1, 0);

    // Neglect: no visits at all.
    pet_t p = {0}; p.last = p.fed = H(7); p.hug_day = 0;
    printf("  %-34s", "no care at all");
    for (int day = 0; day < 7; day++) { catch_up(&p, day * 86400 + H(23)); printf(" %2d", p.qt); }
    printf("   %s\n", p.qt >= QT_DEAD ? "DEAD" : "alive");
    return 0;
}
