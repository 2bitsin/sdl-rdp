#include <sdl-rdp/storage/malformed-response.hpp>

namespace Backend {
MalformedResponse::MalformedResponse(std::string const& cause, std::weak_ptr<DriveChannel> channel)
    : std::runtime_error(cause), origin(std::move(channel)) { }
}
