#pragma once

namespace sdl_rdp::video::pointer::detail::sender {
class PointerSender;
}
namespace sdl_rdp::video::pointer::detail::store {
class PointerStore;
}

namespace sdl_rdp::video::pointer {
using detail::sender::PointerSender;
using detail::store::PointerStore;
}
