#pragma once

#include <utility>
#include <vector>
#include <winpr/wtypes.h>

namespace Headless {
auto ToneMeasurements(std::vector<INT16> const& samples, unsigned rate) -> std::pair<double, double>;
}
