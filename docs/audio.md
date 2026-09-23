# Audio

Select playback with `SDL_AUDIO_DRIVER=rdp`. The single playback device,
"RDP client", starts at 44.1 kHz, 16-bit stereo PCM and switches to the
client’s negotiated rate (44.1 kHz or 48 kHz). Measurements on 2026-09-23 showed that mstsc plays
48 kHz PCM at its 44.1 kHz device rate, making audio fall behind by 8.8 percent
of playing time, so the server offers 44.1 kHz first. SDL converts application
streams to the current device format. `SDL_RDP_AUDIO_LATENCY` controls how far
the server may run ahead of the client’s confirmed playback before it waits, in milliseconds (default 500).
This window guards against a stalled client; the SDL driver paces the stream.
Audio is sent in 20 ms blocks. The driver maintains a real-time
audio clock even when no client is attached, discarding those samples.
The backend does no rate conversion: `sdlrdp_audio_open(handle)` opens playback,
`sdlrdp_audio_rate(handle)` returns the negotiated rate (0 without a playing
client), and `sdlrdp_audio_write` accepts stereo S16 frames at that rate.
`SDLRDP_AUDIO {freq, 1}` announces audio negotiation; `{0, 0}` announces loss
of audio. A client advertising no compatible formats receives no audio; the
sound channel is released, `{0, 0}` is queued, and the audio rate stays zero.
Video, input and clipboard continue on the same connection.
Connection events are never revised after they are queued.
Playback uses FreeRDP’s `SendSamples2` to send PCM directly as Wave2, requiring
rdpsnd version 8 or newer. FreeRDP’s DSP is not used and no wire correction is applied.
The sample logs the device format again when SDL reports a format change.

`SDL_RDP_AUDIO_LEAD` (hint `SDL_HINT_RDP_AUDIO_LEAD`)
sets how much audio the client keeps queued, in milliseconds (default 150).
The driver runs its audio clock ahead by this amount, bursting on client attach
and after a stall to refill the lead, then sending at real-time cadence.
Audio lags the picture by the lead plus the round trip. The lead must be below
the `SDL_RDP_AUDIO_LATENCY` window; zero restores pacing without a lead.

Audio works without initializing video. An audio-only application opens
the same listener using the RDP hints above, with a black desktop at the
configured width and height. Audio and video share a reference-counted
backend handle when both are selected. There is no recording device.

For mstsc, leave Remote audio playback set to **Play on this computer**
(the default). Run the sample with `--tone` for a 440 Hz sine at -12 dBFS;
add `--tight` to exercise audio alongside frame acknowledgement pacing:

    SDL_VIDEO_DRIVER=rdp SDL_AUDIO_DRIVER=rdp sdl-rdp-sample --tone --tight
