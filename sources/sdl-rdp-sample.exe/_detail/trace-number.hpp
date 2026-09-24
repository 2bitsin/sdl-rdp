#pragma once
#include <cstdint>
#include <string_view>
namespace SampleGate {
int64_t TraceNumber(std::string_view line, std::string_view marker);
}
