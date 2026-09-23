#pragma once

struct DriveOptions {
  const char* list  = nullptr;
  const char* cat   = nullptr;
  const char* write = nullptr;
};
bool RunDrives(DriveOptions const& /*options*/);
