#pragma once
#include "test-drive-session.hpp"

#include <string>

namespace DriveGate {
class DriveChecks : public DriveSession {
protected:
  void ThenReadRanges(sdlrdp_file* file, std::string const& source);
};
}
