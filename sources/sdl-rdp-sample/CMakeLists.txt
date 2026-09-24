Init_submodule()
Link_dependencies(SDL3 TEST sample-gate headless-client utilities video)
# buildutil #128: Link_dependencies records a SYSTEM package as a conan requirement in the manifest.
target_link_libraries(sdl-rdp-sample PRIVATE OpenSSL::Crypto)
