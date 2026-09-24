#pragma once

#include <winpr/stream.h>

namespace Backend {
struct ReleaseStream {
public:
  void operator()(wStream* stream) const;
};
}
