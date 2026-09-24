#pragma once
#include <cstdint>
#include <string_view>
namespace SampleGate {
auto TraceNumber(std::string_view line, std::string_view marker) -> std::int64_t;
}
