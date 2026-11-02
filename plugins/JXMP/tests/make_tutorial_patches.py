#!/usr/bin/env python3
"""Writes the start.vcv of each tutorial lesson (plugins/JXMP/res/tutorials/*/start.vcv).
Port and param ids follow the enums in plugins/JXMP/src (comments name them). Positions are [HP, row].
Run from anywhere: python3 plugins/JXMP/tests/make_tutorial_patches.py"""
import json
import os

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "res", "tutorials")
COLORS = ["#f0a030", "#5b6ee1", "#3cb46e", "#e04e4e", "#9a6bd6", "#ee7b1f", "#3a8dde", "#f2c230"]


def module(mid, plugin, model, x, row=0, params=None):
    return {"id": mid, "plugin": plugin, "model": model, "version": "2.0.0",
            "params": [{"id": k, "value": v} for k, v in (params or {}).items()], "pos": [x, row]}


def patch(modules, cables):
    out = []
    for i, (om, oid, im, iid) in enumerate(cables):
        out.append({"id": i + 1, "outputModuleId": om, "outputId": oid, "inputModuleId": im, "inputId": iid,
                    "color": COLORS[i % len(COLORS)]})
    return {"version": "2.6.0", "zoom": 1.0, "gridOffset": [-1, 0], "modules": modules, "cables": out}


# Ids: large, so the tutorial can tell its start modules from a patch loaded later (patch files reuse 1, 2, 3…)
ID_BASE = 7300000000
PB, HA, CLK, VCO, VCF, ADSR, VCA, SCP, LFO, ATT = (ID_BASE + k for k in range(1, 11))
# Patchbay4: FRONT_INPUT 0-3, BACK_INPUT 4-7; FRONT_OUTPUT 0-3, BACK_OUTPUT 4-7
PB_FRONT_OUT_1, PB_BACK_IN_1 = 0, 4
# Host Audio level param 0: 0.5 = about -12 dB (quiet start)
HOST = {0: 0.5}
OUT_CABLE = (PB, PB_FRONT_OUT_1, HA, 0)  # patchbay front Ch 1 -> Host Audio input 1 (mono to both speakers)

lesson1 = patch([module(PB, "JXMP", "Patchbay4", 0), module(HA, "Cardinal", "HostAudio2", 4, params=HOST)],
                [OUT_CABLE])

# Each lesson starts where the one before ended (only the new modules of the lesson are added, unpatched), with
# settings that sound musical: a bass octave, eighth notes, short plucky notes.
SEQ, QNT, RND, EUC, DRM, LGC, MIX, DLY, CHR, REV = (ID_BASE + k for k in range(11, 21))
# Clock DIV_OUTPUT order: x4, x2, Beat, /2, /4, /8, /16
CLK_X4, CLK_X2 = 0, 1
CLOCK = {0: 110, 1: 1}                       # TEMPO 110 BPM, RUN on
VCO_BASS = {6: -1}                           # OCTAVE_PARAM 6: one octave down
PLUCK = {0: 0.1, 1: 0.62, 2: 0.6, 3: 0.58}   # ADSR 1 ms * 10000^v: attack 2.5 ms, decay 300 ms, sustain 60 %, release 210 ms
FILTER = {0: 0.6, 1: 0.4, 9: 0.5}            # VCF cutoff C4 * 2^(10v - 5) = 520 Hz, resonance 40 % (RES 1), envelope amount (ENV_AMT 9)
FILTER_LFO = dict(FILTER, **{}); FILTER_LFO[3] = 0.35  # + Cutoff CV (FREQ_CV 3)
LFO_SLOW = {0: -2.0}                         # LFO rate 0.25 Hz
# A little melody for the sequencer (CV_PARAM 0-7, volts; the quantizer makes it A minor)
MELODY = {0: 0.0, 1: 0.33, 2: 0.58, 3: 0.83, 4: 1.0, 5: 0.83, 6: 0.58, 7: 0.33}
A_MINOR = {0: 2, 1: 9}                       # Quantizer SCALE 2 = Minor, ROOT 9 = A
# Euclid (STEPS 0-3, HITS 4-7, ROTATE 8-11): hi-hat (track 3) on the off-beats only, 4 hits rotated by 2
HATS_OFFBEAT = {6: 4, 10: 2}

