#pragma once
#include <optional>
#include <string_view>

namespace sample::detail::drives {
struct DriveOptions {
  std::optional<std::string_view> list;
  std::optional<std::string_view> cat;
  std::optional<std::string_view> write;
};
auto RunDrives(DriveOptions const& /*options*/) -> bool;
}

namespace sample {
using detail::drives::DriveOptions;
using detail::drives::RunDrives;
}
