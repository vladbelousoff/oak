#!/usr/bin/env python3
"""Cross-language benchmark runner for Oak.

Verifies every program's output against expected.txt, then times each
benchmark across all available runtimes with hyperfine and aggregates the
results into results/RESULTS.md.
"""

import argparse
import json
import os
import shutil
import subprocess
import sys

BENCH_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_DIR = os.path.dirname(BENCH_DIR)

# Debug builds are never benchmarked: buildtype=debug is -O0 with memory
# tracking compiled in, which runs 4-8x slower and says nothing about real
# Oak performance. Only a build dir whose meson buildtype is confirmed
# non-debug is accepted.
_OAK_CANDIDATES = [
    os.path.join(REPO_DIR, "build-release"),
    os.path.join(REPO_DIR, "build"),
]


def _optimized_oak():
    for build_dir in _OAK_CANDIDATES:
        exe = os.path.join(build_dir, "oak")
        info = os.path.join(build_dir, "meson-info", "intro-buildoptions.json")
        if not (os.path.exists(exe) and os.path.exists(info)):
            continue
        try:
            with open(info) as f:
                opts = {o["name"]: o["value"] for o in json.load(f)}
        except (OSError, ValueError, KeyError):
            continue
        if opts.get("buildtype") != "debug":
            return exe
    return None


OAK_BIN = _optimized_oak()
RESULTS_DIR = os.path.join(BENCH_DIR, "results")

BENCHMARKS = ["fib", "nsieve", "mandelbrot", "hashmap", "strcat"]

# line index -> relative tolerance, applied when comparing against expected.txt
TOLERANCES = {
    "mandelbrot": {0: 0.005},  # Oak computes in f32; boundary pixels may flip
}


class Lang:
    def __init__(self, name, ext, cmd, compile_cmd=None, version_cmd=None):
        self.name = name
        self.ext = ext
        self.cmd = cmd  # list with {src}/{exe} placeholders
        self.compile_cmd = compile_cmd
        self.version_cmd = version_cmd

    def available(self):
        probe = self.compile_cmd[0] if self.compile_cmd else self.cmd[0]
        if os.path.isabs(probe):
            return os.path.exists(probe)
        return shutil.which(probe) is not None

    def command(self, bench):
        src = os.path.join(BENCH_DIR, bench, bench + "." + self.ext)
        exe = os.path.join(BENCH_DIR, bench, bench + ".exe")
        return [a.format(src=src, exe=exe) for a in self.cmd]

    def compile(self, bench):
        """Compile step (C# only). Returns None on success, error text on failure."""
        if not self.compile_cmd:
            return None
        src = os.path.join(BENCH_DIR, bench, bench + "." + self.ext)
        exe = os.path.join(BENCH_DIR, bench, bench + ".exe")
        cmd = [a.format(src=src, exe=exe) for a in self.compile_cmd]
        proc = subprocess.run(cmd, capture_output=True, text=True)
        if proc.returncode != 0:
            return proc.stderr.strip() or proc.stdout.strip()
        return None

    def version(self):
        if not self.version_cmd:
            return ""
        try:
            proc = subprocess.run(
                self.version_cmd, capture_output=True, text=True, timeout=10
            )
            out = (proc.stdout or proc.stderr).strip().splitlines()
            return out[0] if out else ""
        except OSError:
            return ""


LANGS = [
    Lang("oak", "oak", [OAK_BIN or "oak-optimized-build-missing",
                        "--no-debug-symbols", "{src}"],
         version_cmd=None),
    Lang("lua5.4", "lua", ["lua5.4", "{src}"],
         version_cmd=["lua5.4", "-v"]),
    Lang("python3", "py", ["python3", "{src}"],
         version_cmd=["python3", "--version"]),
    Lang("ruby", "rb", ["ruby", "{src}"],
         version_cmd=["ruby", "--version"]),
    Lang("node", "js", ["node", "{src}"],
         version_cmd=["node", "--version"]),
    Lang("csharp", "cs", ["mono", "{exe}"],
         compile_cmd=["mcs", "-optimize+", "-out:{exe}", "{src}"],
         version_cmd=["mono", "--version"]),
]


