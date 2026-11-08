# Cross toolchain for aarch64 (ARM64) Linux targets on a Debian/Ubuntu
# multiarch host — see Dockerfile.probe. The cross gcc finds the target
# libc itself; CMAKE_LIBRARY_ARCHITECTURE steers find_library/find_package
# to the /usr/lib/aarch64-linux-gnu multiarch locations. No sysroot: the
# host's /usr layout already carries both architectures.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)

set(CMAKE_LIBRARY_ARCHITECTURE aarch64-linux-gnu)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)
