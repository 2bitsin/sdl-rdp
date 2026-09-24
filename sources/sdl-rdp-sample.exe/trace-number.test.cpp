#include "_detail/trace-number.hpp"
#include <sdl-rdp-backend.so/_detail/contract.hpp>
#include <oxbox/utilities/number-text.hpp>
#include <stdexcept>
#include <string>
namespace SampleGate {
int64_t TraceNumber(std::string_view line, std::string_view marker) {
  auto at = line.find(marker);
  utilities::Expects(at != std::string_view::npos, "trace contains the requested field");
  auto token = line.substr(at + marker.size());
  auto value = oxbox::utilities::ParseNumber<int64_t>(token.substr(0, token.find(' ')));
  utilities::Expects(value.has_value(), "trace field is an integer");
  if (!value) throw std::invalid_argument(std::string(line));
  return *value;
}
}
