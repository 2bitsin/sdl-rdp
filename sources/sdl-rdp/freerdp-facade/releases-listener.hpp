#pragma once

#include <freerdp/listener.h>
#include <memory>

namespace Backend {
struct ReleasesListener {
public:
  auto operator()(freerdp_listener* listener) const -> void;
};

using ListenerHandle = std::unique_ptr<freerdp_listener, ReleasesListener>;
}
