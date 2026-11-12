#!/usr/bin/env python3
"""Run bounded regex ownership regressions using a Linux/GNU Makefiles LeakCheck build."""
import argparse
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("build_dir", type=Path)
parser.add_argument("--repeat", type=int, default=2)
args = parser.parse_args()
if args.repeat < 1:
    parser.error("--repeat must be positive")
build = args.build_dir.resolve()
source = Path(__file__).resolve().parent
link = shlex.split((build / "CMakeFiles/moo.dir/link.txt").read_text())
if "-fsanitize=address" not in " ".join(link):
    parser.error("configure and build with -DCMAKE_BUILD_TYPE=LeakCheck first")
# Reuse the build's compiler, dependencies and ABI flags. Never modify build objects.
output = link.index("-o")
objects = [str(build / x) for x in link[:output] if x.endswith(".o")]
server = next(x for x in objects if x.endswith("/src/server.cc.o"))
objects = [x for x in objects if not x.endswith(("/src/server.cc.o", "/src/pcre_moo.cc.o"))]
flags = [x for x in link[1:output] if not x.endswith(".o")]
# Release flags would disable the behavioral assertions in the harness.
flags = [x for x in flags if x != "-DNDEBUG" and not x.startswith("-Wl,--dependency-file=")]
modes = ["match-ok", "match-named", "match-none", "match-empty", "match-global",
         "match-single", "match-compile-error", "match-error", "match-partial-error",
         "match-limit", "replace-ok", "replace-compile-error", "replace-error"]
with tempfile.TemporaryDirectory(prefix="toaststunt-regex-tests-") as tmp:
    renamed = str(Path(tmp) / "server-no-main.o")
    subprocess.run(["objcopy", "--redefine-sym", "main=toaststunt_server_main", server, renamed], check=True)
    binary = str(Path(tmp) / "regex-leaks")
    root = source.parents[1]
    command = [link[0], *flags, "-std=c++17", "-I" + str(root / "src/include"),
               "-I" + str(root / "src/dependencies"), "-I" + str(build),
               str(source / "regex_leaks.cc"), renamed, *objects,
               *link[output + 2:], "-pthread", "-o", binary]
    subprocess.run(command, check=True)
    env = dict(os.environ)
    env["ASAN_OPTIONS"] = "detect_leaks=1:leak_check_at_exit=0"
    failures = []
    for mode in modes:
        result = subprocess.run([binary, mode, str(args.repeat)], env=env, timeout=60)
        if result.returncode:
            failures.append((mode, result.returncode))
    if failures:
        raise SystemExit("Failed regex regressions: " + repr(failures))
print("All 13 regex regressions passed (" + str(args.repeat) + " calls each).")
