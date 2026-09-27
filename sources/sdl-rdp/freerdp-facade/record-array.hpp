#pragma once
#include <sdl-rdp/utilities/contract.hpp>

#include <cstddef>
#include <span>
#include <type_traits>

namespace sdl_rdp::freerdp_facade::detail::record_array {
using sdl_rdp::utilities::Expects;

// A C record lends an array as a first-element field and a count field; a nonzero count covers a pointer.
template <auto ITEMS, auto COUNT, class RecordTy>
auto RecordArray(RecordTy const& record)
    -> std::span<std::remove_pointer_t<std::remove_cvref_t<decltype(record.*ITEMS)>> const> {
  auto const* const items = record.*ITEMS;
  auto const        count = std::size_t{ record.*COUNT };
  if (count) Expects(items != nullptr, "a record's array covers its count");
  return { items, count };
}
}

namespace sdl_rdp::freerdp_facade {
using detail::record_array::RecordArray;
}
