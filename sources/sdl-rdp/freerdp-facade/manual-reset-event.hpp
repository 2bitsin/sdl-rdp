#pragma once
#include <sdl-rdp/freerdp-facade/rdp-handles.hpp>

#include <string_view>

namespace sdl_rdp::freerdp_facade::detail::manual_reset_event {
auto ManualResetEvent(std::string_view subject) -> EventHandle;
}

namespace sdl_rdp::freerdp_facade {
using detail::manual_reset_event::ManualResetEvent;
}
