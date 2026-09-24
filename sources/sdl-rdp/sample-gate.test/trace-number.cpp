#include <sdl-rdp/sample-gate.test/trace-number.hpp>
#include <oxbox/utilities/number-text.hpp>
#include <sdl-rdp/utilities/contract.hpp>
namespace SampleGate {
auto TraceNumber(std::string_view line, std::string_view marker) -> std::int64_t {
  return utilities::Required(oxbox::utilities::ParseNumberAfter<std::int64_t>(line, marker),
                             "trace carries a whole number after the marker");
}
}
