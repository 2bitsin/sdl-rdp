#pragma once

namespace sdl_rdp::session::detail::backend {
class Backend;
}
namespace sdl_rdp::session::detail::presenter {
class Presenter;
}
namespace sdl_rdp::session::detail::session {
class Session;
}

namespace sdl_rdp::session {
using detail::backend::Backend;
using detail::presenter::Presenter;
using detail::session::Session;
}
