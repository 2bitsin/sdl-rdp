#include "SDL_rdpdrive.h"

typedef struct RDP_File {
  SDL_RDP_Backend backend;
  sdlrdp_handle*  handle;
  sdlrdp_file*    file;
  Sint64          position;
  bool            append;
} RDP_File;

static bool RDP_DriveListValid(SDL_RDP_Backend* backend, int count, unsigned capacity) {
  if (count < 0) return SDL_SetError("%s", backend->last_error());
  if ((unsigned)count < capacity) return true;
  if (capacity > SDL_MAX_SINT32 / 2 / sizeof(sdlrdp_drive)) return SDL_SetError("Too many RDP drives");
  return true;
}

static sdlrdp_drive* RDP_DriveList(SDL_RDP_Backend* backend, sdlrdp_handle* handle, int* count) {
  unsigned      capacity = 16;
  sdlrdp_drive* drives   = NULL;
  for (;;) {
    sdlrdp_drive* grown = SDL_realloc(drives, capacity * sizeof(*drives));
    if (!grown) {
      SDL_free(drives);
      return NULL;
    }
    drives = grown;
    *count = backend->drive_list(handle, drives, capacity);
    if (!RDP_DriveListValid(backend, *count, capacity)) {
      SDL_free(drives);
      return NULL;
    }
    if ((unsigned)*count < capacity) return drives;
    capacity *= 2;
  }
}

bool SDL_RDP_FindDrive(SDL_RDP_Backend* backend, sdlrdp_handle* handle, char const* name, unsigned* id) {
  int           count  = 0;
  sdlrdp_drive* drives = RDP_DriveList(backend, handle, &count);
  if (!drives) return false;
  for (int i = 0; i < count; ++i) {
    if (!name || !*name || !SDL_strcmp(name, drives[i].name)) {
      *id = drives[i].id;
      SDL_free(drives);
      return true;
    }
  }
  SDL_free(drives);
  return SDL_SetError("RDP drive unavailable: %s", name ? name : "");
}

static Sint64 SDLCALL RDP_FileSize(void* userdata) {
  RDP_File*   file = userdata;
  sdlrdp_stat info;
  if (file->backend.drive_fstat(file->handle, file->file, &info) < 0) {
    SDL_SetError("%s", file->backend.last_error());
    return -1;
  }
  if (info.size > SDL_MAX_SINT64) {
    SDL_SetError("RDP file exceeds signed stream size");
    return -1;
  }
  return (Sint64)info.size;
}

static Sint64 SDLCALL RDP_FileSeek(void* userdata, Sint64 offset, SDL_IOWhence whence) {
  RDP_File* file = userdata;
  Sint64    base = 0;
  if (whence == SDL_IO_SEEK_CUR)
    base = file->position;
  else if (whence == SDL_IO_SEEK_END)
    base = RDP_FileSize(file);
  else if (whence != SDL_IO_SEEK_SET)
    base = -1;
  if (base < 0 || offset < -base || offset > SDL_MAX_SINT64 - base) {
    SDL_SetError("Invalid RDP file seek");
    return -1;
  }
  file->position = base + offset;
  return file->position;
}

static size_t RDP_FileResult(RDP_File* file, int count, size_t size, SDL_IOStatus* status, SDL_IOStatus short_status) {
  if (count < 0) {
    *status = SDL_IO_STATUS_ERROR;
    SDL_SetError("%s", file->backend.last_error());
    return 0;
  }
  file->position += count;
  if ((size_t)count < size) *status = short_status;
  return count;
}

static size_t SDLCALL RDP_FileRead(void* userdata, void* buffer, size_t size, SDL_IOStatus* status) {
  RDP_File* file = userdata;
  int count = file->backend.drive_read(file->handle, file->file, file->position, buffer, SDL_min(size, SDL_MAX_SINT32));
  return RDP_FileResult(file, count, size, status, SDL_IO_STATUS_EOF);
}

static size_t SDLCALL RDP_FileWrite(void* userdata, void const* buffer, size_t size, SDL_IOStatus* status) {
  RDP_File* file = userdata;
  if (file->append && RDP_FileSeek(file, 0, SDL_IO_SEEK_END) < 0) {
    *status = SDL_IO_STATUS_ERROR;
    return 0;
  }
  int count =
      file->backend.drive_write(file->handle, file->file, file->position, buffer, SDL_min(size, SDL_MAX_SINT32));
  return RDP_FileResult(file, count, size, status, SDL_IO_STATUS_ERROR);
}

