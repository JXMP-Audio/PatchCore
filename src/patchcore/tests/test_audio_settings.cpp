#include <cassert>
#include <cstdio>
#include <cstdlib>
#include "../../../dpf/distrho/src/jackbridge/AudioDeviceSettings.hpp"

using namespace patchcore_audio;

static DeviceInfo dev(const char* name, unsigned outs, unsigned ins, std::vector<unsigned> rates) {
	DeviceInfo d;
	d.name = name;
	d.outputs = outs;
	d.inputs = ins;
	d.sampleRates = rates;
	return d;
}

int main() {
	// Round trip
	AudioSetup s = defaultSetup();
	s.driver = Driver::Alsa;
	s.output = "Scarlett 2i2: USB Audio (hw:2,0)";
	s.input = "Scarlett 2i2: USB Audio (hw:2,0)";
	s.sampleRate = 96000;
	s.bufferSize = 128;
	const AudioSetup r = parse(serialize(s));
	assert(r.driver == Driver::Alsa && r.output == s.output && r.input == s.input && r.sampleRate == 96000 && r.bufferSize == 128);

	// parse garbage: defaults, no crash
	const AudioSetup g = parse("driver=banana\nsampleRate=-5\nbufferSize=abc\n\x01\x02 junk without equals\n");
	const AudioSetup d = defaultSetup();
	assert(g.driver == d.driver && g.sampleRate == d.sampleRate && g.bufferSize == d.bufferSize && g.output.empty());
	assert(parse("").sampleRate == 48000);
	// A file edited on Windows (CRLF): values without the \r
	const AudioSetup crlf = parse("output=Built-in Audio\r\nsampleRate=44100\r\n");
	assert(crlf.output == "Built-in Audio" && crlf.sampleRate == 44100);

	// nearest
	assert(nearest({44100, 48000, 96000}, 88200) == 96000);
	assert(nearest({44100, 48000}, 44100) == 44100);
	assert(nearest({}, 48000) == 48000);

	const std::vector<DeviceInfo> devices = {
	    dev("Built-in Audio", 2, 2, {44100, 48000}),
	    dev("Scarlett 2i2: USB Audio (hw:2,0)", 2, 2, {44100, 48000, 88200, 96000}),
	};

	// The saved device exists and supports everything: kept, no notice
	Choice c = chooseSetup(s, devices);
	assert(c.setup.output == s.output && c.setup.sampleRate == 96000 && c.setup.bufferSize == 128 && c.notice.empty());

	// missing output device: default output, input off, a notice naming the card
	AudioSetup gone = s;
	gone.output = gone.input = "USB Thing";
	c = chooseSetup(gone, devices);
	assert(c.setup.output.empty() && c.setup.input.empty());
	assert(c.notice.find("USB Thing") != std::string::npos);

	// Unsupported rate → nearest the device supports; odd buffer → nearest allowed
	AudioSetup odd = s;
	odd.output = "Built-in Audio";
	odd.input.clear();
	odd.sampleRate = 96000;
	odd.bufferSize = 300;
	c = chooseSetup(odd, devices);
	assert(c.setup.sampleRate == 48000 && c.setup.bufferSize == 256);

	// PipeWire/JACK: the server decides the device; the setup passes through
	AudioSetup jack = defaultSetup();
	jack.driver = Driver::Jack;
	c = chooseSetup(jack, {});
	assert(c.setup.driver == Driver::Jack && c.notice.empty());

	// Config path override and file round trip
	// saveSetup creates a missing folder
	setenv("PATCHCORE_AUDIO_CONFIG", "/tmp/jxmp-tests/audio-new-dir/audio-test.conf", 1);
	assert(configPath() == "/tmp/jxmp-tests/audio-new-dir/audio-test.conf");
	assert(saveSetup(s));
	assert(loadSetup().output == s.output);

	// Latency: 256 samples at 48 kHz ≈ 5.3 ms
	AudioSetup lat = defaultSetup();
	assert(latencyMs(lat) > 5.3 && latencyMs(lat) < 5.4);

	std::printf("audio settings: all tests passed\n");
	return 0;
}
