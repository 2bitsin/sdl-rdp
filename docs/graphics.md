# Graphics pipeline

Clients that negotiate MS-RDPEGFX use `auto` by default: AVC420 when the encoder
is available and the confirmed client capabilities allow AVC420, otherwise progressive.
`planar` and `raw` select exact RGB transport; `progressive`, `remotefx`, and `nscodec`
select progressive on the pipeline. RemoteFX and NSCodec remain legacy choices.
Clients without GFX, with a rejected channel, or without confirmation within
three seconds of activation use legacy codec selection. `SDL_PROP_WINDOW_RDP_CODEC_STRING`
on the window reports the negotiated codec, set when a client attaches and on every change.

Progressive encodes damaged tiles with FreeRDP's single-pass RemoteFX encoder,
without refinement passes. SYNC/CONTEXT headers are sent once per surface.
Graphics-pipeline encoding runs outside the session lock; legacy `SendFrame`
encoding for clients without the graphics pipeline remains under it.
Resize replaces the context and surface and sends a full picture without deactivating the
session; pointer PDUs continue.

At most two frames await acknowledgement. This project's byte-budget rule also
limits pending payload bytes + nonzero client `queueDepth` + next payload to
max(64 KiB, twice the larger of the last and next payloads), excluding TLS/DVC
headers. With no outstanding frames, one probe is allowed for stale backlog
reports. The suspend value clears outstanding frames and disables waiting until
another acknowledgement arrives. Capabilities are logged at INFO once per peer;
`SDL_LOGGING=video=info` shows them in SDL applications.
At disconnect, the INFO `Frames:` line reports sent and coalesced frames, mean and
maximum encode and acknowledgement times, and the count of acknowledgements over
100 ms. Connections that sent AVC420 frames also report mean conversion, input
upload, and NVENC times for those frames; the adjacent `Audio:` line, when a sound channel exists, reports blocks
sent, mean and maximum gaps between sends, and the count of gaps over 40 ms.

| Client | Progressive | AVC420 | AVC444 |
|---|---|---|---|
| Windows 10/11 mstsc | Yes | Yes | Yes, including v2 |
| macOS Windows App / Microsoft Remote Desktop | Yes | Yes | Unverified |
| iOS / Android Windows App | Assumed; unverified | Unsupported in xrdp tests | Unsupported in xrdp tests |
| FreeRDP 3.15 | Yes | Yes | Yes |

`SDL_RDP_CODEC=avc420` explicitly selects H.264 AVC420 over RDPGFX, including
in the sample. The server needs an NVIDIA GPU and a driver providing NVENC
(SDK 13 headers require driver 570 or newer). CUDA and NVENC are loaded at runtime;
no NVIDIA library is linked. Missing libraries, unavailable hardware, a surface
below NVENC's minimum picture size, or client capabilities without AVC420 cause
a fallback to progressive, with one INFO diagnostic per connection.

`SDL_RDP_AVC_BITRATE` sets the bitrate in kbit/s. Zero (the default) means
16000 kbit/s at 1920x1080, scaled by the real surface pixel count with a
2000 kbit/s floor. An explicit nonzero bitrate is used as given. The server
encodes one access unit per frame, with no B frames or reorder delay. CPU conversion
uses BT.709 full-range I420. Pictures are padded to multiples of 16 by repeating
the last column and row; only the real damage is drawn. Padding does not scale
the picture. Resize and switching into AVC420 force an IDR with SPS/PPS.

| RDPGFX codec | Selection | Encoder |
|---|---|---|
| Progressive | `progressive`; `auto` without the encoder or when the client disallows AVC420 | FreeRDP RemoteFX |
| AVC420 | `avc420`; `auto` with the encoder when the client allows AVC420 | NVIDIA NVENC H.264, lossy 4:2:0 |
| Planar | Explicit `planar` | Lossless RGB |
| Raw | Explicit `raw` | Uncompressed RGB |

Every surface update or renderer present hands the damaged rectangles to the
encoder.