static bool SDLCALL RDP_FileFlush(void* userdata, SDL_IOStatus* status) {
  RDP_File* file = userdata;
  if (file->backend.drive_flush(file->handle, file->file) >= 0) return true;
  *status = SDL_IO_STATUS_ERROR;
  return SDL_SetError("%s", file->backend.last_error());
}

static bool SDLCALL RDP_FileClose(void* userdata) {
  RDP_File* file = userdata;
  bool      ok   = true;
  if (file->file && file->backend.drive_close(file->handle, file->file) < 0)
    ok = SDL_SetError("%s", file->backend.last_error());
  if (file->handle) SDL_RDP_ReleaseBackend();
  SDL_free(file);
  return ok;
}

static unsigned RDP_FileMode(char const* mode) {
  unsigned flags = 0;
  if (!mode || !*mode) return 0;
  if (*mode == 'r')
    flags = SDLRDP_FILE_READ;
  else if (*mode == 'w')
    flags = SDLRDP_FILE_WRITE | SDLRDP_FILE_CREATE | SDLRDP_FILE_TRUNCATE;
  else if (*mode == 'a')
    flags = SDLRDP_FILE_WRITE | SDLRDP_FILE_CREATE;
  else
    return 0;
  for (char const* p = mode + 1; *p; ++p) {
    if (*p == '+')
      flags |= SDLRDP_FILE_READ | SDLRDP_FILE_WRITE;
    else if (*p != 'b')
      return 0;
  }
  return flags;
}

SDL_IOStream* SDLCALL SDL_RDP_OpenFile(char const* drive, char const* path, char const* mode) {
  unsigned              id     = 0;
  unsigned              flags  = RDP_FileMode(mode);
  SDL_IOStreamInterface iface  = { 0 };
  SDL_IOStream*         stream = NULL;
  if (!flags || !path) {
    SDL_SetError("Invalid RDP file path or mode");
    return NULL;
  }
  RDP_File* file = SDL_calloc(1, sizeof(*file));
  if (!file) return NULL;
  if (!SDL_RDP_AcquireBackend(&file->backend, &file->handle, NULL) ||
      !SDL_RDP_FindDrive(&file->backend, file->handle, drive, &id)) {
    RDP_FileClose(file);
    return NULL;
  }
  if (file->backend.drive_open(file->handle, id, path, flags, &file->file) < 0) {
    SDL_SetError("%s", file->backend.last_error());
    RDP_FileClose(file);
    return NULL;
  }
  file->append  = *mode == 'a';
  iface.version = sizeof(iface);
  iface.size    = RDP_FileSize;
  iface.seek    = RDP_FileSeek;
  iface.read    = flags & SDLRDP_FILE_READ ? RDP_FileRead : NULL;
  iface.write   = flags & SDLRDP_FILE_WRITE ? RDP_FileWrite : NULL;
  iface.flush   = RDP_FileFlush;
  iface.close   = RDP_FileClose;
  stream        = SDL_OpenIO(&iface, file);
  if (!stream) RDP_FileClose(file);
  return stream;
}

void SDL_RDP_UpdateDrives(SDL_RDP_Backend* backend, sdlrdp_handle* handle, SDL_PropertiesID props) {
  int           count  = 0;
  sdlrdp_drive* drives = RDP_DriveList(backend, handle, &count);
  if (!drives) return;
  size_t capacity = 1;
  for (int i = 0; i < count; ++i)
    capacity += SDL_strlen(drives[i].name) + 1;
  char* names = SDL_calloc(1, capacity);
  if (!names) {
    SDL_free(drives);
    return;
  }
  for (int i = 0; i < count; ++i) {
    if (i) SDL_strlcat(names, "\n", capacity);
    SDL_strlcat(names, drives[i].name, capacity);
  }
  SDL_SetStringProperty(props, SDL_PROP_DISPLAY_RDP_DRIVES_STRING, names);
  SDL_SetPointerProperty(props, SDL_PROP_DISPLAY_RDP_OPEN_FILE_POINTER, (void*)SDL_RDP_OpenFile);
  SDL_free(names);
  SDL_free(drives);
}
