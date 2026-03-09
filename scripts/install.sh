#!/usr/bin/env bash
# ds4linux — Build & install script for Arch-based systems
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

echo "=== ds4linux installer ==="
echo ""

# ── 1. Install build dependencies ────────────────────────────────────────────
echo "[1/4] Installing dependencies..."
sudo pacman -S --needed --noconfirm \
    cmake ninja gcc \
    libevdev \
    nlohmann-json

# ── 2. Build the daemon ──────────────────────────────────────────────────────
echo "[2/4] Building daemon..."
cd "$PROJECT_DIR"
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"

# ── 3. Install binaries ──────────────────────────────────────────────────────
echo "[3/4] Installing binaries..."
sudo cmake --install build --prefix /usr

# ── 4. Install udev rules ─────────────────────────────────────────────────────
echo "[4/4] Installing udev rules..."
sudo cp "$PROJECT_DIR/config/99-ds4linux.rules" /etc/udev/rules.d/
sudo udevadm control --reload-rules
sudo udevadm trigger

# Ensure uinput module is loaded now and on boot
if ! lsmod | grep -q uinput; then
    sudo modprobe uinput
fi
echo "uinput" | sudo tee /etc/modules-load.d/ds4linux.conf > /dev/null

echo ""
echo "=== Installation complete ==="
echo ""
echo "Usage:"
echo "  1. Connect your DualShock 4 / DualSense controller (USB or Bluetooth)."
echo "  2. Run the daemon:"
echo "       sudo ds4linux-daemon"
echo "  3. Open Steam AFTER the daemon is running for best compatibility."
echo "  4. When done, press Ctrl+C to stop the daemon."
