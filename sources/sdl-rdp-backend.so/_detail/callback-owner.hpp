#pragma once
#include "contract.hpp"

#include <freerdp/freerdp.h>

namespace Backend {
template <class Owner> auto CallbackOwner(void* data) -> Owner& {
  Expects(data != nullptr, "callback carries its owner");
  return *static_cast<Owner*>(data);
}
template <class Context>
concept ServerContext = requires(Context context) {
  context.custom     = nullptr;
  context.rdpcontext = nullptr;
};
template <ServerContext Context> auto BindContext(Context* context, void* owner, rdpContext& session) noexcept -> bool {
  if (!context) return false;
  context->custom     = owner;
  context->rdpcontext = &session;
  return true;
}
}
