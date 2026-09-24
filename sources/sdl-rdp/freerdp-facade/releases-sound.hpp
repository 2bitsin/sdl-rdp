#pragma once

#include <freerdp/server/rdpsnd.h>
#include <memory>

namespace Backend {
struct ReleasesSound {
public:
  auto operator()(RdpsndServerContext* sound) const -> void;
};

using SoundContext = std::unique_ptr<RdpsndServerContext, ReleasesSound>;
}
