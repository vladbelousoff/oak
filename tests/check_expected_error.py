#!/usr/bin/env python3

import subprocess
import sys
from pathlib import Path


def main() -> int:
    if len(sys.argv) != 5:
        print(
            "usage: check_expected_error.py OAK_BIN SCRIPT EXPECTED_ERROR WORKDIR",
            file=sys.stderr,
        )
        return 2

    oak_bin, script, expected_error, workdir = sys.argv[1:]
    result = subprocess.run(
        [oak_bin, script],
        cwd=workdir,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE,
        text=True,
        check=False,
    )

    if result.returncode == 0:
        print("Expected oak to fail", file=sys.stderr)
        return 1

    expected = Path(expected_error).read_text(encoding="utf-8").strip()
    if expected not in result.stderr:
        print(f"Expected error output to contain: {expected!r}", file=sys.stderr)
        print(result.stderr, file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
