# Drives

Share a folder in the RDP client: `/drive:share,/path/to/folder` in xfreerdp,
**Local resources, Drives** in mstsc, or **Folders** in the Mac client.
The application on the box initiates every file operation against that share.

For whole files, set `SDL_HINT_STORAGE_TITLE_DRIVER` to `"rdp"`, then call
`SDL_OpenTitleStorage("share", 0)` and `SDL_StorageReady`. An empty override
selects the first shared drive. This title storage supports reads and writes,
directory enumeration, metadata, mkdir, remove, rename and copy. Remaining
space is reported as zero (unknown). `SDL_HINT_STORAGE_USER_DRIVER="rdp"`
also selects it; the application argument names the drive and the organization
argument is unused. Storage uses the same backend listener as video and audio.

For random access, get `SDL_PROP_DISPLAY_RDP_OPEN_FILE_POINTER` from
`SDL_GetDisplayProperties(SDL_GetPrimaryDisplay())`. Its type is:

```c
typedef SDL_IOStream *(SDLCALL *RDP_OpenFile)(const char *drive,
                                           const char *path,
                                           const char *mode);
```

Call it with `("share", "disk.img", "r+b")`, then use `SDL_SeekIO`,
`SDL_ReadIO`, `SDL_WriteIO`, `SDL_GetIOSize` and `SDL_CloseIO` normally.
Modes follow `SDL_IOFromFile`; paths are UTF-8 with `/`. Operations block
until the client responds or disconnects. There is no cache and no FUSE yet.
Flush waits for a metadata round trip after acknowledged writes; it does not
promise that the client's operating system has flushed physical media.

`SDL_PROP_DISPLAY_RDP_DRIVES_STRING` contains drive names separated by newlines,
updated when the application pumps SDL events. There is no native SDL signal
for drive changes and no custom event; read the property when needed.
Disconnected streams fail; reconnecting clients receive fresh drive IDs.
Close streams before shutting down SDL, and coordinate a stream's position and
lifetime when sharing it between application threads.

Backend ABI version 6 adds `SDLRDP_DRIVE` announcements and `sdlrdp_drive_*`.
Enumeration uses an entry offset: add the returned count to the offset for the
next page. A directory changed between calls may reorder entries. Modification
times are Unix seconds. Drive names have a 511-byte UTF-8 limit and entry names
1023 bytes. Each read/write ABI call accepts at most `INT_MAX` bytes, with
64-bit file offsets, and sends up to eight 64 KiB requests concurrently.
Calls from different threads can be outstanding together. The peer owns the
static channel; transport loss wakes all waiters and removes its drives.

FreeRDP 3.15's server `DriveReadFile`/`DriveWriteFile` wrappers expose 32-bit
offsets and a private reader thread. This backend instead pumps MS-RDPEFS
packets through FreeRDP's WTS channel on the peer and owns completion IDs,
preserving 64-bit offsets and avoiding races with directory continuations.
The 64 KiB chunk size is this backend's transfer limit, not a negotiated
client maximum; FreeRDP 3.15's drive reader accepts a 32-bit Length field
without an explicit smaller read cap.

The sample accepts `--ls share[/path]`, `--cat share/path` and
`--write share/path`. The last command writes a 1 MiB pattern (`i % 251`)
at offsets zero and 2 MiB. The cat command prints a SHA-256 via OpenSSL.
