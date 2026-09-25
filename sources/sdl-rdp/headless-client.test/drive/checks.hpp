#pragma once
#include "session.hpp"

#include <string>

namespace sdl_rdp::headless_client_test::drive::detail::checks {
class DriveChecks : public DriveSession {
protected:
  auto ThenReadRanges(sdlrdp_file& file, std::string const& source) -> void;
};
}

namespace sdl_rdp::headless_client_test::drive {
using detail::checks::DriveChecks;
}
