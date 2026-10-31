# Prime-X: the build lives in xbox/CMakeLists.txt; this file only makes the
# checkout's root the source directory (short object paths). Use xbox/build.sh
# or tools/xbox/msys/build.sh, not cmake directly.
cmake_minimum_required(VERSION 3.16)
project(prime_x C CXX)
include(xbox/CMakeLists.txt)
