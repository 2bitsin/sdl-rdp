#include <sdl-rdp/drive/malformed-response.hpp>

namespace sdl_rdp::drive::detail::malformed_response {
MalformedResponse::MalformedResponse(std::string const& cause, std::weak_ptr<channel::DriveChannel> channel)
    : std::runtime_error(cause), origin(std::move(channel)) { }
}
