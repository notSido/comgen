#!/bin/bash
set -e

echo "Installing comgen..."

# Check for dependencies
if ! command -v gcc &> /dev/null; then
    echo "Error: gcc is not installed"
    exit 1
fi

if ! command -v make &> /dev/null; then
    echo "Error: make is not installed"
    exit 1
fi

# Build
echo "Building..."
make comgen

# Install
echo "Installing to /usr/local/bin..."
if [ -w /usr/local/bin ]; then
    cp comgen /usr/local/bin/
else
    sudo cp comgen /usr/local/bin/
fi

echo "Done! You can now run 'comgen' from anywhere."
echo "Run 'comgen' to complete configuration."
