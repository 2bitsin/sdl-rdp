#pragma once

namespace sdl_rdp::freerdp_facade::detail::channel_manager {
class ChannelManager;
}
namespace sdl_rdp::freerdp_facade::detail::connection {
class Connection;
}
namespace sdl_rdp::freerdp_facade::detail::updates {
class Updates;
}
namespace sdl_rdp::freerdp_facade::detail::wait_handle {
class WaitHandle;
}

namespace sdl_rdp::freerdp_facade {
using detail::channel_manager::ChannelManager;
using detail::connection::Connection;
using detail::updates::Updates;
using detail::wait_handle::WaitHandle;
}
