#include "backend.hpp"
#include <sdl-rdp/SDL3/rdp/exceptions.hpp>
#include <cstddef>
#include <string>

namespace sdl3::rdp::backend::detail::backend {
namespace {
template <std::size_t... INDICES>
auto LoadSymbols(Library const& library, [[maybe_unused]] std::index_sequence<INDICES...> indices) -> BackendSymbols {
  return BackendSymbols{ reinterpret_cast<std::tuple_element_t<INDICES, BackendSymbols>>(
      SDL_LoadFunction(library.Get(), BackendCatalog::Names.at(INDICES)))... };
}
}
Backend::Backend(std::filesystem::path const& path) : _library{ path }, _symbols{ Loaded(_library) } {
  if (auto const found = Call<Operation::VERSION>(); found != SDLRDP_ABI_VERSION)
    throw AbiMismatch{ found, SDLRDP_ABI_VERSION };
  SDL_ClearError();
}
auto Backend::Loaded(Library const& library) -> BackendSymbols {
  auto symbols = LoadSymbols(library, std::make_index_sequence<std::tuple_size_v<BackendSymbols>>{ });
  if (!std::apply([](auto... symbol) { return (... && (symbol != nullptr)); }, symbols))
    throw RelayedFailure{ SDL_GetError() };
  return symbols;
}
auto LoadLibrary(std::filesystem::path const& path) -> SDL_SharedObject* {
  return SDL_LoadObject(path.string().c_str());
}
auto OpenSession(Backend const& backend, sdlrdp_config const& config) -> SessionValue {
  sdlrdp_handle* handle{ };
  if (backend.Call<Operation::OPEN>(&config, &handle) != 0)
    throw RelayedFailure{ backend.Call<Operation::LAST_ERROR>() };
  return { backend, handle };
}
auto CloseSession(SessionValue const& session) noexcept -> void {
  session.first.get().Call<Operation::CLOSE>(session.second);
}
}
