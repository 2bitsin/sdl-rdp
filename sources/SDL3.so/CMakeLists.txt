set(ENV{SDL_RDP_BUILD_TYPE} "${CMAKE_BUILD_TYPE}")
Init_submodule(PUBLISH_SYMBOLS)
include("${CMAKE_BINARY_DIR}/generated/SDL3/settings.cmake")
