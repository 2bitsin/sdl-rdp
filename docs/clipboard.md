# Clipboard

Clipboard text travels both ways through SDL's `SDL_SetClipboardText`,
`SDL_GetClipboardText`, and `SDL_HasClipboardText`. Remote changes arrive as
`SDL_EVENT_CLIPBOARD_UPDATE`. The sample accepts `--clip "hello"` and prints
`event CLIPBOARD text=<text>` on updates. UTF-8 text is transferred as
`CF_UNICODETEXT` (UTF-16LE); the server also announces `CF_TEXT`
with ASCII fallback (`?` for non-ASCII characters).
Clipboard redirection must be enabled in the RDP client (`/clipboard` in
xfreerdp). Images and files are not supported yet.

`SDL_GetClipboardText` returns a copy the application owns, as SDL specifies; a
remote change reaches the application only as `SDL_EVENT_CLIPBOARD_UPDATE`.
