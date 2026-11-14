/*
 * DISTRHO Cardinal Plugin
 * Copyright (C) 2021-2026 Filipe Coelho <falktx@falktx.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#ifdef BUILDING_PLUGIN_MODULES
#include "rack.hpp"
#endif
#include <patchcore/Song.hpp>

namespace rack {
namespace midi {
struct Message;
}
}

// --------------------------------------------------------------------------------------------------------------------
// Base DISTRHO classes

#ifndef DISTRHO_DETAILS_HPP_INCLUDED
namespace CardinalDISTRHO {
struct MidiEvent {
    static const uint32_t kDataSize = 4;
    uint32_t frame;
    uint32_t size;
    uint8_t data[kDataSize];
    const uint8_t* dataExt;
};
}
#endif

#ifndef DISTRHO_PLUGIN_HPP_INCLUDED
namespace CardinalDISTRHO {
class Plugin;
}
#endif

#ifndef DISTRHO_UI_HPP_INCLUDED
namespace CardinalDISTRHO {
class UI;
}
#endif

// --------------------------------------------------------------------------------------------------------------------
// Base DGL classes

namespace CardinalDGL {
#ifndef DGL_BASE_HPP_INCLUDED
class TopLevelWidget;
struct IdleCallback {
    virtual ~IdleCallback() {}
    virtual void idleCallback() = 0;
};
#endif
#ifndef DGL_NANO_WIDGET_HPP_INCLUDED
template <class BaseWidget> class NanoBaseWidget;
typedef NanoBaseWidget<TopLevelWidget> NanoTopLevelWidget;
#endif
}

using CardinalDGL::IdleCallback;

// --------------------------------------------------------------------------------------------------------------------
// Cardinal specific context

enum CardinalVariant {
    kCardinalVariantFX,
    kCardinalVariantLoader,
    kCardinalVariantMain,
    kCardinalVariantMini,
    kCardinalVariantNative,
    kCardinalVariantSynth,
};

struct CardinalPluginContext : rack::Context {
    const CardinalVariant variant;
    const uint32_t parameterCount;
    float* const parameters;
    uint32_t bufferSize, processCounter;
    double sampleRate;
    bool bypassed, playing, reset, bbtValid;
    int32_t bar, beat, beatsPerBar, beatType;
    uint64_t frame;
    double barStartTick, beatsPerMinute;
    double tick, tickClock, ticksPerBeat, ticksPerClock, ticksPerFrame;
    uintptr_t nativeWindowId;
    const float* const* dataIns;
    float** dataOuts;
    const CardinalDISTRHO::MidiEvent* midiEvents;
    uint32_t midiEventCount;
    CardinalDISTRHO::Plugin* const plugin;
    CardinalDGL::NanoTopLevelWidget* tlw;
    CardinalDISTRHO::UI* ui;
    // PatchCore: internal transport, used in standalone when the host provides no musical time
    volatile bool internalTransportPlaying = true;  // PatchCore: plays at launch; Space pauses
    volatile bool internalTransportResetRequested = false;
    // PatchCore: paused with Space: the engine is frozen and the modules never see a stop (they go on seamlessly)
    volatile bool internalTransportHeld = false;
    volatile double internalTransportBeatsPerMinute = 120.0;
    volatile int32_t internalTransportBeatsPerBar = 4;
    volatile int32_t internalTransportBeatType = 4;
    uint64_t internalTransportFrame = 0;
    bool internalTransportActive = false;
    // PatchCore: the Editor's loop (LOOP button): playback runs between START and STOP, in beats from the song start
    volatile bool internalTransportLoop = false;
    volatile double internalTransportLoopStart = 0.0;
    volatile double internalTransportLoopEnd = 16.0;
    // PatchCore: the metronome (CLICK button) and REC (the Editor records the MIDI keyboard)
    volatile bool internalTransportMetronome = false;
    volatile bool internalTransportRecord = false;
    // PatchCore: the Editor moves the song position (a click on its ruler): the frame to go to, -1 = none
    volatile int64_t internalTransportSeekFrame = -1;
    // PatchCore: the Editor's song (JX-EDIT): played into the MIDI that the Host MIDI modules read, track N on channel N
    ::pcsong::SongStore editorSong;
    // The MIDI keyboard plays this Editor track (moved to its MIDI channel, 0..15) while the Editor is open or REC is
    // on; -1 = the keyboard as it sends (a Host MIDI on its channel plays it directly)
    volatile int editorKeyboardTrack = -1;
    volatile int editorKeyboardChannel = 0;
    // The note the Editor lets sound while one is drawn or dragged (pcsong::NotePreview::pack, 0 = none)
    volatile int32_t editorPreview = 0;
    // Keyboard notes recorded while REC is on (audio thread → the Editor)
    ::pcsong::RecordRing editorRecord;
    // PatchCore: peak level of the first two plugin outputs over the last block, for the transport meters
    volatile float outputPeaks[2] = {};
    // PatchCore: Audio Settings "Test tone": frames of a 440 Hz beep still to play on the first two outputs
    volatile int32_t testToneFrames = 0;
    float testTonePhase = 0.f;
    CardinalPluginContext(CardinalDISTRHO::Plugin* const p);
    void writeMidiMessage(const rack::midi::Message& message, uint8_t channel);
    bool addIdleCallback(IdleCallback* cb) const;
    void removeIdleCallback(IdleCallback* cb) const;
};

#ifndef HEADLESS
void handleHostParameterDrag(const CardinalPluginContext* pcontext, uint index, bool started);
#endif

// --------------------------------------------------------------------------------------------------------------------
