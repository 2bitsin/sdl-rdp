#include <sdl-rdp/sample-gate.test/process/trace-number.hpp>
#include <oxbox/utilities/number-text.hpp>
#include <sdl-rdp/utilities/contract.hpp>
namespace sdl_rdp::sample_gate_test::process::detail::trace_number {
using sdl_rdp::utilities::Required;

auto TraceNumber(std::string_view line, std::string_view marker) -> std::int64_t {
  return Required(oxbox::utilities::ParseNumberAfter<std::int64_t>(line, marker),
                  "trace carries a whole number after the marker");
}
}
