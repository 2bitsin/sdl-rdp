#pragma once
#include "session.hpp"

#include <string>

namespace DriveGate {
class DriveChecks : public DriveSession {
protected:
  auto ThenReadRanges(sdlrdp_file* file, std::string const& source) -> void;
};
}
