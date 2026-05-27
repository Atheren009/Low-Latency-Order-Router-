#!/usr/bin/env bash
# build.sh — One-command build + test for the C Order Router
# Run from WSL2: bash scripts/build.sh [debug|release] [test]
#
# Examples:
#   bash scripts/build.sh                # release build
#   bash scripts/build.sh debug          # debug build with ASAN/UBSAN
#   bash scripts/build.sh release test   # build + run all C tests

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
C_DIR="$ROOT/c"
BUILD_DIR="$C_DIR/build"

BUILD_TYPE="${1:-Release}"
DO_TEST="${2:-}"

# Capitalise first letter for cmake
BUILD_TYPE="$(echo "${BUILD_TYPE^}" | sed 's/debug/Debug/I; s/release/Release/I')"
[[ "$BUILD_TYPE" == "Debug" || "$BUILD_TYPE" == "Release" ]] || {
    echo "[error] BUILD_TYPE must be 'debug' or 'release', got '$BUILD_TYPE'"; exit 1; }

echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "  Order Router — C Build"
echo "  Mode : $BUILD_TYPE"
echo "  Dir  : $BUILD_DIR"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

# Check dependencies
for cmd in cmake gcc; do
    command -v "$cmd" &>/dev/null || { echo "[error] '$cmd' not found"; exit 1; }
done
echo "[ok] gcc  : $(gcc --version | head -1)"
echo "[ok] cmake: $(cmake --version | head -1)"

# Configure
mkdir -p "$BUILD_DIR"
cmake -S "$C_DIR" -B "$BUILD_DIR" \
      -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
      -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
      --log-level=WARNING

# Build (all targets)
cmake --build "$BUILD_DIR" --parallel "$(nproc)"

echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "  Targets built:"
for bin in or_backtest or_sim_backtest \
           test_order_book test_exchange test_strategies \
           test_latency test_sim_orders; do
    if [[ -f "$BUILD_DIR/$bin" ]]; then
        echo "    ✓ $bin"
    else
        echo "    ✗ $bin  (MISSING)"
    fi
done
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

# Optionally run CTest
if [[ "$DO_TEST" == "test" ]]; then
    echo ""
    echo "  Running CTest..."
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    cd "$BUILD_DIR"
    ctest --output-on-failure --verbose
fi

echo ""
echo "[done] Build complete."
echo ""
echo "  Quick commands:"
echo "    Run real-data backtest:"
echo "      $BUILD_DIR/or_backtest 'Price Feed' results/c_backtest_results.csv"
echo ""
echo "    Run simulated-orders backtest:"
echo "      $BUILD_DIR/or_sim_backtest 'Price Feed' results/c_sim_results.csv 2000"
echo ""
echo "    Run latency profiler:"
echo "      $BUILD_DIR/test_latency"
echo ""
echo "    Generate charts (Python):"
echo "      python scripts/run_c_backtest.py --skip-build --skip-run"
