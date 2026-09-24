#pragma once
// SDL's internal C declarations need the same linkage wrapper as its Haiku backend.
extern "C" {
#include "SDL_internal.h"
#include "src/SDL_hints_c.h"
#include "src/SDL_properties_c.h"
#include "src/audio/SDL_sysaudio.h"
#include "src/events/SDL_clipboardevents_c.h"
#include "src/events/SDL_keyboard_c.h"
#include "src/events/SDL_mouse_c.h"
#include "src/events/SDL_touch_c.h"
#include "src/events/SDL_windowevents_c.h"
#include "src/storage/SDL_sysstorage.h"
#include "src/video/SDL_clipboard_c.h"
#include "src/video/SDL_sysvideo.h"
}
#include <sdl-rdp/utilities/contract.hpp>
#include <sdl-rdp-abi/sdl-rdp-backend.h>
