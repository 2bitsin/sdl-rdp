Init_submodule()
Link_dependencies(freerdp-server freerdp winpr oxbox::platform utilities)
# buildutil #128: Link_dependencies records a SYSTEM package as a conan requirement in the manifest.
target_link_libraries(sdl-rdp-freerdp-facade PUBLIC OpenSSL::SSL OpenSSL::Crypto)
