Require(FreeRDP          VERSION "3.32.0"       CONAN freerdp)
Require(GTest            VERSION ">=1.17.0"     TEST CONAN gtest COMPONENTS gtest gtest_main)
Require(benchmark        VERSION ">=1.9.0"      BENCH CONAN benchmark)
Require(oxbox            VERSION "0.33.0.359"   CONAN oxbox)
Require(nv-codec-headers VERSION "13.0.19.0"    CONAN nv-codec-headers)
# oxbox pins openssl/3.6.3 exactly; a range here conflicts with it (2bitsin/oxbox#1).
Require(OpenSSL          VERSION "3.6.3"        CONAN openssl)
Scan_subdirectories()
