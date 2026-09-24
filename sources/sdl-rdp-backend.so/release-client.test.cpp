#include "_detail/release-client.hpp"

#include <freerdp/gdi/gdi.h>

namespace Headless {
auto ReleaseClient::operator()(freerdp* instance) const -> void {
  freerdp_disconnect(instance);
  gdi_free(instance);
  freerdp_context_free(instance);
  freerdp_free(instance);
}
}
