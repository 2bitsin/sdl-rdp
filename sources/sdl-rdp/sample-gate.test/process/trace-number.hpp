#pragma once
#include <cstdint>
#include <string_view>
namespace sdl_rdp::sample_gate_test::process::detail::trace_number {
auto TraceNumber(std::string_view line, std::string_view marker) -> std::int64_t;
}

namespace sdl_rdp::sample_gate_test::process {
using detail::trace_number::TraceNumber;
}
