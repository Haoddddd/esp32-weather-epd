#!/usr/bin/env bash
# Builds the host-side CJK preview and writes preview.png (1x) and
# preview_3x.png (3x, for easier viewing) to this directory.
#
# Dependencies: a C++17 compiler, Python 3, Pillow
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../../.." && pwd)"
BUILD="$HERE/build"
mkdir -p "$BUILD"

c++ -std=c++17 -Wall -O1 \
  -I "$HERE/stubs" \
  -I "$REPO/fonts/fontconvert" \
  -iquote "$REPO/platformio/include" \
  -I "$REPO/platformio/lib/esp32-weather-epd-assets" \
  "$HERE/preview.cpp" "$REPO/platformio/src/cjk_text.cpp" \
  -o "$BUILD/preview"

"$BUILD/preview" "$BUILD/preview.pgm"

python3 -I - "$BUILD/preview.pgm" "$HERE" <<'EOF'
import sys
from PIL import Image
img = Image.open(sys.argv[1])
img.save(f'{sys.argv[2]}/preview.png')
img.resize((img.width * 3, img.height * 3), Image.NEAREST).save(
    f'{sys.argv[2]}/preview_3x.png')
EOF
echo "wrote $HERE/preview.png and $HERE/preview_3x.png"
