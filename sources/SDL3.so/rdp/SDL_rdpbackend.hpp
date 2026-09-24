#pragma once
#include "SDL_rdpresources.hpp"
#include "SDL_rdpoperations.generated.hpp"
#include <filesystem>
#include <tuple>
namespace rdp {
using BackendSymbols = BackendCatalog::Symbols;
static_assert(std::tuple_size_v<BackendSymbols> == std::to_underlying(Operation::COUNT));
static_assert(BackendCatalog::Names.size() == std::tuple_size_v<BackendSymbols>);
template <Operation _Operation, typename... _Args>
concept BackendOperation = std::invocable<std::tuple_element_t<std::to_underlying(_Operation), BackendSymbols>,
                                          _Args...>;
auto LoadLibrary(std::filesystem::path const& path) -> SDL_SharedObject*;
using Library = Resource<SDL_SharedObject*, LoadLibrary, SDL_UnloadObject>;
class Backend {
public:
  explicit Backend(std::filesystem::path const& path);
  template <Operation _Operation, typename... _Args>
    requires BackendOperation<_Operation, _Args...>
  auto Call(_Args&&... args) const -> decltype(auto) {
    return std::get<std::to_underlying(_Operation)>(_symbols)(std::forward<_Args>(args)...);
  }
private:
  static auto _Load(Library const& library) -> BackendSymbols;
  Library const        _library;
  BackendSymbols const _symbols;
};
using SessionValue = std::pair<std::reference_wrapper<Backend const>, sdlrdp_handle*>;
auto OpenSession(Backend const& backend, sdlrdp_config const& config) -> SessionValue;
auto CloseSession(SessionValue const& session) noexcept               -> void;
using SessionState = PointerState<SessionValue, &SessionValue::second>;
using Session = utilities::RAIIWrap<SessionValue, OpenSession, CloseSession, SessionState::IsNull,
                                    SessionState::MakeNull>;
}
