Init_submodule(PUBLISH_SYMBOLS)
Link_dependencies(oxbox::platform utilities sdl-rdp-abi sdl-rdp-driver m)
# buildutil #142: SDL's compile options, read by the hook from SDL's compile database.
target_compile_options(SDL3 PRIVATE "$<$<COMPILE_LANGUAGE:C>:@${CMAKE_BINARY_DIR}/generated/SDL3/sdl-c.rsp>"
                                    "$<$<COMPILE_LANGUAGE:CXX>:@${CMAKE_BINARY_DIR}/generated/SDL3/sdl-driver.rsp>")
# buildutil #145: the driver dlopens libsdl-rdp-backend.so, installed beside libSDL3.
set_target_properties(SDL3 PROPERTIES INSTALL_RPATH "$ORIGIN")
