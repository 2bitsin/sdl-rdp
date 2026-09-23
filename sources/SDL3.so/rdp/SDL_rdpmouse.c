#include "SDL_rdpvideo.h"
#include "src/events/SDL_mouse_c.h"

struct SDL_CursorData {
  SDL_Surface* surface;
  int hot_x, hot_y;
};

static void SDL_RDP_FreeCursor(SDL_Cursor* cursor) {
  if (cursor->internal) {
    SDL_DestroySurface(cursor->internal->surface);
    SDL_free(cursor->internal);
  }
  SDL_free(cursor);
}

static SDL_Cursor* SDL_RDP_CreateCursor(SDL_Surface* surface, int hot_x, int hot_y) {
  SDL_Cursor* cursor = SDL_calloc(1, sizeof(*cursor));
  if (!cursor) return NULL;
  cursor->internal = SDL_calloc(1, sizeof(*cursor->internal));
  if (!cursor->internal) {
    SDL_free(cursor);
    return NULL;
  }
  cursor->internal->surface = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_ARGB8888);
  if (!cursor->internal->surface) {
    SDL_RDP_FreeCursor(cursor);
    return NULL;
  }
  cursor->internal->hot_x = hot_x;
  cursor->internal->hot_y = hot_y;
  return cursor;
}

static bool SDL_RDP_ShowCursor(SDL_Cursor* cursor) {
  SDL_VideoData*  data    = SDL_GetVideoDevice()->internal;
  SDL_CursorData* shape   = cursor ? cursor->internal : NULL;
  SDL_Surface*    surface = shape ? shape->surface : NULL;
  int             result  = 0;
  if (!data->handle) return true;
  result =
      data->backend.set_pointer(data->handle, surface ? surface->w : 0, surface ? surface->h : 0,
                                shape ? shape->hot_x : 0, shape ? shape->hot_y : 0, surface ? surface->pixels : NULL);
  return result == 0 || SDL_SetError("%s", data->backend.last_error());
}

void SDL_RDP_InitMouse(void) {
  SDL_Mouse* mouse = SDL_GetMouse();
  mouse->CreateCursor = SDL_RDP_CreateCursor;
  mouse->ShowCursor   = SDL_RDP_ShowCursor;
  mouse->FreeCursor   = SDL_RDP_FreeCursor;
}
