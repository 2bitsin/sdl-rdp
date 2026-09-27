#pragma once

namespace sdl_rdp::freerdp_facade::detail::connection {
class Connection;
}
namespace sdl_rdp::freerdp_facade::detail::channel_manager {
class ChannelManager;
}
namespace sdl_rdp::freerdp_facade::detail::updates {
class Updates;
}

namespace sdl_rdp::freerdp_facade {
using detail::connection::Connection;
using detail::channel_manager::ChannelManager;
using detail::updates::Updates;
}
