#include "refresh.hpp"
namespace Backend {
auto SampleWire(int descriptor) -> WireSample {
  utilities::Expects(descriptor >= 0, "peer socket is open");
  return { };
}
}
