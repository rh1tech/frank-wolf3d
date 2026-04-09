#!/bin/bash
#
# release.sh - Build all release variants of frank-wolf
#
# Creates UF2 files for each board variant (M1, M2) at each clock speed:
#   - Non-overclocked: 252 MHz CPU, 100 MHz PSRAM
#   - Medium overclock: 378 MHz CPU, 133 MHz PSRAM
#   - Max overclock: 504 MHz CPU, 166 MHz PSRAM
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

# Version file
VERSION_FILE="version.txt"

if [[ -f "$VERSION_FILE" ]]; then
    read -r LAST_MAJOR LAST_MINOR < "$VERSION_FILE"
else
    LAST_MAJOR=1
    LAST_MINOR=0
fi

NEXT_MINOR=$((LAST_MINOR + 1))
NEXT_MAJOR=$LAST_MAJOR
if [[ $NEXT_MINOR -ge 100 ]]; then
    NEXT_MAJOR=$((NEXT_MAJOR + 1))
    NEXT_MINOR=0
fi

echo ""
echo -e "${CYAN}frank-wolf Release Builder${NC}"
echo ""
echo -e "Last version: ${YELLOW}${LAST_MAJOR}.$(printf '%02d' $LAST_MINOR)${NC}"
echo ""

DEFAULT_VERSION="${NEXT_MAJOR}.$(printf '%02d' $NEXT_MINOR)"
if [[ -n "$1" ]]; then
    INPUT_VERSION="$1"
else
    read -p "Enter version [default: $DEFAULT_VERSION]: " INPUT_VERSION
    INPUT_VERSION=${INPUT_VERSION:-$DEFAULT_VERSION}
fi

if [[ "$INPUT_VERSION" == *"."* ]]; then
    MAJOR="${INPUT_VERSION%%.*}"
    MINOR="${INPUT_VERSION##*.}"
else
    read -r MAJOR MINOR <<< "$INPUT_VERSION"
fi

MINOR=$((10#$MINOR))
MAJOR=$((10#$MAJOR))

VERSION="${MAJOR}_$(printf '%02d' $MINOR)"
echo -e "${GREEN}Building release version: ${MAJOR}.$(printf '%02d' $MINOR)${NC}"
echo "$MAJOR $MINOR" > "$VERSION_FILE"

RELEASE_DIR="$SCRIPT_DIR/release"
mkdir -p "$RELEASE_DIR"

CONFIGS=(
    "M1 252 100 non-overclocked"
    "M1 378 133 medium-overclock"
    "M1 504 166 max-overclock"
    "M2 252 100 non-overclocked"
    "M2 378 133 medium-overclock"
    "M2 504 166 max-overclock"
)

BUILD_COUNT=0
TOTAL_BUILDS=${#CONFIGS[@]}

echo -e "${YELLOW}Building $TOTAL_BUILDS firmware variants...${NC}"

for config in "${CONFIGS[@]}"; do
    read -r BOARD CPU PSRAM DESC <<< "$config"
    BUILD_COUNT=$((BUILD_COUNT + 1))

    if [[ "$BOARD" == "M1" ]]; then BOARD_NUM=1; else BOARD_NUM=2; fi

    OUTPUT_NAME="frank-wolf_m${BOARD_NUM}_${CPU}_${PSRAM}_${VERSION}.uf2"
    echo -e "${CYAN}[$BUILD_COUNT/$TOTAL_BUILDS] Building: $OUTPUT_NAME${NC}"

    rm -rf build && mkdir build && cd build
    cmake .. -DBOARD_VARIANT="$BOARD" -DCPU_SPEED="$CPU" -DPSRAM_SPEED="$PSRAM" -DUSB_HID_ENABLED=1 > /dev/null 2>&1

    if make -j8 > /dev/null 2>&1; then
        if [[ -f "frank-wolf.uf2" ]]; then
            cp "frank-wolf.uf2" "$RELEASE_DIR/$OUTPUT_NAME"
            echo -e "  ${GREEN}OK${NC} -> release/$OUTPUT_NAME"
        else
            echo -e "  ${RED}UF2 not found${NC}"
        fi
    else
        echo -e "  ${RED}Build failed${NC}"
    fi

    cd "$SCRIPT_DIR"
done

rm -rf build
echo -e "${GREEN}Release build complete!${NC}"
