#pragma once

#include <concepts>
#include <freerdp/channels/cliprdr.h>

namespace Backend {
template <std::invocable<CLIPRDR_CAPABILITIES const*> Send>
auto SendGeneralCapabilities(Send send) {
  CLIPRDR_GENERAL_CAPABILITY_SET general{ CB_CAPSTYPE_GENERAL, CB_CAPSTYPE_GENERAL_LEN, CB_CAPS_VERSION_2,
                                          CB_USE_LONG_FORMAT_NAMES };
  CLIPRDR_CAPABILITIES           caps   { .common = { .msgType = CB_CLIP_CAPS } };
  caps.cCapabilitiesSets = 1;
  caps.capabilitySets    = reinterpret_cast<CLIPRDR_CAPABILITY_SET*>(&general);
  return send(&caps);
}
}
