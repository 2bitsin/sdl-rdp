#pragma once
#include "SDL_rdpboundary.hpp"
#include "_detail/scoped.hpp"
#include <filesystem>
#include <functional>
#include <stdexcept>
#include <tuple>
namespace rdp {
auto LockMutex(SDL_Mutex& mutex)                 -> SDL_Mutex&;
void UnlockMutex(SDL_Mutex& mutex)                 noexcept;
using ScopedMutexLock      = utilities::RAIIWrap<SDL_Mutex&, LockMutex, UnlockMutex>;
auto LockProperties(SDL_PropertiesID properties) -> SDL_PropertiesID;
void UnlockProperties(SDL_PropertiesID properties) noexcept;
using ScopedPropertiesLock = utilities::RAIIWrap<SDL_PropertiesID, LockProperties, UnlockProperties>;
// SDL handles are C pointers; these two policies are the only place their null value is spelled.
template<typename _Handle, auto _Projection = std::identity{ }>
class PointerState {
public:
  static auto IsNull(_Handle const& value) noexcept -> bool { return std::invoke(_Projection, value) == nullptr; }
  static void MakeNull(_Handle& value) noexcept { std::invoke(_Projection, value) = nullptr; }
};
template<auto _Acquire>
class CheckedAcquisition {
public:
  template<typename... _Args> requires std::invocable<decltype(_Acquire), _Args...>
  auto operator()(_Args&&... args) const {
    auto value = std::invoke(_Acquire, std::forward<_Args>(args)...);
    if (!value) throw std::runtime_error(SDL_GetError());
    return value;
  }
};
template<typename _Handle, auto _Acquire, auto _Release>
using Resource = utilities::RAIIWrap<_Handle, CheckedAcquisition<_Acquire>{ }, _Release,
    PointerState<_Handle>::IsNull, PointerState<_Handle>::MakeNull>;
using Stream            = Resource<SDL_IOStream*, SDL_OpenIO, SDL_CloseIO>;
using Surface           = Resource<SDL_Surface*, SDL_CreateSurface, SDL_DestroySurface>;
using ConvertedSurface  = Resource<SDL_Surface*, SDL_ConvertSurface, SDL_DestroySurface>;
using StorageHandle     = Resource<SDL_Storage*, SDL_OpenStorage, SDL_CloseStorage>;
// SDL removes a hint callback by the same name, function and context it was added with.
using HintRegistration  = std::tuple<char const*, SDL_HintCallback, void*>;
auto ObserveHint(char const* name, SDL_HintCallback callback, void* context) -> HintRegistration;
void ForgetHint(HintRegistration const& registration) noexcept;
using HintObserver      = utilities::RAIIWrap<HintRegistration, ObserveHint, ForgetHint>;
auto AttachTouch(SDL_TouchID touch)                                          -> SDL_TouchID;
void DetachTouch(SDL_TouchID touch)                   noexcept;
using TouchRegistration = utilities::RAIIWrap<SDL_TouchID, AttachTouch, DetachTouch>;
// SDL_LoadFile returns an SDL-allocated C buffer that SDL_free releases.
auto LoadFile(std::filesystem::path const& path)                             -> char*;
using LoadedFile = utilities::RAIIWrap<char*, LoadFile, SDL_free, PointerState<char*>::IsNull,
                                       PointerState<char*>::MakeNull>;
}
