#!/bin/bash
set -xue
# -x: trace mode, print each command before executing (for debug)
# -u: error on undefined variables
# -e: exit immediately if any command fails (non-zero status)
# fail fast + easier debugging

# QEMU file path
# QEMU points to the executable qemu-system-riscv32
# Target binary of QEMU for 32-bit RISC-V
QEMU=qemu-system-riscv32

# Start QEMU
$QEMU -machine virt -bios default -nographic -serial mon:stdio --no-reboot
# -machine virt: uses generic virtual board; minimal, clean environment (common for OS dev)
# -bios default: Uses QEMU’s built-in firmware (OpenSBI), handles early boot before OS
# -serial mon:stdio:
#       * Connect QEMU's standard input/output to the virtual machine's serial port
#       * Specifying mon: allows switching to the QEMU monitor by pressing Ctrl+A then C.
# --no-reboot: If the guest crashes or exits, no reboot, QEMU stops
