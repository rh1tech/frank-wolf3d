#!/bin/bash
#
# release.sh - Build all release variants of frank-wolf3d
#
# Creates UF2 files for each board variant (M1, M2) and each speed profile:
#   - standard: 378 MHz CPU, 100 MHz PSRAM, 66 MHz Flash (default)
#   - oc504:    504 MHz CPU, 100 MHz PSRAM, 66 MHz Flash (for boards that
#               do not run reliably at 378; requires higher core voltage)
#
# Usage: ./release.sh [version]
#   version  - Optional version string (e.g. 1.05). If omitted, prompts interactively.
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
echo -e "${CYAN}FRANK Wolf3D Release Builder${NC}"
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

BOARDS=("M1" "M2")

# Speed profiles: "suffix:CPU:PSRAM:FLASH"
# - standard runs at 378 MHz core (lower voltage, works on most boards)
# - oc504 runs at 504 MHz core for boards that are unstable at 378 MHz
PROFILES=(
    "standard:378:100:66"
    "oc504:504:100:66"
)

BUILD_COUNT=0
TOTAL_BUILDS=$((${#BOARDS[@]} * ${#PROFILES[@]}))

echo -e "${YELLOW}Building $TOTAL_BUILDS firmware variants...${NC}"

for BOARD in "${BOARDS[@]}"; do
    if [[ "$BOARD" == "M1" ]]; then BOARD_NUM=1; else BOARD_NUM=2; fi

    for PROFILE in "${PROFILES[@]}"; do
        BUILD_COUNT=$((BUILD_COUNT + 1))

        IFS=':' read -r SUFFIX CPU PSRAM FLASH <<< "$PROFILE"

        if [[ "$SUFFIX" == "standard" ]]; then
            OUTPUT_NAME="frank-wolf3d_m${BOARD_NUM}_${VERSION}.uf2"
        else
            OUTPUT_NAME="frank-wolf3d_m${BOARD_NUM}_${SUFFIX}_${VERSION}.uf2"
        fi

        echo -e "${CYAN}[$BUILD_COUNT/$TOTAL_BUILDS] Building: $OUTPUT_NAME (${CPU}/${PSRAM}/${FLASH})${NC}"

        rm -rf build && mkdir build && cd build
        cmake .. -DBOARD_VARIANT="$BOARD" -DCPU_SPEED="$CPU" -DPSRAM_SPEED="$PSRAM" -DFLASH_SPEED="$FLASH" -DUSB_HID_ENABLED=1 > /dev/null 2>&1

        if make -j8 > /dev/null 2>&1; then
            if [[ -f "frank-wolf3d.uf2" ]]; then
                cp "frank-wolf3d.uf2" "$RELEASE_DIR/$OUTPUT_NAME"
                echo -e "  ${GREEN}OK${NC} -> release/$OUTPUT_NAME"
            else
                echo -e "  ${RED}UF2 not found${NC}"
            fi
        else
            echo -e "  ${RED}Build failed${NC}"
        fi

        cd "$SCRIPT_DIR"
    done
done

rm -rf build
echo -e "${GREEN}Release build complete!${NC}"
