#!/usr/bin/env bash
#
# regress.sh — convenience wrapper around the regression-test container.
#
# Usage:
#   regress/regress.sh check   [16|17|18]   # default: 16
#   regress/regress.sh refresh [16|17|18]   # rewrites expected/zhparser.out
#   regress/regress.sh matrix                # build & test 16, 17, 18 in turn
#   regress/regress.sh shell   [16|17|18]
#
# Requires: docker (or podman aliased as docker).

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

DOCKER="${DOCKER:-docker}"
if ! command -v "$DOCKER" >/dev/null 2>&1; then
    echo "regress.sh: '$DOCKER' not found. Install Docker or set DOCKER=podman." >&2
    exit 2
fi

build() {
    local pg="$1"
    "$DOCKER" build \
        -f regress/Dockerfile \
        --build-arg "PG_VERSION=$pg" \
        -t "zhparser-regress:pg$pg" \
        .
}

run_check() {
    local pg="$1"
    build "$pg"
    "$DOCKER" run --rm "zhparser-regress:pg$pg"
}

run_refresh() {
    local pg="$1"
    build "$pg"
    "$DOCKER" run --rm \
        -v "$ROOT/expected:/host-expected" \
        "zhparser-regress:pg$pg" refresh
}

run_shell() {
    local pg="$1"
    build "$pg"
    "$DOCKER" run --rm -it "zhparser-regress:pg$pg" shell
}

cmd="${1:-check}"
pg="${2:-16}"

case "$cmd" in
    check)   run_check   "$pg" ;;
    refresh) run_refresh "$pg" ;;
    shell)   run_shell   "$pg" ;;
    matrix)
        for v in 16 17 18; do
            echo "==================== PG $v ===================="
            run_check "$v"
        done
        ;;
    *)
        echo "usage: $0 {check|refresh|shell|matrix} [PG_VERSION]" >&2
        exit 2
        ;;
esac
