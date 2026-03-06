#!/usr/bin/env bash
# ds4linux — Quick install script for Arch-based systems
set -euo pipefail

echo "=== ds4linux installer ==="

# 1. Install dependencies
echo "[1/5] Installing dependencies..."
sudo pacman -S --needed --noconfirm \
    cmake ninja gcc \
    libevdev \
    qt6-base qt6-wayland \
    nlohmann-json

# 2. Build
echo "[2/5] Building..."
cd "$(dirname "$0")/.."
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"

# 3. Install binaries
echo "[3/5] Installing binaries..."
sudo cmake --install build --prefix /usr

# 4. Install systemd service
echo "[4/5] Installing systemd service..."
sudo cp config/ds4linux.service /etc/systemd/system/
sudo systemctl daemon-reload

# 5. Set up uinput module
echo "[5/5] Ensuring uinput module is loaded..."
if ! lsmod | grep -q uinput; then
    sudo modprobe uinput
fi
echo "uinput" | sudo tee /etc/modules-load.d/ds4linux.conf > /dev/null

echo ""
echo "=== Installation complete ==="
echo "Start the daemon:  sudo systemctl enable --now ds4linux"
echo "Launch the GUI:    ds4linux-gui"
