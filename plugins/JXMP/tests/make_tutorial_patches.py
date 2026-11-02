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

voice = [
    module(CLK, "JXMP", "Clock", 0, params={0: 100, 1: 1}),  # TEMPO 100 BPM, RUN on
    module(VCO, "JXMP", "VCO", 12),
    module(VCF, "JXMP", "VCF", 30),
    module(ADSR, "JXMP", "ADSR", 44),
    module(VCA, "JXMP", "VCA", 58),
    module(SCP, "JXMP", "Scope", 66),
    module(PB, "JXMP", "Patchbay4", 90),
    module(HA, "Cardinal", "HostAudio2", 94, params=HOST),
]
lesson2 = patch(voice, [OUT_CABLE])

# Lesson 3 starts from the finished lesson 2: resonance and envelope amount up
voice3 = [dict(m) for m in voice]
voice3[2] = module(VCF, "JXMP", "VCF", 30, params={1: 0.5, 9: 0.5})  # RES_PARAM 1, ENV_AMT_PARAM 9
voice3 += [module(LFO, "JXMP", "LFO", 0, row=1), module(ATT, "JXMP", "Attenuverter", 10, row=1)]
lesson3 = patch(voice3, [
    OUT_CABLE,
    (VCO, 2, VCF, 0),           # VCO SAW_OUTPUT 2 -> VCF IN_INPUT 0
    (VCF, 0, VCA, 0),           # VCF OUT_OUTPUT 0 -> VCA IN1_INPUT 0
    (VCA, 0, PB, PB_BACK_IN_1),  # VCA OUT1_OUTPUT 0 -> Patchbay back Ch 1
    (CLK, 2, ADSR, 0),          # Clock DIV_OUTPUT+2 "Beat" -> ADSR GATE_INPUT 0
    (ADSR, 0, VCA, 1),          # ADSR ENV_OUTPUT 0 -> VCA CV1_INPUT 1
    (ADSR, 0, SCP, 0),          # ADSR ENV -> Scope IN1_INPUT 0
    (ADSR, 0, VCF, 2),          # ADSR ENV -> VCF ENV_INPUT 2
])

# Lessons 4-6 (sequences, drums, effects and mix)
SEQ, QNT, RND, EUC, DRM, LGC, MIX, DLY, CHR, REV = (ID_BASE + k for k in range(11, 21))
CLOCK = {0: 100, 1: 1}  # TEMPO 100 BPM, RUN on
# Clock DIV_OUTPUT order: x4, x2, Beat, /2, /4, /8, /16
CLK_X4, CLK_X2 = 0, 1
# A little melody for the sequencer (CV_PARAM 0-7, volts; the quantizer makes it a scale)
MELODY = {0: 0.0, 1: 0.33, 2: 0.58, 3: 0.83, 4: 1.0, 5: 0.83, 6: 0.58, 7: 0.33}

lesson4 = patch([
    module(CLK, "JXMP", "Clock", 0, params=CLOCK),
    module(SEQ, "JXMP", "SEQ", 12),
    module(QNT, "JXMP", "Quantizer", 30),
    module(RND, "JXMP", "Random", 38),
    module(VCO, "JXMP", "VCO", 46),
    module(ADSR, "JXMP", "ADSR", 64),
    module(VCA, "JXMP", "VCA", 78),
    module(PB, "JXMP", "Patchbay4", 86),
    module(HA, "Cardinal", "HostAudio2", 90, params=HOST),
], [
    OUT_CABLE,
    (VCO, 2, VCA, 0),            # VCO SAW -> VCA IN 1
    (ADSR, 0, VCA, 1),           # ADSR ENV -> VCA CV 1
    (VCA, 0, PB, PB_BACK_IN_1),  # VCA OUT 1 -> Patchbay back Ch 1
])

lesson5 = patch([
    module(CLK, "JXMP", "Clock", 0, params=CLOCK),
    module(EUC, "JXMP", "Euclid", 12),
    module(DRM, "JXMP", "Drums", 26),
    module(LGC, "JXMP", "Logic", 44),
    module(PB, "JXMP", "Patchbay4", 52),
    module(HA, "Cardinal", "HostAudio2", 56, params=HOST),
], [OUT_CABLE])

# Lesson 6: a melody (lesson 4 finished, quantized) and a beat (lesson 5) playing, nothing to the speakers yet
seq_params = dict(MELODY)
lesson6 = patch([
    module(CLK, "JXMP", "Clock", 0, params=CLOCK),
    module(SEQ, "JXMP", "SEQ", 12, params=seq_params),
    module(QNT, "JXMP", "Quantizer", 30),
    module(VCO, "JXMP", "VCO", 38),
    module(ADSR, "JXMP", "ADSR", 56),
    module(VCA, "JXMP", "VCA", 70),
    module(EUC, "JXMP", "Euclid", 0, row=1),
    module(DRM, "JXMP", "Drums", 14, row=1),
    module(MIX, "JXMP", "Mixer", 32, row=1),
    module(DLY, "JXMP", "Delay", 46, row=1),
    module(CHR, "JXMP", "Chorus", 56, row=1),
    module(REV, "JXMP", "Reverb", 66, row=1),
    module(PB, "JXMP", "Patchbay4", 78),
    module(HA, "Cardinal", "HostAudio2", 82, params=HOST),
], [
    OUT_CABLE,
    (CLK, CLK_X2, SEQ, 0),       # Clock x2 -> SEQ CLOCK
    (SEQ, 1, ADSR, 0),           # SEQ GATE -> ADSR GATE
    (SEQ, 0, QNT, 0),            # SEQ CV -> Quantizer IN
    (QNT, 0, VCO, 0),            # Quantizer OUT -> VCO V/OCT
    (VCO, 2, VCA, 0),            # VCO SAW -> VCA IN 1
    (ADSR, 0, VCA, 1),           # ADSR ENV -> VCA CV 1
    (CLK, CLK_X4, EUC, 0),       # Clock x4 -> Euclid CLOCK
    (EUC, 0, DRM, 0),            # Euclid GATE 1 -> Drums KICK TRIG
    (EUC, 1, DRM, 1),            # Euclid GATE 2 -> Drums SNARE TRIG
    (EUC, 2, DRM, 2),            # Euclid GATE 3 -> Drums HAT TRIG
])

for name, data in [("01-first-sound", lesson1), ("02-filter-envelope", lesson2), ("03-modulation", lesson3),
                   ("04-sequences", lesson4), ("05-drums", lesson5), ("06-effects-mix", lesson6)]:
    path = os.path.join(ROOT, name, "start.vcv")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        json.dump(data, f, indent=1)
        f.write("\n")
    print("wrote", path)
