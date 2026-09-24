#include "_detail/trace-number.hpp"
#include <oxbox/utilities/number-text.hpp>
#include <sdl-rdp-backend.so/_detail/contract.hpp>
namespace SampleGate {
auto TraceNumber(std::string_view line, std::string_view marker) -> int64_t {
  return utilities::Required(oxbox::utilities::ParseNumberAfter<int64_t>(line, marker),
                             "trace carries a whole number after the marker");
}
}
