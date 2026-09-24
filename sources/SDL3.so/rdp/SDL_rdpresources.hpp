#pragma once
#include "SDL_rdpboundary.hpp"
#include "SDL_rdpcheckedacquisition.hpp"
#include "SDL_rdppointerstate.hpp"
#include "_detail/scoped.hpp"
#include <filesystem>
#include <tuple>
namespace rdp {
auto LockMutex(SDL_Mutex& mutex)                            -> SDL_Mutex&;
auto UnlockMutex(SDL_Mutex& mutex) noexcept                 -> void;
using ScopedMutexLock      = utilities::RAIIWrap<SDL_Mutex&, LockMutex, UnlockMutex>;
auto LockProperties(SDL_PropertiesID properties)            -> SDL_PropertiesID;
auto UnlockProperties(SDL_PropertiesID properties) noexcept -> void;
using ScopedPropertiesLock = utilities::RAIIWrap<SDL_PropertiesID, LockProperties, UnlockProperties>;
template <typename _Handle, auto _Acquire, auto _Release>
using Resource = utilities::RAIIWrap<_Handle, CheckedAcquisition<_Acquire>{ }, _Release, PointerState<_Handle>::IsNull,
                                     PointerState<_Handle>::MakeNull>;
using Stream            = Resource<SDL_IOStream*, SDL_OpenIO, SDL_CloseIO>;
using Surface           = Resource<SDL_Surface*, SDL_CreateSurface, SDL_DestroySurface>;
using ConvertedSurface  = Resource<SDL_Surface*, SDL_ConvertSurface, SDL_DestroySurface>;
using StorageHandle     = Resource<SDL_Storage*, SDL_OpenStorage, SDL_CloseStorage>;
// SDL removes a hint callback by the same name, function and context it was added with.
using HintRegistration  = std::tuple<char const*, SDL_HintCallback, void*>;
auto ObserveHint(char const* name, SDL_HintCallback callback, void* context) -> HintRegistration;
auto ForgetHint(HintRegistration const& registration) noexcept               -> void;
using HintObserver      = utilities::RAIIWrap<HintRegistration, ObserveHint, ForgetHint>;
auto AttachTouch(SDL_TouchID touch)                                          -> SDL_TouchID;
auto DetachTouch(SDL_TouchID touch) noexcept                                 -> void;
using TouchRegistration = utilities::RAIIWrap<SDL_TouchID, AttachTouch, DetachTouch>;
// SDL_LoadFile returns an SDL-allocated C buffer that SDL_free releases.
auto LoadFile(std::filesystem::path const& path)                             -> char*;
using LoadedFile = utilities::RAIIWrap<char*, LoadFile, SDL_free, PointerState<char*>::IsNull,
                                       PointerState<char*>::MakeNull>;
}
