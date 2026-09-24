#pragma once

#include <freerdp/listener.h>
#include <memory>

namespace Backend {
struct ReleasesListener {
public:
  void operator()(freerdp_listener* listener) const;
};

using ListenerHandle = std::unique_ptr<freerdp_listener, ReleasesListener>;
}
