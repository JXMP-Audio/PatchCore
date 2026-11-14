/*
 * RtAudio Bridge for DPF
 * Copyright (C) 2021-2025 Filipe Coelho <falktx@falktx.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any purpose with
 * or without fee is hereby granted, provided that the above copyright notice and this
 * permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD
 * TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN
 * NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER
 * IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef RTAUDIO_BRIDGE_HPP_INCLUDED
#define RTAUDIO_BRIDGE_HPP_INCLUDED

#include "NativeBridge.hpp"

#if (DISTRHO_PLUGIN_NUM_INPUTS + DISTRHO_PLUGIN_NUM_OUTPUTS) == 0
# error RtAudio without audio does not make sense
#endif

#if defined(DISTRHO_OS_MAC)
# define __MACOSX_CORE__
# define RTAUDIO_API_TYPE MACOSX_CORE
# define RTMIDI_API_TYPE MACOSX_CORE
#elif defined(DISTRHO_OS_WINDOWS) && !defined(_MSC_VER)
# define __WINDOWS_WASAPI__
# define __WINDOWS_MM__
# ifdef PATCHCORE_HAVE_ASIO
#  define __WINDOWS_ASIO__  // PatchCore: ASIO next to WASAPI
# endif
# define RTAUDIO_API_TYPE WINDOWS_WASAPI
# define RTMIDI_API_TYPE WINDOWS_MM
#else
// PatchCore: the native driver on Linux is ALSA (direct); PipeWire is reached through JACK, no PulseAudio fallback
# if defined(HAVE_ALSA)
#  define RTAUDIO_API_TYPE LINUX_ALSA
# endif
# ifdef HAVE_ALSA
#  define __LINUX_ALSA__
#  define RTMIDI_API_TYPE LINUX_ALSA
# endif
#endif

#ifdef RTAUDIO_API_TYPE
# include "rtaudio/RtAudio.h"
# include "rtmidi/RtMidi.h"
# include "../../extra/ScopedPointer.hpp"
# include "../../extra/String.hpp"
# include "../../extra/ScopedDenormalDisable.hpp"

using DISTRHO_NAMESPACE::ScopedDenormalDisable;
using DISTRHO_NAMESPACE::ScopedPointer;
using DISTRHO_NAMESPACE::String;

struct RtAudioBridge : NativeBridge {
    // pointer to RtAudio instance
    ScopedPointer<RtAudio> handle;
    bool captureEnabled = false;
   #if defined(RTMIDI_API_TYPE) && DISTRHO_PLUGIN_WANT_MIDI_INPUT
    std::vector<RtMidiIn> midiIns;
   #endif
   #if defined(RTMIDI_API_TYPE) && DISTRHO_PLUGIN_WANT_MIDI_OUTPUT
    std::vector<RtMidiOut> midiOuts;
   #endif

    // caching
    String name;
    uint nextBufferSize = 512;

    // PatchCore: what to open (driver, device names, rate, buffer); after an open it holds the real values
    patchcore_audio::AudioSetup setup = patchcore_audio::defaultSetup();
    static volatile bool* sRunning;

    RtAudioBridge()
    {
       #if defined(RTMIDI_API_TYPE) && (DISTRHO_PLUGIN_WANT_MIDI_INPUT || DISTRHO_PLUGIN_WANT_MIDI_OUTPUT)
        midiAvailable = true;
       #endif
    }

    const char* getVersion() const noexcept
    {
        return RTAUDIO_VERSION;
    }

    bool open(const char* const clientName) override
    {
        name = clientName;
        // PatchCore: the saved setup, checked against the devices that exist now
        setup = patchcore_audio::gPatchCoreSetup;
        try {
            RtAudio probe(apiFor(setup.driver));
            const patchcore_audio::Choice c = patchcore_audio::chooseSetup(setup, devicesOf(probe));
            setup = c.setup;
            if (! c.notice.empty() && patchcore_audio::gPatchCoreNotice.empty())
                patchcore_audio::gPatchCoreNotice = c.notice;
        } DISTRHO_SAFE_EXCEPTION("PatchCore: probing audio devices");

        if (_open(! setup.input.empty()))
            return true;

        // The chosen device does not open: the default output, no input
        if (! setup.output.empty() || ! setup.input.empty())
        {
            patchcore_audio::gPatchCoreNotice = "Could not open " + (setup.output.empty() ? setup.input : setup.output)
                                              + ", using the default output";
            setup.output.clear();
            setup.input.clear();
            return _open(false);
        }
        return false;
    }

    // PatchCore: RtAudio API of a driver (the compiled-in default when the driver is not native)
    static RtAudio::Api apiFor(const patchcore_audio::Driver driver)
    {
        switch (driver)
        {
       #ifdef __LINUX_ALSA__
        case patchcore_audio::Driver::Alsa: return RtAudio::LINUX_ALSA;
       #endif
       #ifdef __WINDOWS_WASAPI__
        case patchcore_audio::Driver::Wasapi: return RtAudio::WINDOWS_WASAPI;
       #endif
       #ifdef __WINDOWS_ASIO__
        case patchcore_audio::Driver::Asio: return RtAudio::WINDOWS_ASIO;
       #endif
        default: return RtAudio::RTAUDIO_API_TYPE;
        }
    }

    static std::vector<patchcore_audio::DeviceInfo> devicesOf(RtAudio& rt)
    {
        std::vector<patchcore_audio::DeviceInfo> out;
        const uint count = rt.getDeviceCount();
        for (uint i = 0; i < count; ++i)
        {
            RtAudio::DeviceInfo info;
            try {
                info = rt.getDeviceInfo(i);
            } catch (...) { continue; }
            if (! info.probed)
                continue;
            patchcore_audio::DeviceInfo d;
            d.name = info.name;
            d.outputs = info.outputChannels;
            d.inputs = info.inputChannels;
            d.sampleRates = info.sampleRates;
            out.push_back(d);
        }
        return out;
    }

    std::vector<patchcore_audio::DeviceInfo> listDevices() override
    {
        if (handle == nullptr)
            return {};
        return devicesOf(*handle);
    }

    patchcore_audio::AudioSetup currentSetup() const override
    {
        return setup;
    }

    bool reopen(const patchcore_audio::AudioSetup& wanted, std::string* const error) override
    {
        const patchcore_audio::AudioSetup previous = setup;
        const uint previousRate = sampleRate;
        const uint previousBufferSize = bufferSize;

        // The audio thread stops first (it takes midiInLock itself); then the MIDI input threads are held off
        // while the buffers they write into are freed and created again.
        // Aborted, not stopped: RtAudio's stopStream drains the output (snd_pcm_drain) holding the stream mutex, and
        // through PipeWire's ALSA plugin a drain may never finish: the whole app hung on a sample-rate change.
        // The stream is closed right after, so what is still queued does not need to play.
        if (handle != nullptr && handle->isStreamRunning())
        {
            try {
                handle->abortStream();
            } DISTRHO_SAFE_EXCEPTION("handle->abortStream()");
        }

        bool ok;
        {
            const RecursiveMutexLocker rml(midiInLock);

            if (handle != nullptr)
                close();

            setup = wanted;
            ok = _open(! wanted.input.empty());

            if (! ok)
            {
                if (error != nullptr)
                    *error = "Could not open " + (wanted.output.empty() ? std::string("the default output") : wanted.output)
                           + " at " + std::to_string(wanted.sampleRate) + " Hz";
                setup = previous;
                if (! _open(! previous.input.empty()))
                {
                    setup.output.clear();
                    setup.input.clear();
                    _open(false);
                }
            }
        }

        if (handle == nullptr)
        {
            running = false;
            return false;
        }

        if (bufferSize != previousBufferSize && bufferSizeCallback != nullptr)
            bufferSizeCallback(bufferSize, jackBufferSizeArg);
        if (sampleRate != previousRate && sampleRateCallback != nullptr)
            sampleRateCallback(sampleRate, jackSampleRateArg);

        running = true;
        activate();
        return ok;
    }

    // RtAudio's error callback has no user pointer: a stream error marks the (single) bridge as stopped
    // ALSA reports a lost device only as warnings ("audio write error, No such device"): an underrun is normal,
    // but a run of other read/write errors means the card is gone
    static void errorCallback(const RtAudioError::Type type, const std::string& text)
    {
        static int ioErrors = 0;
        if (type == RtAudioError::DEBUG_WARNING)
            return;
        if (type == RtAudioError::WARNING)
        {
            const bool ioError = (text.find("audio write error") != std::string::npos
                               || text.find("audio read error") != std::string::npos)
                              && text.find("underrun") == std::string::npos;
            if (! ioError)
                return;
            if (++ioErrors < 20)
                return;
        }
        ioErrors = 0;
        d_stderr("PatchCore audio: %s", text.c_str());
        if (sRunning != nullptr)
            *sRunning = false;
    }

    bool close() override
    {
        DISTRHO_SAFE_ASSERT_RETURN(handle != nullptr, false);

        if (handle->isStreamRunning())
        {
            try {
                handle->abortStream();
            } DISTRHO_SAFE_EXCEPTION("handle->abortStream()");
        }

        // PatchCore: always, since every open allocates them (an output-only variant leaked them before)
        freeBuffers();
        handle = nullptr;
        return true;
    }

    bool activate() override
    {
        DISTRHO_SAFE_ASSERT_RETURN(handle != nullptr, false);

        try {
            handle->startStream();
        } DISTRHO_SAFE_EXCEPTION_RETURN("handle->startStream()", false);

        return true;
    }

    bool deactivate() override
    {
        DISTRHO_SAFE_ASSERT_RETURN(handle != nullptr, false);

        try {
            handle->stopStream();
        } DISTRHO_SAFE_EXCEPTION_RETURN("handle->stopStream()", false);

        return true;
    }

    bool isAudioInputEnabled() const override
    {
       #if DISTRHO_PLUGIN_NUM_INPUTS > 0
        return captureEnabled;
       #else
        return false;
       #endif
    }

    bool requestAudioInput() override
    {
       #if DISTRHO_PLUGIN_NUM_INPUTS > 0
        // PatchCore: the input of the output device (or the default input)
        patchcore_audio::AudioSetup wanted = setup;
        if (wanted.input.empty())
            wanted.input = wanted.output.empty() ? std::string("default") : wanted.output;
        return reopen(wanted, nullptr);
       #else
        return false;
       #endif
    }

    bool isMIDIEnabled() const override
    {
       #if defined(RTMIDI_API_TYPE) && DISTRHO_PLUGIN_WANT_MIDI_INPUT
        if (!midiIns.empty())
            return true;
       #endif
       #if defined(RTMIDI_API_TYPE) && DISTRHO_PLUGIN_WANT_MIDI_OUTPUT
        if (!midiOuts.empty())
            return true;
       #endif
        return false;
    }

    bool requestMIDI() override
    {
        d_stdout("%s %d", __PRETTY_FUNCTION__, __LINE__);
        // clear ports in use first
       #if defined(RTMIDI_API_TYPE) && DISTRHO_PLUGIN_WANT_MIDI_INPUT
        if (!midiIns.empty())
        {
            try {
                midiIns.clear();
            } catch (const RtMidiError& err) {
                d_safe_exception(err.getMessage().c_str(), __FILE__, __LINE__);
                return false;
            } DISTRHO_SAFE_EXCEPTION_RETURN("midiIns.clear()", false);
        }
       #endif
       #if defined(RTMIDI_API_TYPE) && DISTRHO_PLUGIN_WANT_MIDI_OUTPUT
        if (!midiOuts.size())
        {
            try {
                midiOuts.clear();
            } catch (const RtMidiError& err) {
                d_safe_exception(err.getMessage().c_str(), __FILE__, __LINE__);
                return false;
            } DISTRHO_SAFE_EXCEPTION_RETURN("midiOuts.clear()", false);
        }
       #endif

        // query port count
       #if defined(RTMIDI_API_TYPE) && DISTRHO_PLUGIN_WANT_MIDI_INPUT
        uint midiInCount;
        try {
            RtMidiIn midiIn(RtMidi::RTMIDI_API_TYPE, name.buffer());
            midiInCount = midiIn.getPortCount();
        } catch (const RtMidiError& err) {
            d_safe_exception(err.getMessage().c_str(), __FILE__, __LINE__);
            return false;
        } DISTRHO_SAFE_EXCEPTION_RETURN("midiIn.getPortCount()", false);
       #endif
       #if defined(RTMIDI_API_TYPE) && DISTRHO_PLUGIN_WANT_MIDI_OUTPUT
        uint midiOutCount;
        try {
            RtMidiOut midiOut(RtMidi::RTMIDI_API_TYPE, name.buffer());
            midiOutCount = midiOut.getPortCount();
        } catch (const RtMidiError& err) {
            d_safe_exception(err.getMessage().c_str(), __FILE__, __LINE__);
            return false;
        } DISTRHO_SAFE_EXCEPTION_RETURN("midiOut.getPortCount()", false);
       #endif

        // open all possible ports
       #if defined(RTMIDI_API_TYPE) && DISTRHO_PLUGIN_WANT_MIDI_INPUT
        for (uint i=0; i<midiInCount; ++i)
        {
            try {
                RtMidiIn midiIn(RtMidi::RTMIDI_API_TYPE, name.buffer());
                midiIn.setCallback(RtMidiCallback, this);
                midiIn.openPort(i);
                midiIns.push_back(std::move(midiIn));
            } catch (const RtMidiError& err) {
                d_safe_exception(err.getMessage().c_str(), __FILE__, __LINE__);
            } DISTRHO_SAFE_EXCEPTION("midiIn.openPort()");
        }
       #endif
       #if defined(RTMIDI_API_TYPE) && DISTRHO_PLUGIN_WANT_MIDI_OUTPUT
        for (uint i=0; i<midiOutCount; ++i)
        {
            try {
                RtMidiOut midiOut(RtMidi::RTMIDI_API_TYPE, name.buffer());
                midiOut.openPort(i);
                midiOuts.push_back(std::move(midiOut));
            } catch (const RtMidiError& err) {
                d_safe_exception(err.getMessage().c_str(), __FILE__, __LINE__);
            } DISTRHO_SAFE_EXCEPTION("midiOut.openPort()");
        }
       #endif

        return true;
    }

    bool supportsBufferSizeChanges() const override
    {
        return true;
    }

    bool requestBufferSizeChange(const uint32_t newBufferSize) override
    {
        patchcore_audio::AudioSetup wanted = setup;
        wanted.bufferSize = newBufferSize;
        return reopen(wanted, nullptr);
    }

    bool _open(const bool withInput, RtAudio* tryingAgain = nullptr)
    {
        ScopedPointer<RtAudio> rtAudio;

        if (tryingAgain == nullptr)
        {
            try {
                rtAudio = new RtAudio(apiFor(setup.driver));  // PatchCore: the chosen driver
            } DISTRHO_SAFE_EXCEPTION_RETURN("new RtAudio()", false);
        }
        else
        {
            rtAudio = tryingAgain;
        }

        // PatchCore: the chosen buffer size, and devices by name (empty or not found = the default)
        nextBufferSize = setup.bufferSize;
        uint rtAudioBufferFrames = nextBufferSize;
        const auto deviceId = [&rtAudio](const std::string& wanted, const bool output) -> uint
        {
            if (! wanted.empty())
            {
                const uint count = rtAudio->getDeviceCount();
                for (uint i = 0; i < count; ++i)
                {
                    try {
                        const RtAudio::DeviceInfo info = rtAudio->getDeviceInfo(i);
                        if (info.probed && info.name == wanted && (output ? info.outputChannels : info.inputChannels) > 0)
                            return i;
                    } catch (...) {}
                }
            }
            return output ? rtAudio->getDefaultOutputDevice() : rtAudio->getDefaultInputDevice();
        };

       #if DISTRHO_PLUGIN_NUM_INPUTS > 0
        RtAudio::StreamParameters inParams;
       #endif
        RtAudio::StreamParameters* inParamsPtr = nullptr;

       #if DISTRHO_PLUGIN_NUM_INPUTS > 0
        if (withInput)
        {
            inParams.deviceId = deviceId(setup.input, false);
            inParams.nChannels = DISTRHO_PLUGIN_NUM_INPUTS_2;
            inParamsPtr = &inParams;
        }
       #endif

       #if DISTRHO_PLUGIN_NUM_OUTPUTS > 0
        RtAudio::StreamParameters outParams;
        outParams.deviceId = tryingAgain != nullptr ? 1 : deviceId(setup.output, true);
        outParams.nChannels = DISTRHO_PLUGIN_NUM_OUTPUTS_2;
        RtAudio::StreamParameters* const outParamsPtr = &outParams;
       #else
        RtAudio::StreamParameters* const outParamsPtr = nullptr;
       #endif

        RtAudio::StreamOptions opts;
        opts.flags = RTAUDIO_NONINTERLEAVED;
        // PatchCore: ALSA's "default" only when no device is named (the flag applies to output and input alike)
        if (setup.output.empty() && (! withInput || setup.input.empty() || setup.input == "default"))
            opts.flags |= RTAUDIO_ALSA_USE_DEFAULT;
       #ifndef DISTRHO_OS_MAC
       /* RtAudio in macOS uses a different than usual way to handle audio block size,
        * where RTAUDIO_MINIMIZE_LATENCY makes CoreAudio use very low latencies (around 15 samples).
        * That has serious performance drawbacks, so we skip that here.
        */
        opts.flags |= RTAUDIO_MINIMIZE_LATENCY;
       #endif
        opts.numberOfBuffers = 2;
        opts.streamName = name.buffer();

        try {
            sRunning = &running;
            rtAudio->openStream(outParamsPtr, inParamsPtr, RTAUDIO_FLOAT32, setup.sampleRate, &rtAudioBufferFrames,
                                RtAudioCallback, this, &opts, errorCallback);
        } catch (const RtAudioError& err) {
           #if DISTRHO_PLUGIN_NUM_OUTPUTS > 0
            if (outParams.deviceId == 0 && rtAudio->getDeviceCount() > 1)
                return _open(withInput, rtAudio.release());
           #endif
            d_safe_exception(err.getMessage().c_str(), __FILE__, __LINE__);
            return false;
        } DISTRHO_SAFE_EXCEPTION_RETURN("rtAudio->openStream()", false);

        handle = rtAudio;
        bufferSize = rtAudioBufferFrames;
        sampleRate = handle->getStreamSampleRate();
        // PatchCore: buffers for every port on every open (a reopen freed them; the extra ports need them)
        allocBuffers(true, true);
        captureEnabled = withInput;
        setup.sampleRate = sampleRate;
        setup.bufferSize = bufferSize;
        return true;
    }

    static int RtAudioCallback(void* const outputBuffer,
                              #if DISTRHO_PLUGIN_NUM_INPUTS > 0
                               void* const inputBuffer,
                              #else
                               void*,
                              #endif
                               const uint numFrames,
                               const double /* streamTime */,
                               const RtAudioStreamStatus /* status */,
                               void* const userData)
    {
        RtAudioBridge* const self = static_cast<RtAudioBridge*>(userData);

        if (self->jackProcessCallback == nullptr)
        {
            if (outputBuffer != nullptr)
                std::memset((float*)outputBuffer, 0, sizeof(float)*numFrames*DISTRHO_PLUGIN_NUM_OUTPUTS_2);
            return 0;
        }

       #if DISTRHO_PLUGIN_NUM_INPUTS > 0
        if (float* const insPtr = static_cast<float*>(inputBuffer))
        {
            for (uint i=0; i<DISTRHO_PLUGIN_NUM_INPUTS_2; ++i)
                self->audioBuffers[i] = insPtr + (i * numFrames);
        }
       #endif

       #if DISTRHO_PLUGIN_NUM_OUTPUTS > 0
        if (float* const outsPtr = static_cast<float*>(outputBuffer))
        {
            for (uint i=0; i<DISTRHO_PLUGIN_NUM_OUTPUTS_2; ++i)
                self->audioBuffers[DISTRHO_PLUGIN_NUM_INPUTS + i] = outsPtr + (i * numFrames);
        }
       #endif

        const ScopedDenormalDisable sdd;
        self->jackProcessCallback(numFrames, self->jackProcessArg);

       #if DISTRHO_PLUGIN_WANT_MIDI_OUTPUT
        if (self->midiOutBuffer.isDataAvailableForReading())
        {
            static_assert(kMaxMIDIInputMessageSize + 1u == 4, "change code if bumping this value");
            uint8_t data[4] = {};

            while (self->midiOutBuffer.isDataAvailableForReading() &&
                    self->midiOutBuffer.readCustomData(data, ARRAY_SIZE(data)))
            {
                // offset not used in RtMidiOut
                self->midiOutBuffer.readUInt();

                for (std::vector<RtMidiOut>::iterator it = self->midiOuts.begin(), end = self->midiOuts.end(); it != end; ++it)
                {
                    static_cast<RtMidiOut&>(*it).sendMessage(data + 1, data[0]);
                }
            }

            self->midiOutBuffer.flush();
        }
       #endif

        return 0;
    }

   #if defined(RTMIDI_API_TYPE) && DISTRHO_PLUGIN_WANT_MIDI_INPUT
    static void RtMidiCallback(double /*timestamp*/, std::vector<uchar>* const message, void* const userData)
    {
        const size_t len = message->size();
        DISTRHO_SAFE_ASSERT_RETURN(len > 0 && len <= kMaxMIDIInputMessageSize,);

        RtAudioBridge* const self = static_cast<RtAudioBridge*>(userData);

        const RecursiveMutexLocker rml(self->midiInLock);

        self->midiInBufferPending.writeByte(static_cast<uint8_t>(len));
        // TODO timestamp
        // self->midiInBufferPending.writeDouble(timestamp);
        self->midiInBufferPending.writeCustomData(message->data(), len);
        for (uint8_t i = len; i < kMaxMIDIInputMessageSize; ++i)
            self->midiInBufferPending.writeByte(0);
        self->midiInBufferPending.commitWrite("RtMidiCallback");
    }
   #endif
};

// PatchCore: the running flag the RtAudio error callback clears (one bridge per process)
volatile bool* RtAudioBridge::sRunning = nullptr;

#endif // RTAUDIO_API_TYPE
#endif // RTAUDIO_BRIDGE_HPP_INCLUDED
