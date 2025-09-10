#!/bin/bash

# Generate PNG icons from SVG
# Requires inkscape or imagemagick

# Get the directory where this script is located
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RESOURCES_DIR="${SCRIPT_DIR}/resources"

if command -v inkscape &> /dev/null; then
    echo "Using Inkscape to generate PNG icons..."
    inkscape "${RESOURCES_DIR}/icon.svg" --export-type=png --export-filename="${RESOURCES_DIR}/icon.png" --export-width=128 --export-height=128
    inkscape "${RESOURCES_DIR}/icon.svg" --export-type=png --export-filename="${RESOURCES_DIR}/icon-64.png" --export-width=64 --export-height=64
    inkscape "${RESOURCES_DIR}/icon.svg" --export-type=png --export-filename="${RESOURCES_DIR}/icon-32.png" --export-width=32 --export-height=32
elif command -v convert &> /dev/null; then
    echo "Using ImageMagick to generate PNG icons..."
    convert -background transparent "${RESOURCES_DIR}/icon.svg" -resize 128x128 "${RESOURCES_DIR}/icon.png"
    convert -background transparent "${RESOURCES_DIR}/icon.svg" -resize 64x64 "${RESOURCES_DIR}/icon-64.png"
    convert -background transparent "${RESOURCES_DIR}/icon.svg" -resize 32x32 "${RESOURCES_DIR}/icon-32.png"
else
    echo "Please install inkscape or imagemagick to generate PNG icons"
    echo "Arch Linux: sudo pacman -S inkscape"
    echo "           or: sudo pacman -S imagemagick"
fi