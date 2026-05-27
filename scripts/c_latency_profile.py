#!/usr/bin/env python3
"""
c_latency_profile.py — Runs the C latency test and formats the output.

Usage:
  cd ~/Order-Router
  python scripts/c_latency_profile.py

Requires the C build to have been done first (see run_c_backtest.py --skip-run).
"""
import subprocess
import sys
from pathlib import Path

ROOT      = Path(__file__).parent.parent
BUILD_DIR = ROOT / "c" / "build"


def main():
    binary = BUILD_DIR / "test_latency"
    if not binary.exists():
        sys.exit(
            f"[error] {binary} not found.\n"
            "Build first:\n"
            "  cd ~/Order-Router/c && mkdir -p build && cd build\n"
            "  cmake .. -DCMAKE_BUILD_TYPE=Release && make -j4"
        )

    print("=" * 60)
    print("  C Hot-Path Latency Profile")
    print("=" * 60)
    result = subprocess.run([str(binary)], capture_output=False)
    if result.returncode != 0:
        sys.exit("[error] test_latency failed")


if __name__ == "__main__":
    main()
