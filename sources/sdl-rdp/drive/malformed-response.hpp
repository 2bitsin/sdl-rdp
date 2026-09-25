#pragma once
#include <memory>
#include <stdexcept>
#include <string>

namespace sdl_rdp::drive::detail::channel {
class DriveChannel;
}
namespace sdl_rdp::drive::detail::malformed_response {
struct MalformedResponse : std::runtime_error {
public:
  MalformedResponse(std::string const& cause, std::weak_ptr<channel::DriveChannel> channel);
  std::weak_ptr<channel::DriveChannel> origin;
};
}
namespace sdl_rdp::drive {
using detail::malformed_response::MalformedResponse;
}
