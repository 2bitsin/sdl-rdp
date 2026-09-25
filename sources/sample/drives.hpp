#pragma once

namespace sample::detail::drives {
struct DriveOptions {
  char const* list  = nullptr;
  char const* cat   = nullptr;
  char const* write = nullptr;
};
auto RunDrives(DriveOptions const& /*options*/) -> bool;
}
namespace sample {
using detail::drives::DriveOptions;
using detail::drives::RunDrives;
}
