#pragma once
#include "checkedacquisition.hpp"
#include "internals.hpp"
#include <sdl-rdp/utilities/scoped.hpp>
#include <tuple>
namespace sdl3::rdp::sdl::detail::resources {
using sdl_rdp::utilities::RAIIWrap;

auto LockMutex(SDL_Mutex& mutex)                            -> SDL_Mutex&;
auto UnlockMutex(SDL_Mutex& mutex) noexcept                 -> void;
using ScopedMutexLock      = RAIIWrap<SDL_Mutex&, LockMutex, UnlockMutex>;
auto LockProperties(SDL_PropertiesID properties)            -> SDL_PropertiesID;
auto UnlockProperties(SDL_PropertiesID properties) noexcept -> void;
using ScopedPropertiesLock = RAIIWrap<SDL_PropertiesID, LockProperties, UnlockProperties>;
// SDL handles are C pointers; this policy and CheckedAcquisition are the only place their null value is spelled.
template <typename HandleTy> class PointerState {
public:
  static auto IsNull(HandleTy const& value) noexcept -> bool {
    return value == nullptr;
  }
  static auto MakeNull(HandleTy& value) noexcept -> void {
    value = nullptr;
  }
};
template <typename HandleTy, auto ACQUIRE, auto RELEASE>
using Resource = RAIIWrap<HandleTy, CheckedAcquisition<ACQUIRE>{ }, RELEASE, PointerState<HandleTy>::IsNull,
                          PointerState<HandleTy>::MakeNull>;
using Stream           = Resource<SDL_IOStream*, SDL_OpenIO, SDL_CloseIO>;
using Surface          = Resource<SDL_Surface*, SDL_CreateSurface, SDL_DestroySurface>;
using ConvertedSurface = Resource<SDL_Surface*, SDL_ConvertSurface, SDL_DestroySurface>;
using StorageHandle    = Resource<SDL_Storage*, SDL_OpenStorage, SDL_CloseStorage>;
// SDL removes a hint callback by the same name, function and context it was added with.
using HintRegistration  = std::tuple<char const*, SDL_HintCallback, void*>;
auto ObserveHint(char const* name, SDL_HintCallback callback, void* context) -> HintRegistration;
auto ForgetHint(HintRegistration const& registration) noexcept               -> void;
using HintObserver      = RAIIWrap<HintRegistration, ObserveHint, ForgetHint>;
auto AttachTouch(SDL_TouchID touch)                                          -> SDL_TouchID;
auto DetachTouch(SDL_TouchID touch) noexcept                                 -> void;
using TouchRegistration = RAIIWrap<SDL_TouchID, AttachTouch, DetachTouch>;
}

namespace sdl3::rdp::sdl {
using detail::resources::AttachTouch;
using detail::resources::ConvertedSurface;
using detail::resources::DetachTouch;
using detail::resources::ForgetHint;
using detail::resources::HintObserver;
using detail::resources::HintRegistration;
using detail::resources::LockMutex;
using detail::resources::LockProperties;
using detail::resources::ObserveHint;
using detail::resources::Resource;
using detail::resources::ScopedMutexLock;
using detail::resources::ScopedPropertiesLock;
using detail::resources::StorageHandle;
using detail::resources::Stream;
using detail::resources::Surface;
using detail::resources::TouchRegistration;
using detail::resources::UnlockMutex;
using detail::resources::UnlockProperties;
}