# Row 0: the voice; row 1: modulation and sequencing; row 2: drums, mixer, effects
def row0(vcf=None, adsr=None, vco=None):
    mods = [
        module(VCO, "JXMP", "VCO", 0, params=vco),
        module(VCA, "JXMP", "VCA", 18),
        module(SCP, "JXMP", "Scope", 26),
        module(PB, "JXMP", "Patchbay4", 50),
        module(HA, "Cardinal", "HostAudio2", 54, params=HOST),
    ]
    if vcf is not None:
        mods += [module(CLK, "JXMP", "Clock", 64, params=CLOCK), module(VCF, "JXMP", "VCF", 76, params=vcf),
                 module(ADSR, "JXMP", "ADSR", 90, params=adsr)]
    return mods

VOICE_OUT = (VCA, 0, PB, PB_BACK_IN_1)  # VCA OUT 1 -> Patchbay back Ch 1

# Lesson 2: lesson 1 finished (VCO SIN -> VCA -> speakers, SIN on the scope) + clock, filter, envelope
lesson2 = patch(row0(vcf={}, adsr=PLUCK, vco=VCO_BASS), [
    OUT_CABLE, VOICE_OUT,
    (VCO, 0, VCA, 0),     # VCO SIN -> VCA IN 1
    (VCO, 0, SCP, 0),     # VCO SIN -> Scope IN 1
])

# The voice of lesson 2 finished: saw -> filter -> amplifier, envelope on both, eighth notes
VOICE = [
    OUT_CABLE, VOICE_OUT,
    (VCO, 2, VCF, 0),     # VCO SAW -> VCF IN
    (VCF, 0, VCA, 0),     # VCF OUT -> VCA IN 1
    (ADSR, 0, VCA, 1),    # ADSR ENV -> VCA CV 1
    (ADSR, 0, VCF, 2),    # ADSR ENV -> VCF ENV
]

# Lesson 3: lesson 2 finished + LFO, attenuverter
lesson3 = patch(row0(vcf=FILTER, adsr=PLUCK, vco=VCO_BASS) + [
    module(LFO, "JXMP", "LFO", 0, row=1),
    module(ATT, "JXMP", "Attenuverter", 10, row=1),
], VOICE + [
    (CLK, CLK_X2, ADSR, 0),  # Clock x2 -> ADSR GATE
    (ADSR, 0, SCP, 0),       # ADSR ENV -> Scope IN 1
])

# Lesson 4: the voice of lesson 3 with the LFO on the filter (the trill unplugged) + sequencer, quantizer, random
lesson4 = patch(row0(vcf=FILTER_LFO, adsr=PLUCK, vco=VCO_BASS) + [
    module(LFO, "JXMP", "LFO", 0, row=1, params=LFO_SLOW),
    module(ATT, "JXMP", "Attenuverter", 10, row=1),
    module(SEQ, "JXMP", "SEQ", 20, row=1),
    module(QNT, "JXMP", "Quantizer", 38, row=1),
    module(RND, "JXMP", "Random", 46, row=1),
], VOICE + [
    (CLK, CLK_X2, ADSR, 0),  # Clock x2 -> ADSR GATE
    (LFO, 1, VCF, 3),        # LFO TRI -> VCF FREQ CV
    (LFO, 0, SCP, 0),        # LFO SIN -> Scope IN 1
    (VCA, 0, SCP, 1),        # VCA OUT 1 -> Scope IN 2
])

