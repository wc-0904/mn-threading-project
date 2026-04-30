#!/bin/bash
set -e
cd "$(dirname "${BASH_SOURCE[0]}")"

echo "==> Building..."
make build/test/simulations/bench_heat2

echo ""
echo "==> Running benchmark and capturing frames..."
./build/test/simulations/bench_heat2 --save

echo ""
echo "==> Stitching GIF..."
convert -delay 6 -loop 0 heat2_frame_*.ppm heat2.gif
SIZE=$(du -h heat2.gif | cut -f1)
echo "    heat2.gif created (${SIZE})"

echo ""
echo "==> Copying to Windows Desktop..."
WIN_USER=$(cmd.exe /c "echo %USERNAME%" 2>/dev/null | tr -d '\r\n')
DEST="/mnt/c/Users/${WIN_USER}/Desktop/heat2.gif"
if cp heat2.gif "$DEST" 2>/dev/null; then
    echo "    Copied to ${DEST}"
else
    echo "    Could not copy to Desktop — open heat2.gif manually from WSL"
fi

echo ""
echo "Done."
