#pragma once

namespace sdl_rdp::clipboard::detail::channel {
class ClipboardChannel;
}
namespace sdl_rdp::clipboard::detail::store {
class ClipboardStore;
}

namespace sdl_rdp::clipboard {
using detail::channel::ClipboardChannel;
using detail::store::ClipboardStore;
}