# The melody of lesson 4 finished (sequencer through the quantizer, A minor)
MELODY_CABLES = VOICE + [
    (LFO, 1, VCF, 3),        # LFO TRI -> VCF FREQ CV
    (LFO, 0, SCP, 0),        # LFO SIN -> Scope IN 1
    (VCA, 0, SCP, 1),        # VCA OUT 1 -> Scope IN 2
    (CLK, CLK_X2, SEQ, 0),   # Clock x2 -> SEQ CLOCK
    (SEQ, 1, ADSR, 0),       # SEQ GATE -> ADSR GATE
    (SEQ, 0, QNT, 0),        # SEQ CV -> Quantizer IN
    (QNT, 0, VCO, 0),        # Quantizer OUT -> VCO V/OCT
]
MELODY_MODULES = row0(vcf=FILTER_LFO, adsr=PLUCK, vco=VCO_BASS) + [
    module(LFO, "JXMP", "LFO", 0, row=1, params=LFO_SLOW),
    module(ATT, "JXMP", "Attenuverter", 10, row=1),
    module(SEQ, "JXMP", "SEQ", 20, row=1, params=MELODY),
    module(QNT, "JXMP", "Quantizer", 38, row=1, params=A_MINOR),
    module(RND, "JXMP", "Random", 46, row=1),
]

# Lesson 5: the melody of lesson 4 playing + Euclid, drums, logic, mixer
lesson5 = patch(MELODY_MODULES + [
    module(EUC, "JXMP", "Euclid", 0, row=2, params=HATS_OFFBEAT),
    module(DRM, "JXMP", "Drums", 14, row=2),
    module(LGC, "JXMP", "Logic", 32, row=2),
    module(MIX, "JXMP", "Mixer", 40, row=2),
], MELODY_CABLES)

# Lesson 6: lesson 5 finished (melody and drums through the mixer, RESET to both sequencers) + delay, chorus,
# reverb. The RESET cables come last: patching RESET restarts the clock, so the sequencers start together.
lesson6 = patch(MELODY_MODULES + [
    module(EUC, "JXMP", "Euclid", 0, row=2, params=HATS_OFFBEAT),
    module(DRM, "JXMP", "Drums", 14, row=2),
    module(LGC, "JXMP", "Logic", 32, row=2),
    module(MIX, "JXMP", "Mixer", 40, row=2, params={1: 0.55}),  # drums (channel 2) lower than the melody
    module(DLY, "JXMP", "Delay", 54, row=2),
    module(CHR, "JXMP", "Chorus", 64, row=2),
    module(REV, "JXMP", "Reverb", 74, row=2),
], [c for c in MELODY_CABLES if c != VOICE_OUT] + [
    (CLK, CLK_X4, EUC, 0),   # Clock x4 -> Euclid CLOCK
    (EUC, 0, DRM, 0),        # Euclid GATE 1 -> Drums KICK TRIG
    (EUC, 1, DRM, 1),        # Euclid GATE 2 -> Drums SNARE TRIG
    (EUC, 2, DRM, 2),        # Euclid GATE 3 -> Drums HAT TRIG
    (EUC, 0, LGC, 0),        # Euclid GATE 1 -> Logic A
    (EUC, 1, LGC, 1),        # Euclid GATE 2 -> Logic B (clap with the snare on 2 and 4)
    (LGC, 0, DRM, 3),        # Logic AND -> Drums CLAP TRIG
    (VCA, 0, MIX, 0),        # VCA OUT 1 -> Mixer IN 1
    (DRM, 4, MIX, 1),        # Drums MIX -> Mixer IN 2
    (MIX, 0, PB, PB_BACK_IN_1),  # Mixer MIX -> Patchbay back Ch 1
    (CLK, 7, EUC, 1),        # Clock RESET (DIV_OUTPUT x7, then RESET) -> Euclid RESET
    (CLK, 7, SEQ, 1),        # Clock RESET -> SEQ RESET
])

for name, data in [("01-first-sound", lesson1), ("02-filter-envelope", lesson2), ("03-modulation", lesson3),
                   ("04-sequences", lesson4), ("05-drums", lesson5), ("06-effects-mix", lesson6)]:
    path = os.path.join(ROOT, name, "start.vcv")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        json.dump(data, f, indent=1)
        f.write("\n")
    print("wrote", path)
