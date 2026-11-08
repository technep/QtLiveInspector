# Cross toolchain for armhf (ARMv7, hard-float) Linux targets on a
# Debian/Ubuntu multiarch host — see Dockerfile.probe. The cross gcc finds
# the target libc itself; CMAKE_LIBRARY_ARCHITECTURE steers find_library
# and find_package to the /usr/lib/arm-linux-gnueabihf multiarch locations.
# No sysroot: the host's /usr layout already carries both architectures.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(CMAKE_C_COMPILER arm-linux-gnueabihf-gcc)
set(CMAKE_CXX_COMPILER arm-linux-gnueabihf-g++)

set(CMAKE_LIBRARY_ARCHITECTURE arm-linux-gnueabihf)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)
