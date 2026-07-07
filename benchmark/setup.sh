#!/bin/sh
# Install the runtimes and tooling needed for the full benchmark matrix.
# Already-present runtimes (python3, node) are not touched.
set -e
sudo apt install -y lua5.4 ruby mono-mcs mono-runtime hyperfine
