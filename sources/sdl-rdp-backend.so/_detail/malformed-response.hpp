#pragma once
#include <memory>
#include <stdexcept>
#include <string>

namespace Backend {
class DriveChannel;
struct MalformedResponse : std::runtime_error {
public:
  MalformedResponse(std::string const& cause, std::weak_ptr<DriveChannel> channel);
  std::weak_ptr<DriveChannel> origin;
};
}
