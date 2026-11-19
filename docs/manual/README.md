# JXMP modules — manual

One page per module. In PatchCore, right-click a module and choose **Info › User manual**: its page opens here.

Every page has the same parts: *What it does*, *How it works* (with a signal-flow picture), *Quick start*, the
*Front panel* and *Back panel* tables, *Everything it can do*, *Patch ideas*, *Good to know* and *See also*.

**The rack has two sides.** JXMP modules have their knobs on the front and their sockets on the back: `Tab` turns the
rack (or `F1` front and `F2` back). Basic modules (Host MIDI, Host Audio, other plugins) have their sockets on the front;
a [patchbay](jx-pb.md) connects the two sides. `F3` opens the Mixer view of [JX-CONSOLE](jx-console.md), `F4` the
[Editor](jx-edit.md). All keys are in the top bar under **Shortcuts**.

**Learning.** **Learn** in the top bar holds interactive lessons in six categories: 1 Basics, 2 Sound synthesis,
3 Rhythm and sequences, 4 Effects and mix, 5 Editor and song, 6 Advanced. Each step says why, what to do, and what
just happened; PatchCore checks your patch as you go. **View → Language** switches the lessons to Czech (more of the UI will follow).

## Sound sources

| | Module | What it is |
|---|---|---|
| <img src="img/jx-osc-front.png" height="90"> | [JX-OSC](jx-osc.md) · VCO | Oscillator: four shapes, morph, sub, unison up to 7 voices. |
| <img src="img/jx-nse-front.png" height="90"> | [JX-NSE](jx-nse.md) · Noise | Seven colours of noise. |
| <img src="img/jx-drm-front.png" height="90"> | [JX-DRM](jx-drm.md) · Drums | Kick, snare, hi-hat, clap. |
| <img src="img/jx-smpl-front.png" height="90"> | [JX-SMPL](jx-smpl.md) · Sampler | 16 sample slots with editing, loops, envelopes and recording. |
| <img src="img/jx-nest-front.png" height="90"> | [JX-NEST](jx-nest.md) · Plugin host | A VST3 / CLAP / LV2 plugin inside the patch. |

## Filters and shapers

| | Module | What it is |
|---|---|---|
| <img src="img/jx-flt-front.png" height="90"> | [JX-FLT](jx-flt.md) · VCF | Ladder filter: LP, BP, HP, notch, 12 / 24 dB. |
| <img src="img/jx-fld-front.png" height="90"> | [JX-FLD](jx-fld.md) · Folder | Wavefolder. |
| <img src="img/jx-rng-front.png" height="90"> | [JX-RNG](jx-rng.md) · Ring | Ring modulator. |

## Envelopes and modulation

| | Module | What it is |
|---|---|---|
| <img src="img/jx-env-front.png" height="90"> | [JX-ENV](jx-env.md) · ADSR | Envelope with ADSR, AD and LOOP modes. |
| <img src="img/jx-lfo-front.png" height="90"> | [JX-LFO](jx-lfo.md) · LFO | Low-frequency oscillator with clock sync. |
| <img src="img/jx-rnd-front.png" height="90"> | [JX-RND](jx-rnd.md) · Random | Stepped and smooth random, sample & hold. |
| <img src="img/jx-slw-front.png" height="90"> | [JX-SLW](jx-slw.md) · Slew | Glide and slew limiter. |

## Sequencing and time

| | Module | What it is |
|---|---|---|
| <img src="img/jx-edit-front.png" height="90"> | [JX-EDIT](jx-edit.md) · Editor | Piano roll for whole songs (F4). |
| <img src="img/jx-clk-front.png" height="90"> | [JX-CLK](jx-clk.md) · Clock | Master clock with divisions and swing. |
| <img src="img/jx-seq-front.png" height="90"> | [JX-SEQ](jx-seq.md) · SEQ | Eight-step sequencer. |
| <img src="img/jx-euc-front.png" height="90"> | [JX-EUC](jx-euc.md) · Euclid | Four Euclidean rhythm tracks. |
| <img src="img/jx-qnt-front.png" height="90"> | [JX-QNT](jx-qnt.md) · Quantizer | Pitch to the nearest note of a scale. |

## Effects

| | Module | What it is |
|---|---|---|
| <img src="img/jx-dly-front.png" height="90"> | [JX-DLY](jx-dly.md) · Delay | Mono delay up to 10 s. |
| <img src="img/jx-rev-front.png" height="90"> | [JX-REV](jx-rev.md) · Reverb | Stereo reverb: plate, hall, room. |
| <img src="img/jx-chr-front.png" height="90"> | [JX-CHR](jx-chr.md) · Chorus | Stereo chorus and ensemble. |

## Mixing and routing

| | Module | What it is |
|---|---|---|
| <img src="img/jx-console-front.png" height="90"> | [JX-CONSOLE](jx-console.md) · Console 4–32 | The song mixer with EQ, sends and the Mixer view (F3). |
| <img src="img/jx-mix-front.png" height="90"> | [JX-MIX](jx-mix.md) · Mixer | Four-channel utility mixer. |
| <img src="img/jx-amp-front.png" height="90"> | [JX-AMP](jx-amp.md) · VCA | Two VCAs. |
| <img src="img/jx-sw-front.png" height="90"> | [JX-SW](jx-sw.md) · Switch | Sequential switch 4 → 1 and 1 → 4. |
| <img src="img/jx-pb4-front.png" height="90"> | [JX-PB4 / 8 / 16](jx-pb.md) · Patchbay | Front ↔ back connections. |

## Utilities

| | Module | What it is |
|---|---|---|
| <img src="img/jx-att-front.png" height="90"> | [JX-ATT](jx-att.md) · Attenuverter | Gain and offset, manual voltages. |
| <img src="img/jx-lgc-front.png" height="90"> | [JX-LGC](jx-lgc.md) · Logic | AND, OR, XOR, NOT and a comparator. |
| <img src="img/jx-ply-front.png" height="90"> | [JX-PLY](jx-ply.md) · Poly | Split, merge, count voices, mult. |
| <img src="img/jx-scp-front.png" height="90"> | [JX-SCP](jx-scp.md) · Scope | Two-channel oscilloscope. |

## A first voice

The smallest patch that plays notes from a keyboard or the Editor:

```mermaid
flowchart LR
  HM[Host MIDI] -->|V/OCT, GATE| PB[JX-PB4]
  PB --> OSC[JX-OSC] --> FLT[JX-FLT] --> AMP[JX-AMP] --> CON[JX-CONSOLE]
  PB --> ENV[JX-ENV]
  ENV --> FLT & AMP
  CON -->|MAIN L / R| PB2[JX-PB4] --> HA[Host Audio]
```

The [Cardinal modules](../CARDINAL-MODULES.md) page describes Host MIDI, Host Audio and the other basic modules.
