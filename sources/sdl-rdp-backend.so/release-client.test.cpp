#include "_detail/release-client.hpp"

#include <freerdp/gdi/gdi.h>

namespace Headless {
void ReleaseClient::operator()(freerdp* instance) const {
  freerdp_disconnect(instance);
  gdi_free(instance);
  freerdp_context_free(instance);
  freerdp_free(instance);
}
}
