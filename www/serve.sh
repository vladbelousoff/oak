#!/bin/sh
# Run from the project root: sh www/serve.sh
cd "$(dirname "$0")/.." || exit 1
PORT=${1:-8765}
echo "Serving at http://127.0.0.1:$PORT/www/index.html"
python3 -m http.server "$PORT" --bind 127.0.0.1
