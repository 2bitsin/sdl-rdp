#pragma once

#include <winpr/wtypes.h>
#include <utility>
#include <vector>

namespace Headless {
auto ToneMeasurements(std::vector<INT16> const& samples, unsigned rate) -> std::pair<double, double>;
}
