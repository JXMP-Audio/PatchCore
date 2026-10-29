#include "Serial.hpp"
#include <cassert>
#include <cstdio>

int main() {
	assert(jxmp::serialNumber(-1) == "----");
	const std::string a = jxmp::serialNumber(1234567890123LL);
	assert(a == jxmp::serialNumber(1234567890123LL));
	assert(a.size() == 9 && a[4] == '-');
	for (size_t i = 0; i < a.size(); ++i)
		assert(i == 4 || (a[i] >= '0' && a[i] <= '9') || (a[i] >= 'A' && a[i] <= 'F'));
	assert(jxmp::serialNumber(1) != jxmp::serialNumber(2));
	std::puts("jxmp base: all tests passed");
	return 0;
}
