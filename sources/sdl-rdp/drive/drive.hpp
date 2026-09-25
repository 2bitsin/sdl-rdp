#pragma once
#include <cstdint>
#include <string>

namespace sdl_rdp::drive::detail::drive {
struct Drive {
  std::uint32_t id  { };
  std::string   name;
  friend auto operator==(Drive const&, Drive const&) -> bool = default;
};
}

namespace sdl_rdp::drive {
using detail::drive::Drive;
}
