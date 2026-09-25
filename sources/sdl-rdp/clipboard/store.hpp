#pragma once
#include <sdl-rdp/utilities/generational.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace sdl_rdp::clipboard::detail::store {
using sdl_rdp::utilities::Generational;

class ClipboardStore : private Generational<std::string> {
public:
  using Generational::Generation;
  auto Replace(std::string value) -> std::uint64_t;
  auto Text() const noexcept      -> std::string const&;
  auto Unicode() const noexcept   -> std::span<std::byte const>;

private:
  std::vector<std::byte> _unicode{ std::byte{ 0 }, std::byte{ 0 } };
};
}

namespace sdl_rdp::clipboard {
using detail::store::ClipboardStore;
}
