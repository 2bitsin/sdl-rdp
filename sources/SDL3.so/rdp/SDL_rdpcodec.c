#include "SDL_rdpvideo.h"

static const struct
{
    const char *name;
    sdlrdp_codec codec;
} SDL_RDP_codecs[] = {
    { "auto", SDLRDP_CODEC_AUTO },
    { "planar", SDLRDP_CODEC_PLANAR },
    { "remotefx", SDLRDP_CODEC_REMOTEFX },
    { "nscodec", SDLRDP_CODEC_NSCODEC },
    { "raw", SDLRDP_CODEC_RAW }
};

bool SDL_RDP_ParseCodec(const char *name, sdlrdp_codec *codec)
{
    unsigned i;
    if (!name) {
        name = "auto";
    }
    for (i = 0; i < SDL_arraysize(SDL_RDP_codecs); ++i) {
        if (SDL_strcmp(name, SDL_RDP_codecs[i].name) == 0) {
            *codec = SDL_RDP_codecs[i].codec;
            return true;
        }
    }
    return SDL_SetError("Invalid RDP codec: %s", name);
}

const char *SDL_RDP_CodecName(sdlrdp_codec codec)
{
    unsigned i;
    for (i = 0; i < SDL_arraysize(SDL_RDP_codecs); ++i) {
        if (SDL_RDP_codecs[i].codec == codec) {
            return SDL_RDP_codecs[i].name;
        }
    }
    SDL_assert(!"invalid RDP codec");
    return "unknown";
}