def shell_quote(args):
    return " ".join("'" + a.replace("'", "'\\''") + "'" for a in args)


def outputs_match(bench, actual_lines, expected_lines):
    if len(actual_lines) != len(expected_lines):
        return False
    tol = TOLERANCES.get(bench, {})
    for idx, (got, want) in enumerate(zip(actual_lines, expected_lines)):
        if got == want:
            continue
        rel = tol.get(idx)
        if rel is None:
            return False
        try:
            got_n, want_n = float(got), float(want)
        except ValueError:
            return False
        if want_n == 0 or abs(got_n - want_n) / abs(want_n) > rel:
            return False
    return True


def verify(lang, bench):
    """Run once and compare against expected.txt. Returns None or error text."""
    expected_path = os.path.join(BENCH_DIR, bench, "expected.txt")
    with open(expected_path) as f:
        expected = f.read().strip().splitlines()
    try:
        proc = subprocess.run(
            lang.command(bench), capture_output=True, text=True, timeout=600
        )
    except subprocess.TimeoutExpired:
        return "timed out"
    if proc.returncode != 0:
        return "exit {}: {}".format(
            proc.returncode, (proc.stderr or proc.stdout).strip()[:200]
        )
    actual = proc.stdout.strip().splitlines()
    if not outputs_match(bench, actual, expected):
        return "output {!r} != expected {!r}".format(actual, expected)
    return None


def run_hyperfine(bench, langs, warmup, min_runs):
    json_path = os.path.join(RESULTS_DIR, bench + ".json")
    md_path = os.path.join(RESULTS_DIR, bench + ".md")
    cmd = [
        "hyperfine",
        "--warmup", str(warmup),
        "--min-runs", str(min_runs),
        "--export-json", json_path,
        "--export-markdown", md_path,
    ]
    for lang in langs:
        cmd += ["-n", lang.name, shell_quote(lang.command(bench))]
    proc = subprocess.run(cmd)
    if proc.returncode != 0:
        return None
    with open(json_path) as f:
        return json.load(f)


