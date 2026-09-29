"""The socket layer's Winsock calls (socket.win32.cpp, socket-library.win32.cpp) link ws2_32 on Windows."""
import buildutil_configure as bc

if bc.target_system() == "Windows":
    bc.link("ws2_32")
