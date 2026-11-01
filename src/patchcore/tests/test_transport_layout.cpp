#include <cassert>
#include <cstdio>
#include "../TransportLayout.hpp"

using namespace rack::patchcore;

int main() {
	const TransportLayout s = transportLayout(true);
	assert(s.height == 74.f && s.buttons && s.editableTempo && s.time);
	// Plugin: thin, read-only, the host owns the transport
	const TransportLayout p = transportLayout(false);
	assert(p.height == 34.f && !p.buttons && !p.editableTempo && !p.time);
	std::printf("transport layout: all tests passed\n");
	return 0;
}
