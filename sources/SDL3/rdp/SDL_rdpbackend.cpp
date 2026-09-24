#include "SDL_rdpbackend.hpp"
#include <stdexcept>
#include <string>

namespace rdp {
namespace {
template <std::size_t... _Indices>
auto LoadSymbols(Library const& library, [[maybe_unused]] std::index_sequence<_Indices...> indices) -> BackendSymbols {
  return BackendSymbols{ reinterpret_cast<std::tuple_element_t<_Indices, BackendSymbols>>(
      SDL_LoadFunction(library.Get(), BackendCatalog::Names.at(_Indices)))... };
}
}
Backend::Backend(std::filesystem::path const& path) : _library{ path }, _symbols{ _Load(_library) } {
  if (Call<Operation::VERSION>() != SDLRDP_ABI_VERSION)
    throw std::runtime_error("RDP backend ABI version mismatch (expected " + std::to_string(SDLRDP_ABI_VERSION) + ")");
  SDL_ClearError();
}
auto Backend::_Load(Library const& library) -> BackendSymbols {
  auto symbols = LoadSymbols(library, std::make_index_sequence<std::tuple_size_v<BackendSymbols>>{ });
  if (!std::apply([](auto... symbol) { return (... && (symbol != nullptr)); }, symbols))
    throw std::runtime_error(SDL_GetError());
  return symbols;
}
auto LoadLibrary(std::filesystem::path const& path) -> SDL_SharedObject* {
  return SDL_LoadObject(path.string().c_str());
}
auto OpenSession(Backend const& backend, sdlrdp_config const& config) -> SessionValue {
  sdlrdp_handle* handle{ };
  if (backend.Call<Operation::OPEN>(&config, &handle) != 0)
    throw std::runtime_error(backend.Call<Operation::LAST_ERROR>());
  return { backend, handle };
}
auto CloseSession(SessionValue const& session) noexcept -> void {
  session.first.get().Call<Operation::CLOSE>(session.second);
}
}
