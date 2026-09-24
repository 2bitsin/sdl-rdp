#pragma once

struct DriveOptions {
  char const* list  = nullptr;
  char const* cat   = nullptr;
  char const* write = nullptr;
};
auto RunDrives(DriveOptions const& /*options*/) -> bool;
