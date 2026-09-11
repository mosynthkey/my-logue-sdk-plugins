# Trance2 Research Notes

Tempo-synced trance drums + rolling bassline for NTS-3.

## Voices

| Voice | Source |
| --- | --- |
| Closed / Open hats | Packed 6-bit PCM from TR-909 Hi-Hat ROM (same as Trance) |
| Kick (BD) | Analog 909-style model (triangle → soft sine, pitch sweep, click) |
| Clap | Analog 909-style LFSR burst train (HClap path) |
| Bass | Mono saw mid + sine sub, short pluck envelope, kick sidechain duck |

## Bassline templates (web research)

Uplifting / driving trance bass conventions (Myloops, MusicRadar, common tutorials):

1. **Offbeat 1/8** — kick on quarters, bass on the `&` of each beat (progressive default).
2. **Rolling 16ths** — bass on every 16th *between* kicks (`e`, `&`, `a` of each beat = 12 notes/bar), all on the chord root.
3. **Walk / variation** — keep the roll, but earn the bar ending: fifth, leading tone, or pickup on the last `a` (step 15). Short amp (~30–60 ms decay), hard duck under the kick.

Complexity axis (X) morphs 1 → 2 → 3. Root fixed at A1 (MIDI 33).

## Pattern spine (16 steps)

- Kick: `0`, `4`, `8`, `12`
- Clap: `4`, `12`
- Open hats: `2`, `6`, `10`, `14`
- Bass seats: non-kick 16ths; density / pitch from X
- Default BPM: 138

## Mapping

| Control | Role |
| --- | --- |
| Hold pad | Gate only — steps lock to host 4ppqn |
| X / BASS | Bassline complexity |
| Y / DRUM | Hat density + clap-roll build; top-right flick = 1-bar Fill |

## Envelope note

Near-1 multiply coeffs use `1 + x` linearization (not `fasterexpf`). Bass and kick amp use age-based `fasterexpf(-age / tau)`.