def aggregate(results, skipped, failed, langs_run):
    """results: {bench: {lang: median_seconds}}"""
    lines = ["# Benchmark Results", ""]

    lines.append("## Environment")
    lines.append("")
    for lang in langs_run:
        ver = lang.version()
        if lang.name == "oak":
            ver = "oak (this repo, {} --no-debug-symbols)".format(
                os.path.relpath(OAK_BIN, REPO_DIR))
        lines.append("- **{}**: {}".format(lang.name, ver))
    lines.append("")

    lang_names = [l.name for l in langs_run]

    lines.append("## Summary (time relative to fastest, per benchmark)")
    lines.append("")
    lines.append("| language | " + " | ".join(results.keys()) + " |")
    lines.append("|---" * (len(results) + 1) + "|")
    for name in lang_names:
        row = ["| " + name]
        for bench, times in results.items():
            if name in times:
                fastest = min(times.values())
                row.append("{:.2f}x".format(times[name] / fastest))
            else:
                row.append("—")
        lines.append(" | ".join(row) + " |")
    lines.append("")

    for bench, times in results.items():
        lines.append("## {}".format(bench))
        lines.append("")
        md_path = os.path.join(RESULTS_DIR, bench + ".md")
        if os.path.exists(md_path):
            with open(md_path) as f:
                lines.append(f.read().strip())
        lines.append("")

    if skipped:
        lines.append("## Skipped runtimes")
        lines.append("")
        for name, reason in skipped.items():
            lines.append("- **{}**: {}".format(name, reason))
        lines.append("")

    if failed:
        lines.append("## Verification failures (excluded from timing)")
        lines.append("")
        for (name, bench), reason in failed.items():
            lines.append("- **{} / {}**: {}".format(name, bench, reason))
        lines.append("")

    out_path = os.path.join(RESULTS_DIR, "RESULTS.md")
    with open(out_path, "w") as f:
        f.write("\n".join(lines) + "\n")

    # Machine-readable aggregate for downstream tooling (README injection in
    # CI); mirrors RESULTS.md without requiring markdown parsing.
    summary = {
        "languages": lang_names,
        "results": results,
        "skipped": skipped,
        "failed": {"{}/{}".format(l, b): r for (l, b), r in failed.items()},
    }
    with open(os.path.join(RESULTS_DIR, "summary.json"), "w") as f:
        json.dump(summary, f, indent=2)

    return out_path


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--bench", help="comma-separated benchmarks to run")
    ap.add_argument("--lang", help="comma-separated languages to run")
    ap.add_argument("--skip-verify", action="store_true",
                    help="skip output verification before timing")
    # Workloads run 10-20 s in the slowest runtime, so a handful of runs is
    # already stable; more warmup/runs would stretch the suite past an hour.
    ap.add_argument("--warmup", type=int, default=1)
    ap.add_argument("--min-runs", type=int, default=3)
    ap.add_argument("--list", action="store_true",
                    help="list benchmarks and languages, then exit")
    args = ap.parse_args()

    if args.list:
        print("benchmarks:", ", ".join(BENCHMARKS))
        print("languages: ", ", ".join(l.name for l in LANGS))
        return 0

    benches = args.bench.split(",") if args.bench else BENCHMARKS
    for b in benches:
        if b not in BENCHMARKS:
            sys.exit("unknown benchmark: {}".format(b))
    wanted = args.lang.split(",") if args.lang else [l.name for l in LANGS]
    for w in wanted:
        if w not in [l.name for l in LANGS]:
            sys.exit("unknown language: {}".format(w))

    if not shutil.which("hyperfine"):
        sys.exit("hyperfine not found — run ./setup.sh first")

    if "oak" in wanted and OAK_BIN is None:
        sys.exit(
            "no optimized oak build found — debug builds are never "
            "benchmarked.\nCreate one with:\n"
            "  meson setup build-release -Dbuildtype=release\n"
            "  meson compile -C build-release"
        )

    os.makedirs(RESULTS_DIR, exist_ok=True)

    skipped = {}
    langs = []
    for lang in LANGS:
        if lang.name not in wanted:
            continue
        if lang.available():
            langs.append(lang)
        else:
            skipped[lang.name] = "runtime not installed"
            print("skip {}: not installed".format(lang.name))

    failed = {}
    results = {}
    for bench in benches:
        runnable = []
        for lang in langs:
            err = lang.compile(bench)
            if err is not None:
                skipped[lang.name] = "compile failed: " + err[:200]
                print("skip {} / {}: compile failed".format(lang.name, bench))
                continue
            if not args.skip_verify:
                err = verify(lang, bench)
                if err is not None:
                    failed[(lang.name, bench)] = err
                    print("VERIFY FAIL {} / {}: {}".format(lang.name, bench, err))
                    continue
            runnable.append(lang)
        if not runnable:
            print("no runnable languages for {}, skipping".format(bench))
            continue

        print("\n=== {} ({}) ===".format(bench, ", ".join(l.name for l in runnable)))
        data = run_hyperfine(bench, runnable, args.warmup, args.min_runs)
        if data is None:
            print("hyperfine failed for {}".format(bench))
            continue
        # hyperfine reports results in invocation order, matching `runnable`.
        # Median, not mean: robust against outlier runs on noisy hosts
        # (shared CI runners, VMs), which hyperfine warns about but still
        # folds into the mean.
        results[bench] = {
            runnable[idx].name: entry["median"]
            for idx, entry in enumerate(data["results"])
        }

    if not results:
        sys.exit("no results produced")

    out_path = aggregate(results, skipped, failed, langs)
    print("\nwrote {}".format(out_path))
    return 0


if __name__ == "__main__":
    sys.exit(main())
