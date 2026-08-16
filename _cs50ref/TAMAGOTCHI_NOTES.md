# Source Material Notes — The Original Tamagotchi (P1, 1996)

Research notes on the original Bandai Tamagotchi ("P1", 1996 Japan / 1997 US) as
background for designing our own virtual pet. This is reference material, not a
spec — our design (see [DESIGN.md](DESIGN.md)) is deliberately its own framework.

## 1. The hardware — and how it compares to ours

The P1 runs on an **Epson E0C6S46**: a 4-bit CPU clocked at **32.768 kHz** — the
same frequency as a watch crystal, because it *is* a watch-chip family. Bandai
shipped a Casio-watch-class computer with a pet on it; we're doing the reverse,
putting a pet on an actual watch. Delightfully, the whole P1 CPU runs at the exact
frequency our SAM L22 uses just for its RTC.

| | Tamagotchi P1 (1996) | Sensor Watch Pro (ours) |
| --- | --- | --- |
| CPU | Epson E0C6S46, **4-bit** | ATSAML22, ARM Cortex-M0+, **32-bit** |
| Clock | 32.768 kHz (~5–7k instructions/s) | up to 32 MHz (tens of millions of instructions/s) |
| ROM/Flash | 6,144 × 12-bit words (~9 KB) | 256 KB |
| RAM | 640 × 4-bit (320 B) + 160 × 4-bit display RAM | 32 KB |
| Display | **32×16 dot matrix** (512 px) + 8 icons | ~60 fixed 7-segment-style segments + 5 icons |
| Buttons | 3 | 3 |
| Sound | piezo beeper | piezo buzzer (87-note range) |
| Extras | — | RGB LED, accelerometer, temp sensor, RTC w/ backup registers, USB |

The punchline: we have roughly **three to four orders of magnitude more compute
and a hundred times the memory — but only about a tenth the display resolution.**
The P1 could draw little character sprites, animate them walking around, show a
tombstone. We get sixty fixed segments. Their constraint was compute; ours is
expressiveness. That inversion is what makes this project interesting: all the
personality has to come through eye shapes, LED color, and sound.

(The E0C6S46 technical manual is public, which is why first-gen Tamagotchis are
fully emulated today — see TamaLIB and MAME.)

## 2. How the P1 actually works

**Stats.** Four hearts each for **Hungry** and **Happy** (empty → full). A status
meter screen shows age, weight, discipline gauge, hunger, and happiness. Weight
goes up with food, down with play.

**Care actions** (via icon row + 3 buttons):

- **Feed**: meal fills a hunger heart; snack fills a happy heart but adds weight
- **Game**: guess which way the pet turns, 5 rounds — 3/5 wins fill one happy
  heart, 5/5 fills two (play also reduces weight)
- **Clean**: flush poop; adults poop roughly every 3 hours, babies far more often
- **Medicine**: cures sickness (skull icon); it gets sick once per growth stage,
  plus extra if left sitting in poop
- **Lights off** when it sleeps
- **Scold** when it misbehaves (fills the discipline gauge 25% per scold; it
  misbehaves exactly enough times to reach 100%)

**Attention economy.** When it needs something, an attention icon lights and it
beeps, with a **15-minute window** to respond. Missing a legitimate call = a
**care mistake**. Clever twist: some calls are *false alarms* (misbehavior) where
the right answer is scolding, not feeding — pure neediness isn't always correct.

**Life cycle.** Egg hatches 5 minutes after clock-set → baby (needs ~an hour of
constant attention) → child after 65 minutes → teen at age 3 → adult at age 6 →
a secret form possible at ages 8–12. **Age ticks up once per wake-up**, and sleep
schedules vary by character (8 PM–9 AM up to 11 PM–11 AM). Which adult you get is
determined by **care mistakes + discipline level** during each stage — care
quality is the genetics.

**Death.** Untreated sickness, or old age; accumulated care mistakes shorten the
lifespan. The US version shows an angel; the Japanese original a ghost and
tombstone. Then you start a new egg.

## 3. Takeaways for our design (borrow the ideas, not the pet)

Worth borrowing:

- **Care mistakes as the core currency** — a counted, windowed "it called, did you
  answer?" mechanic is simple, fair, and drives everything else
- **Attention call with a timeout** maps perfectly to our hourly background check +
  chirp + BELL indicator
- **Stage-gated content** — even with only eye-sets instead of sprites, evolving
  the *eye vocabulary* by life stage (baby eyes vs. elder eyes) gives the same
  "what will I get?" hook
- **Snack-vs-meal tension** (instant happiness with a long-term cost) is a lot of
  game design for one branch
- **Sleep schedule that the pet owns** (not just a fixed config) adds personality

Deliberately different in ours:

- **No sprite characters** — identity and mood live in eye animation, LED color,
  and chirps (our display can't do sprites, so we lean into what it can do)
- **Play = physical interaction** (accelerometer taps) instead of a button
  guessing game — the watch is *worn*, which the P1 never was
- **Lazy timestamp simulation** instead of an always-on loop — the P1 could afford
  to poll constantly at 32 kHz; we sleep and compute elapsed state on wake
- **No discipline/scolding in v1** — false-alarm calls are a great mechanic but
  add UI complexity; noted as a possible later addition
- **Real-world day cycle** — our pet lives on watch time (RTC), so its sleep
  schedule can genuinely match the wearer's day

## Sources

- [Tamagotchi Tech Specs (loociano)](https://github.com/loociano/tamagotchi-tech-specs/blob/master/index.md) — P1 hardware breakdown
- [Epson E0C6S46 Technical Manual (PDF)](https://download.epson-europe.com/pub/electronics-de/asmic/4bit/62family/technicalmanual/tm_6s46.pdf) — the actual CPU docs
- [TamaLIB (jcrona)](https://github.com/jcrona/tamalib/) — hardware-agnostic first-gen Tamagotchi emulator
- [Thaao's Tamagotchi P1 Care Guide](https://thaao.net/tama/p1/) — mechanics, timings, growth chart
- [Tamagotchi Wiki — Tamagotchi (1996 Pet)](https://tamagotchi.fandom.com/wiki/Tamagotchi_(1996_Pet)) — history and mechanics
- [Tamagotchi P1 User Guide scan (archive.org)](https://archive.org/stream/bandai-tamagotchi-p1-1996/bandai-tamagotchi-p1-1996_djvu.txt) — original manual
- [Spreading virtual life everywhere (blog.rona.fr)](http://blog.rona.fr/post/2021/08/04/Spreading-virtual-life-everywhere) — TamaLIB author on porting the P1 everywhere
