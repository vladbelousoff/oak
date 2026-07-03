#!/usr/bin/env python3
"""Inject the latest benchmark summary into the top-level README.

Reads results/summary.json (written by run.py) and rewrites the block
between the benchmark markers in README.md. Intended to run in CI after
run.py, but works locally too.
"""

import argparse
import datetime
import json
import os
import sys

BENCH_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_DIR = os.path.dirname(BENCH_DIR)

START = "<!-- benchmark:start -->"
END = "<!-- benchmark:end -->"


def build_block(summary):
    results = summary["results"]  # {bench: {lang: median_seconds}}
    benches = list(results.keys())
    langs = [l for l in summary["languages"]
             if any(l in results[b] for b in benches)]

    lines = []
    lines.append("| runtime | " + " | ".join(benches) + " |")
    lines.append("|---" * (len(benches) + 1) + "|")
    for lang in langs:
        row = ["| " + ("**oak**" if lang == "oak" else lang)]
        for bench in benches:
            mean = results[bench].get(lang)
            if mean is None:
                row.append("—")
            else:
                fastest = min(results[bench].values())
                row.append("{:.2f}× ({:.2f} s)".format(mean / fastest, mean))
        lines.append(" | ".join(row) + " |")
    lines.append("")

    sha = os.environ.get("GITHUB_SHA", "")[:9]
    at = " at `{}`".format(sha) if sha else ""
    date = datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%d")
    lines.append(
        "_Relative to the fastest runtime per benchmark, lower is better; "
        "median wall time in parentheses. Measured on a GitHub-hosted "
        "`ubuntu-latest` runner{} on {}. LuaJIT, Node, and mono are "
        "JIT-compiled reference points._".format(at, date))

    notes = ["**{}**: {}".format(k, v) for k, v in summary["skipped"].items()]
    notes += ["**{}**: verification failed — {}".format(k, v)
              for k, v in summary["failed"].items()]
    if notes:
        lines.append("")
        lines.append("_Excluded: " + "; ".join(notes) + "_")

    return "\n".join(lines)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--summary",
                    default=os.path.join(BENCH_DIR, "results", "summary.json"))
    ap.add_argument("--readme", default=os.path.join(REPO_DIR, "README.md"))
    args = ap.parse_args()

    with open(args.summary) as f:
        summary = json.load(f)
    with open(args.readme) as f:
        readme = f.read()

    head, sep, rest = readme.partition(START)
    if not sep:
        sys.exit("marker {!r} not found in {}".format(START, args.readme))
    _, sep, tail = rest.partition(END)
    if not sep:
        sys.exit("marker {!r} not found in {}".format(END, args.readme))

    block = build_block(summary)
    with open(args.readme, "w") as f:
        f.write(head + START + "\n" + block + "\n" + END + tail)
    print("updated {}".format(args.readme))
    return 0


if __name__ == "__main__":
    sys.exit(main())
