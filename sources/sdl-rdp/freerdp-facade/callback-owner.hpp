#pragma once
#include <sdl-rdp/utilities/contract.hpp>

#include <freerdp/freerdp.h>

namespace sdl_rdp::freerdp_facade::detail::callback_owner {
using sdl_rdp::utilities::Expects;

template <class OwnerTy, auto FIELD, class ContextTy> auto CallbackOwner(ContextTy const& context) -> OwnerTy& {
  Expects(context.*FIELD != nullptr, "callback carries its owner");
  return *static_cast<OwnerTy*>(context.*FIELD);
}
template <class Context>
concept ServerContext = requires(Context context) {
  context.custom     = nullptr;
  context.rdpcontext = nullptr;
};
template <ServerContext ContextTy, class OwnerTy>
auto BindContext(ContextTy& context, OwnerTy& owner, rdpContext& session) noexcept -> void {
  context.custom     = &owner;
  context.rdpcontext = &session;
}
}

namespace sdl_rdp::freerdp_facade {
using detail::callback_owner::BindContext;
using detail::callback_owner::CallbackOwner;
}
