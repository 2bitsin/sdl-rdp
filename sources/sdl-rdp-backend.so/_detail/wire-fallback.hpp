#include "refresh.hpp"
namespace Backend {
WireSample SampleWire(int descriptor) {
  utilities::Expects(descriptor >= 0, "peer socket is open");
  return { };
}
}
