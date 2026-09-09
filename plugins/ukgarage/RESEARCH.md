# UKGarage Research Notes

Tempo-synced 2-step UK Garage phrase pad for NTS-3. Ghost notes are the
point of the design — not an afterthought.

## Sources (2026-09 web research)

- Native Instruments — UK garage / 2-step programming
  (kick on 1 + syncopated “and of 3”, snares on 2 and 4, swung hats)
- Reason Studios / MusicRadar — UKG identity is the *broken* kick + shuffle,
  not rigid four-on-the-floor
- Studio Brootle — heavy 16th swing (SP-1200-ish), short pitched-up snares,
  a second quieter snare as the “ghost snare” that drives the rhythm
- EvoSounds “Why Your UKG Drums Don't Groove” — three pillars:
  1. 16th swing ~63–67% (global amount on)
  2. velocity variation on every lane
  3. **ghost notes** at ~20–40% of main velocity between main hits
     - snare ghost a 16th before the backbeat pulls into 2 / 4
     - soft kick double in the second half of the bar pushes forward
- VIXSOUND / Minimal Audio — ghost snares between 2 and 4; hats with swing
  and open/closed pairing

Typical BPM cited: classic 2-step ~130–132, modern ~134–138. Default 134.

## Mapping onto NTS-3

| Control | Role |
| --- | --- |
| Hold pad | Run internal 16th clock (tempo via `setTempo`) |
| X / GHOST | Probability and seats for soft snare/rim ghosts |
| Y / FILL | Hat density + extra kicks; near top → fill energy; top-right flick → 1-bar Fill |
| SWING | 16th shuffle amount (default mid, toward UKG 64%-ish feel) |
| TONE / DEC | Kick/snare pitch colour and shared decay |
| MIX | Dry/wet |

## Pattern spine

16 steps = one bar:

- Kick spine: steps `0`, `10` (beat 1 and the and-of-3)
- Snare spine: steps `4`, `12` (beats 2 and 4)
- Ghost seats: `1,3,6,7,9,11,14,15` with stronger weight on `3` and `11`
  (the 16th before each snare)
- Closed hats: even 16ths + a few offbeats; Open hats: `2,6,10,14`
- Fill: denser snares every step, extra even kicks, almost-full hats

Ghost velocities stay in the ~0.18–0.36 range so they texture the groove
without competing with the backbone.

## Hardware / math pitfalls (from AGENTS.md)

- No libm: NTS-3 does not export `sinf`/`expf`/… — use `utils/float_math.h`
  (`fastersinfullf`, `fasterexpf`, `si_fabsf`). `ULIBS` is empty.
- Do **not** use `fasterexpf` for per-sample multiply coeffs near 1
  (`fasterexpf(0) ≈ 0.971`). Envelopes here are age-based:
  `level = fasterexpf(-age / tau)` with `|age/tau|` not ≪ 1 for most of the hit
  (HClap-style), so decay length stays musical (~tens of ms), not a click.

## Host triage checklist

1. `nm -u` / `readelf -r` on `ukgarage.elf` → no UND libm
2. Offline render at SNAP-like settings: body audible past ~20 ms
3. Low GHOST → few soft hits; high GHOST → many low-velocity ghosts between
   spine hits; high FILL / corner → roll / fill density
