set(ENV{SDL_RDP_VERSION} "${BACKEND_OPTION_SDL_VERSION}")
set(ENV{SDL_RDP_BUILD_TYPE} "${CMAKE_BUILD_TYPE}")
Init_submodule(PUBLISH_SYMBOLS)
include("${CMAKE_BINARY_DIR}/generated/SDL3/settings.cmake")
set_target_properties(SDL3 PROPERTIES CXX_STANDARD 23 SOVERSION 0
                      VERSION "${BACKEND_OPTION_SDL_VERSION}" INSTALL_RPATH "$ORIGIN")

Link_dependencies(oxbox::platform)
target_include_directories(SDL3 PRIVATE "${CMAKE_SOURCE_DIR}/sources/sdl-rdp-backend.so")
target_compile_definitions(SDL3 PRIVATE BACKEND_CONTRACTS="${BACKEND_OPTION_CONTRACTS}")
