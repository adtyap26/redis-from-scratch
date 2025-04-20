#!/bin/bash
set -euo pipefail

COMPILER="gcc"
BUILD_DIR="builds"
mkdir -p builds

rm -rf "${BUILD_DIR:?}/"*

# Build for Linux (64-bit)
echo "Building for Linux (64-bit)..."
if $COMPILER -Wall -Wextra -Og -g main.c -o $BUILD_DIR/server; then
echo "Build complete! server binary is in the builds directory."
else
  echo "Compilation of main.c failed!"
  exit 1
fi

echo "Compiling client.c..."
if $COMPILER -Wall -Wextra -Og -g client.c -o $BUILD_DIR/client; then
  echo "Successfully built client."
else
  echo "Compilation of client.c failed!"
  exit 1
fi

