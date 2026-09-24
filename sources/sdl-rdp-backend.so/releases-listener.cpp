#include "_detail/releases-listener.hpp"

namespace Backend {
void ReleasesListener::operator()(freerdp_listener* listener) const {
  listener->Close(listener);
  freerdp_listener_free(listener);
}
}
