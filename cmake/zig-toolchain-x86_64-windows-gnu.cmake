include(zig-toolchain-common)

set(ZIG_LIBC "gnu")
set(ZIG_OS "windows")
set(ZIG_ARCH "x86_64")
set(ZIG_TARGET "${ZIG_ARCH}-${ZIG_OS}-${ZIG_LIBC}")

set(CMAKE_SYSTEM_NAME "Windows")
set(CMAKE_SYSTEM_PROCESSOR "AMD64")
