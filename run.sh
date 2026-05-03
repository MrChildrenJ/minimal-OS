#!/bin/bash
set -xue
# -x: trace mode, print each command before executing (for debug)
# -u: error on undefined variables
# -e: exit immediately if any command fails (non-zero status)
# fail fast + easier debugging

QEMU=qemu-system-riscv32
# QEMU file path
# QEMU points to the executable qemu-system-riscv32
# Target binary of QEMU for 32-bit RISC-V

# Path to clang and compiler flags
# -g3:                          debug info (max level)
# --target=riscv32-unknown-elf: Cross-compile for RISC-V 32-bit
# -fuse-ld=lld:                 Use LLVM linker (lld) instead of default (ld)
# -fno-stack-protector:         Disable stack protection, avoids inserting extra runtime checks
# -ffreestanding:               No standard environment (no OS and standard libc assumptions)
# -nostdlib:                    Do not link standard libraries
CC=/opt/homebrew/opt/llvm/bin/clang
CFLAGS="-std=c11 -O2 -g3 -Wall -Wextra --target=riscv32-unknown-elf -fuse-ld=lld -fno-stack-protector -ffreestanding -nostdlib"

# Build the kernel (compile + link → kernel.elf)
# -Tkernel.ld:              = -T,kernel.ld = use kernel.ld;
# -Wl:                      pass kernel.ld to linker
# -Map=kernel.map:          Generate a map file (記錄每個 symbol/section 被放到哪個地址, shows memory layout, for debug)
$CC $CFLAGS -Wl,-Tkernel.ld -Wl,-Map=kernel.map -o kernel.elf kernel.c common.c

# Start QEMU
$QEMU -machine virt -bios default -nographic -serial mon:stdio --no-reboot -kernel kernel.elf
# -machine virt: uses generic virtual board; minimal, clean environment (common for OS dev)
# -bios default: Uses QEMU’s built-in firmware (OpenSBI), handles early boot before OS
# -serial mon:stdio:
#       * Connect QEMU's standard input/output to the virtual machine's serial port
#       * Specifying mon: allows switching to the QEMU monitor by pressing Ctrl+A then C.
# --no-reboot: If the guest crashes or exits, no reboot, QEMU stops
# --
