#include "Time.hpp"
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <limits>
int main() {
    const auto fixed = Time::steadyNow();
    const auto expected = Time::secNsec(fixed);
    const int64_t ns = expected.first * 1000000000LL + expected.second;
    int64_t low = std::numeric_limits<int64_t>::max();
    int64_t high = std::numeric_limits<int64_t>::min();
    int nonmonotonic = 0;
    int64_t previous = 0;
    for (int i = 0; i < 100000; ++i) {
        const auto result = Time::toTimespec(fixed);
        const int64_t actual = result.tv_sec * 1000000000LL + result.tv_nsec;
        low = std::min(low, actual - ns);
        high = std::max(high, actual - ns);
        if (i && actual < previous) ++nonmonotonic;
        previous = actual;
    }
    std::cout << "Upstream v0.52.1 Time.cpp, fixed input, 100000 conversions\n"
              << "minimum_error_ns=" << low << " maximum_error_ns=" << high
              << " range_ns=" << high - low << " nonmonotonic=" << nonmonotonic << '\n';
}
