// Validates the waking-time accounting copied verbatim from pet_face.c.
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#define PET_HOUR_WAKE   6
#define PET_HOUR_SLEEP  21
#define PET_AWAKE_SECONDS_PER_DAY ((PET_HOUR_SLEEP - PET_HOUR_WAKE) * 60 * 60)
#define PET_QT_PER_TIC  4
#define PET_TIC(n) ((n) * PET_QT_PER_TIC)
#define PET_QT_DEAD PET_TIC(6)
#define PET_SECONDS_PER_TIC (6*60*60)
#define PET_SECONDS_PER_QT  (PET_SECONDS_PER_TIC / PET_QT_PER_TIC)
#define PET_MISSED_FEED_SECONDS (24*60*60)

static uint32_t _pet_awake_seconds(uint32_t local_ts) {
    uint32_t days = local_ts / 86400;
    uint32_t rem  = local_ts % 86400;
    uint32_t partial;
    if (rem < (uint32_t) PET_HOUR_WAKE * 3600) {
        partial = 0;
    } else if (rem < (uint32_t) PET_HOUR_SLEEP * 3600) {
        partial = rem - (uint32_t) PET_HOUR_WAKE * 3600;
    } else {
        partial = PET_AWAKE_SECONDS_PER_DAY;
    }
    return days * (uint32_t) PET_AWAKE_SECONDS_PER_DAY + partial;
}
static uint32_t awake_between(uint32_t a, uint32_t b) {
    if (b <= a) return 0;
    return _pet_awake_seconds(b) - _pet_awake_seconds(a);
}

static int fails = 0;
#define H(x) ((uint32_t)((x) * 3600))
static void check(const char *what, uint32_t got, uint32_t want) {
    bool ok = got == want;
    if (!ok) fails++;
    printf("  %-46s got %7u  want %7u  %s\n", what, got, want, ok ? "ok" : "FAIL");
}

int main(void) {
    uint32_t d0 = 0, d1 = 86400;
    puts("waking-seconds accounting (sleep 21:00-06:00, 15 h awake/day)");
    check("whole day, midnight to midnight",        awake_between(d0, d1), H(15));
    check("06:00 -> 21:00 (the whole waking day)",  awake_between(d0+H(6), d0+H(21)), H(15));
    check("21:00 -> 06:00 next day (all night)",    awake_between(d0+H(21), d1+H(6)), 0);
    check("20:00 -> 22:00 (only 20-21 counts)",     awake_between(d0+H(20), d0+H(22)), H(1));
    check("05:00 -> 07:00 (only 06-07 counts)",     awake_between(d0+H(5), d0+H(7)), H(1));
    check("23:00 -> 02:00, entirely at night",      awake_between(d0+H(23), d1+H(2)), 0);
    check("zero-length span",                       awake_between(d0+H(12), d0+H(12)), 0);
    check("backwards span clamps to 0",             awake_between(d1, d0), 0);
    check("a full week",                            awake_between(d0, d0 + 7*86400), 7*H(15));
    check("18:00 -> 08:00 next day (3 h + 2 h)",    awake_between(d0+H(18), d1+H(8)), H(5));

    puts("\nmonotonic + additive over 400 random-ish spans");
    uint32_t prev = 0; bool mono = true, additive = true;
    for (uint32_t t = 0; t < 400 * 997; t += 997) {
        uint32_t f = _pet_awake_seconds(t);
        if (f < prev) mono = false;
        prev = f;
        uint32_t mid = t + 3600;
        if (awake_between(t, mid) + awake_between(mid, t + 7200) != awake_between(t, t + 7200)) additive = false;
    }
    check("f(t) never decreases", mono, true);
    check("awake(a,c) == awake(a,b) + awake(b,c)", additive, true);

    puts("\nneglect: how long from full health to death, by start hour");
    for (int start = 0; start < 24; start += 6) {
        uint32_t t0 = d0 + H(start);
        uint32_t qt = 0, residual = 0, last = t0, fed = t0;
        uint32_t h;
        for (h = 1; h <= 24 * 7 && qt < PET_QT_DEAD; h++) {
            uint32_t now = t0 + H(h);
            uint32_t awake = awake_between(last, now) + residual;
            qt += awake / PET_SECONDS_PER_QT;
            residual = awake % PET_SECONDS_PER_QT;
            last = now;
            uint32_t days = (now - fed) / PET_MISSED_FEED_SECONDS;
            if (days) { fed += days * PET_MISSED_FEED_SECONDS; qt += PET_TIC(days); }
        }
        printf("  ignored from %02d:00 -> dead after %2u h of wall clock\n", start, h - 1);
    }
    printf("\n%s\n", fails ? "FAILURES ABOVE" : "all checks passed");
    return fails != 0;
}
