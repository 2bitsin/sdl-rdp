#pragma once
#include <concepts>
#include <cstddef>
#include <type_traits>

namespace sdl_rdp::utilities::detail::void_buffer {
template <class VoidTy>
concept VoidBuffer = std::same_as<VoidTy, void> || std::same_as<VoidTy, void const>;
// The bytes a C transfer buffer holds: written from when const, read into otherwise.
template <VoidBuffer VoidTy> using BytesOf = std::conditional_t<std::is_const_v<VoidTy>, std::byte const, std::byte>;
template <class ByteTy>
concept ByteBuffer = std::same_as<std::remove_const_t<ByteTy>, std::byte>;
}

namespace sdl_rdp::utilities {
using detail::void_buffer::ByteBuffer;
using detail::void_buffer::BytesOf;
using detail::void_buffer::VoidBuffer;
}
