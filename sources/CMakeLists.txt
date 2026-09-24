Require(FreeRDP-Server   VERSION ">=3"          SYSTEM)
Require(FreeRDP          VERSION ">=3"          SYSTEM)
Require(WinPR            VERSION ">=3"          SYSTEM)
Require(FreeRDP-Client   VERSION ">=3"          SYSTEM TEST)
Require(GTest            VERSION ">=1.17.0"     TEST CONAN gtest COMPONENTS gtest gtest_main)
Require(oxbox            VERSION "0.32.3.357"   CONAN oxbox)
Require(nv-codec-headers VERSION "13.0.19.0"    CONAN nv-codec-headers)
Require(OpenSSL          VERSION ">=3"          SYSTEM)
# buildutil #130: FreeRDP links the host OpenSSL; libssl is the host's.
find_library(SDLRDP_HOST_SSL NAMES ssl PATHS ${CMAKE_CXX_IMPLICIT_LINK_DIRECTORIES} NO_DEFAULT_PATH REQUIRED)
# buildutil #130: FreeRDP links the host OpenSSL; libcrypto is the host's.
find_library(SDLRDP_HOST_CRYPTO NAMES crypto PATHS ${CMAKE_CXX_IMPLICIT_LINK_DIRECTORIES} NO_DEFAULT_PATH REQUIRED)
# buildutil #130: OpenSSL::SSL binds the host libssl FreeRDP loads.
set_property(TARGET OpenSSL::SSL PROPERTY INTERFACE_LINK_LIBRARIES "${SDLRDP_HOST_SSL}")
# buildutil #130: OpenSSL::Crypto binds the host libcrypto FreeRDP loads.
set_property(TARGET OpenSSL::Crypto PROPERTY INTERFACE_LINK_LIBRARIES "${SDLRDP_HOST_CRYPTO}")
# buildutil #144: the project is C++23; the profile's cppstd would make it C++26.
set(CMAKE_CXX_STANDARD 23)
Scan_subdirectories()
