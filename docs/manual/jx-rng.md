# JX-RNG — Ring

6 HP · JXMP Audio · [all modules](README.md)

Ring modulator: multiplies A by B (bells, metal, robot voices; an LFO on B gives tremolo), with dry/wet. Polyphonic.

<img src="img/jx-rng-front.png" alt="JX-RNG front" height="380"> <img src="img/jx-rng-back.png" alt="JX-RNG back (sockets)" height="380">

## Front panel

| Control | What it does |
|---|---|
| MIX | From A alone (dry) to A × B (ring). |

## Sockets (back, `Tab` turns the rack)

| Socket | What it does |
|---|---|
| IN A | Carrier, e.g. an oscillator (±5 V). |
| IN B | Modulator, e.g. a second oscillator at another pitch. |
| MIX CV | 0–10 V adds to MIX. |
| OUT | A × B (5 V × 5 V = 5 V), mixed with A by MIX. |

## How to use it

- Bell: two JX-OSC SIN at a non-whole ratio (e.g. one an octave and a tritone up) into A and B, then a short envelope.
- Tremolo: a JX-LFO SIN (UNI) into B, MIX 50 %.
