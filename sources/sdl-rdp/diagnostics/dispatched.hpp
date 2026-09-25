#pragma once
#include <sdl-rdp/diagnostics/failure-log.hpp>
#include <sdl-rdp/utilities/contained.hpp>
#include <sdl-rdp/utilities/contract.hpp>

#include <cstdint>
#include <functional>

namespace Backend {
template <auto HANDLER, class OwnerTy, class PduTy>
  requires std::invocable<decltype(HANDLER), OwnerTy&, PduTy const&>
auto Dispatched(std::uint32_t failure, OwnerTy& owner, PduTy const* pdu, FailureLog const& failures) noexcept
    -> std::uint32_t {
  Expects(pdu != nullptr, "the channel PDU is supplied");
  return Contained(failure, [&] { return std::invoke(HANDLER, owner, *pdu); }, failures);
}
}
