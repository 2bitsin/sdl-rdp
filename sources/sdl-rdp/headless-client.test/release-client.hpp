#pragma once

#include <freerdp/freerdp.h>

namespace Headless {
struct ReleaseClient {
public:
  auto operator()(freerdp* instance) const -> void;
};
}
