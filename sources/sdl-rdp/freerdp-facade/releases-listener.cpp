#include <sdl-rdp/freerdp-facade/releases-listener.hpp>

namespace Backend {
auto ReleasesListener::operator()(freerdp_listener* listener) const -> void {
  listener->Close(listener);
  freerdp_listener_free(listener);
}
}
