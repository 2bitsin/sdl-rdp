#pragma once
#include "session.hpp"
#include <sdl-rdp/drive/file.hpp>

#include <string>

namespace sdl_rdp::headless_client_test::drive::detail::checks {
using sdl_rdp::drive::File;
class DriveChecks : public DriveSession {
protected:
  static auto ThenReadRanges(File& file, std::string const& source) -> void;
};
}

namespace sdl_rdp::headless_client_test::drive {
using detail::checks::DriveChecks;
}
