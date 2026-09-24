#pragma once

#include <winpr/stream.h>

namespace Backend {
struct ReleaseStream {
public:
  auto operator()(wStream* stream) const -> void;
};
}
