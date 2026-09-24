cmake_minimum_required(VERSION 3.25)
# buildutil #143: C is enabled by C sources in the tree; SDL's are declared by its hook.
project(sdl-rdp C CXX)
include(buildutil)
add_subdirectory(sources)
