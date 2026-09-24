#include "_detail/trace-number.hpp"
#include <sdl-rdp-backend.so/_detail/contract.hpp>
#include <oxbox/utilities/number-text.hpp>
namespace SampleGate {
int64_t TraceNumber(std::string_view line, std::string_view marker) {
  return utilities::Required(oxbox::utilities::ParseNumberAfter<int64_t>(line, marker),
                             "trace carries a whole number after the marker");
}
}
