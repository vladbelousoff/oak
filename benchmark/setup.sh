#!/bin/sh
# Install the runtimes and tooling needed for the full benchmark matrix.
# Already-present runtimes (python3, perl) are not touched.
set -e
sudo apt install -y lua5.4 ruby perl php-cli hyperfine
