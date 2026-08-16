#!/usr/bin/bash

set -e

# -------------------------------------------------
# RISC-V RV32 GNU Toolchain Setup
# -------------------------------------------------

URL="https://github.com/riscv-collab/riscv-gnu-toolchain/releases/download/2026.07.15/riscv32-elf-ubuntu-24.04-gcc.tar.xz"

ARCHIVE="/tmp/riscv32-gcc.tar.xz"
INSTALL_DIR="/opt/riscv/rv32"

# -------------------------------------------------
# Install required packages
# -------------------------------------------------

sudo apt update

sudo apt install -y \
    build-essential \
    make \
    wget \
    xz-utils \
    python3 \
    python3-pip \
    python3-serial

# -------------------------------------------------
# Download toolchain
# -------------------------------------------------

echo "Downloading RISC-V toolchain..."

wget -O "$ARCHIVE" "$URL"

# -------------------------------------------------
# Extract toolchain
# -------------------------------------------------

echo "Installing to $INSTALL_DIR..."

sudo mkdir -p "$INSTALL_DIR"

sudo tar -xJf "$ARCHIVE" \
    --strip-components=1 \
    -C "$INSTALL_DIR"

# -------------------------------------------------
# Add toolchain to PATH permanently
# -------------------------------------------------

echo 'export PATH=/opt/riscv/rv32/bin:$PATH' | \
sudo tee /etc/profile.d/riscv32.sh > /dev/null

# -------------------------------------------------
# Clean up
# -------------------------------------------------

rm "$ARCHIVE"

echo
echo "=================================="
echo " Toolchain Installation Complete  "
echo "=================================="
echo
echo "Run this command to update PATH:"
echo
echo "source /etc/profile.d/riscv32.sh"
echo
echo "Then verify:"
echo
echo "riscv32-unknown-elf-gcc --version"
