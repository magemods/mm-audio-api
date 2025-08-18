BUILD_DIR := build
MOD_TOML := ./mod.toml
MOD_ELF  := $(BUILD_DIR)/mod.elf

# Allow the user to specify the compiler and linker on macOS
# as Apple Clang does not support MIPS architecture
ifeq ($(OS),Windows_NT)
    CC      := clang
    LD      := ld.lld
else ifneq ($(shell uname),Darwin)
    CC      := clang
    LD      := ld.lld
else
    CC      ?= clang
    LD      ?= ld.lld
endif

ifeq ($(OS),Windows_NT)
	PYTHON_EXEC ?= python
	RECOMP_TOOL ?= RecompModTool.exe
else
	PYTHON_EXEC ?= python3
	RECOMP_TOOL ?= RecompModTool
endif

LDSCRIPT := mod.ld
CFLAGS   := -target mips -mips2 -mabi=32 -O2 -G0 -mno-abicalls -mno-odd-spreg -mno-check-zero-division \
			-fomit-frame-pointer -ffast-math -fno-unsafe-math-optimizations -fno-builtin-memset \
			-Wall -Wextra -Wno-incompatible-library-redeclaration -Wno-unused-parameter -Wno-unknown-pragmas \
			-Wno-unused-variable -Wno-missing-braces -Wno-unsupported-floating-point-opt -Werror=section
CPPFLAGS := -nostdinc -D_LANGUAGE_C -DMIPS -DF3DEX_GBI_2 -DF3DEX_GBI_PL -DGBI_DOWHILE -I include \
			-I include/recomp/dummy_headers -I mm-decomp/include -I mm-decomp/src -I mm-decomp/extracted/n64-us \
			-I mm-decomp/include/libc
LDFLAGS  := -nostdlib -T $(LDSCRIPT) -Map $(BUILD_DIR)/mod.map --unresolved-symbols=ignore-all --emit-relocs -e 0 \
			--no-nmagic

C_SRCS := $(wildcard src/core/*.c) $(wildcard src/utils/*.c)
C_OBJS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(C_SRCS))
C_DEPS := $(patsubst %.c,$(BUILD_DIR)/%.d,$(C_SRCS))
BUILD_DIRS := $(sort $(dir $(C_OBJS)))


all: $(MOD_ELF) extlib-windows extlib-linux extlib-macos nrm dist

$(BUILD_DIRS):
ifeq ($(OS),Windows_NT)
	mkdir $(subst /,\,$@)
else
	mkdir -p $@
endif

$(MOD_ELF): $(C_OBJS) $(LDSCRIPT) | $(BUILD_DIRS)
	$(LD) $(C_OBJS) $(LDFLAGS) -o $@

$(C_OBJS): $(BUILD_DIR)/%.o : %.c | $(BUILD_DIRS)
	$(CC) $(CFLAGS) $(CPPFLAGS) $< -MMD -MF $(@:.o=.d) -c -o $@

extlib-%:
	cmake -S . -B $(BUILD_DIR)/extlib/$* -G Ninja -DCMAKE_BUILD_TYPE=Release \
	      --toolchain=cmake/zig-toolchain-$*.cmake
	cmake --build $(BUILD_DIR)/extlib/$* --parallel
	cmake --install $(BUILD_DIR)/extlib/$* --prefix $(BUILD_DIR) --component extlib

extlib-windows: extlib-x86_64-windows-gnu

extlib-linux: extlib-x86_64-linux-gnu

extlib-macos: extlib-aarch64-macos-none

nrm: $(MOD_ELF)
	$(RECOMP_TOOL) $(MOD_TOML) $(BUILD_DIR)

dist:
	$(PYTHON_EXEC) tools/thunderstore.py

clean:
ifeq ($(OS),Windows_NT)
	- rmdir /S /Q $(BUILD_DIR)
else
	- rm -rf $(BUILD_DIR)
endif

-include $(C_DEPS)

.PHONY: all extlib-windows extlib-linux extlib-macos nrm dist clean
