Init_submodule(PUBLISH_SYMBOLS)
Link_dependencies(sdl-rdp-abi utilities freerdp-facade core auth storage session TEST headless-client video)
# buildutil #131: the version script beside main.so.cpp.
target_link_options(sdl-rdp-backend PRIVATE "LINKER:--version-script=${CMAKE_CURRENT_SOURCE_DIR}/exports.map")
# buildutil #131: the version script beside main.so.cpp.
set_property(TARGET sdl-rdp-backend APPEND PROPERTY LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/exports.map")
