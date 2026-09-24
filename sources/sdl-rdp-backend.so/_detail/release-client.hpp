#pragma once

#include <freerdp/freerdp.h>

namespace Headless {
struct ReleaseClient {
public:
  void operator()(freerdp* instance) const;
};
}
