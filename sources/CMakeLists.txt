Require(FreeRDP          VERSION "3.32.0-sdl-rdp.3" CONAN freerdp PLATFORM Linux)
# Shipped code encodes AVC420 only through NVENC, so the archives need no openh264; Linux keeps it for the rigs.
Require(FreeRDP          VERSION "3.32.0-sdl-rdp.3" CONAN freerdp PLATFORM Windows Macos OPTIONS with_openh264=False)
Require(GTest            VERSION ">=1.17.0"         TEST CONAN gtest COMPONENTS gtest gtest_main)
Require(benchmark        VERSION ">=1.9.0"          BENCH CONAN benchmark)
Require(oxbox            VERSION "0.36.2.379"       CONAN oxbox)
Require(nv-codec-headers VERSION "13.0.19.0"        CONAN nv-codec-headers)
Require(OpenSSL          VERSION "3.6.3"            CONAN openssl PLATFORM Linux) # oxbox pins it exactly (2bitsin/oxbox#1)
# The shipped libssl reads openssl.cnf from openssldir, so it is a path only an administrator can write.
Require(OpenSSL          VERSION "3.6.3"            CONAN openssl PLATFORM Windows
        OPTIONS "openssldir=C:/Program Files/Common Files/SSL" no_module=True)
Require(OpenSSL          VERSION "3.6.3"            CONAN openssl PLATFORM Macos
        OPTIONS openssldir=/private/etc/ssl no_module=True)
Scan_subdirectories()
