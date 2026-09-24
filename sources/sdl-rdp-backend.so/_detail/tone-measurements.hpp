#pragma once

#include <utility>
#include <vector>
#include <winpr/wtypes.h>

namespace Headless {
std::pair<double, double> ToneMeasurements(std::vector<INT16> const& samples, unsigned rate);
}
