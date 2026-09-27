#pragma once
#include <sdl-rdp/utilities/contract.hpp>

namespace sdl_rdp::freerdp_facade::detail::callback_owner {
using sdl_rdp::utilities::Expects;

template <class OwnerTy, auto FIELD, class ContextTy> auto CallbackOwner(ContextTy const& context) -> OwnerTy& {
  Expects(context.*FIELD != nullptr, "callback carries its owner");
  return *static_cast<OwnerTy*>(context.*FIELD);
}
}

namespace sdl_rdp::freerdp_facade {
using detail::callback_owner::CallbackOwner;
}
