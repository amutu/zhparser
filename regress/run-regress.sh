#!/usr/bin/env bash
#
# run-regress — entrypoint for the zhparser regression-test container.
#
# Modes:
#   check     (default)  Run `pg_regress` against an ephemeral cluster.
#                        Exit 0 on PASS, non-zero on diff.
#   refresh              Run pg_regress in --create-role mode and copy the
#                        produced *.out files to /host-expected (mount this
#                        as a host volume to receive them).
#   shell                Bring up the cluster and drop into bash.
#
# Environment:
#   PGDATA               cluster directory (default: /var/lib/postgresql/regress)
#   PG_REGRESS_PORT      port the throw-away cluster listens on (default: 55432)

set -euo pipefail

mode="${1:-check}"

PGBIN="$(pg_config --bindir)"
PG_REGRESS="$(pg_config --pkglibdir)/pgxs/src/test/regress/pg_regress"
PORT="${PG_REGRESS_PORT:-55432}"
PGDATA="${PGDATA:-/var/lib/postgresql/regress}"

# Some Debian PG packages put pg_regress under .../lib/postgresql/<ver>/lib/pgxs/...
# Resolve to whichever exists.
if [ ! -x "$PG_REGRESS" ]; then
    if [ -x "$(pg_config --pkglibdir)/pgxs/src/test/regress/pg_regress" ]; then
        PG_REGRESS="$(pg_config --pkglibdir)/pgxs/src/test/regress/pg_regress"
    elif command -v pg_regress >/dev/null 2>&1; then
        PG_REGRESS="$(command -v pg_regress)"
    else
        # Debian alternative location.
        cand=$(find /usr/lib/postgresql -name pg_regress 2>/dev/null | head -1 || true)
        if [ -n "$cand" ]; then
            PG_REGRESS="$cand"
        else
            echo "run-regress: cannot find pg_regress" >&2
            exit 2
        fi
    fi
fi

init_cluster() {
    if [ ! -s "$PGDATA/PG_VERSION" ]; then
        rm -rf "$PGDATA"
        "$PGBIN/initdb" -U postgres --auth=trust --no-sync \
            --locale=C.UTF-8 --encoding=UTF8 -D "$PGDATA" >/dev/null
    fi
}

start_cluster() {
    "$PGBIN/pg_ctl" -D "$PGDATA" -l "$PGDATA/server.log" -w \
        -o "-p $PORT -c unix_socket_directories=/tmp -c listen_addresses=" \
        start
}

stop_cluster() {
    "$PGBIN/pg_ctl" -D "$PGDATA" -m fast stop >/dev/null 2>&1 || true
}

trap stop_cluster EXIT

case "$mode" in
    check)
        init_cluster
        start_cluster

        # pg_regress runs in the source tree so it picks up sql/ and expected/.
        cd /home/postgres/zhparser
        "$PG_REGRESS" \
            --inputdir=. \
            --outputdir=. \
            --bindir="$PGBIN" \
            --host=/tmp \
            --port="$PORT" \
            --user=postgres \
            zhparser zhparser_hardening

        echo "pg_regress: PASS"
        ;;

    refresh)
        if [ ! -d /host-expected ]; then
            echo "refresh mode requires -v <path>:/host-expected mounted" >&2
            exit 2
        fi
        init_cluster
        start_cluster

        cd /home/postgres/zhparser

        # pg_regress bails if expected/<test>.out is missing. For refresh,
        # ensure all expected files exist (empty if necessary) so the
        # diff step runs and we get the corresponding results/<test>.out
        # which we then promote to the host.
        for t in zhparser zhparser_hardening; do
            [ -f "expected/$t.out" ] || : > "expected/$t.out"
        done

        set +e
        "$PG_REGRESS" \
            --inputdir=. \
            --outputdir=. \
            --bindir="$PGBIN" \
            --host=/tmp \
            --port="$PORT" \
            --user=postgres \
            zhparser zhparser_hardening
        rc=$?
        set -e

        for t in zhparser zhparser_hardening; do
            if [ -f "results/$t.out" ]; then
                cp "results/$t.out" "/host-expected/$t.out"
                echo "refresh: wrote /host-expected/$t.out"
            else
                echo "refresh: results/$t.out missing" >&2
            fi
        done
        echo "refresh: done (pg_regress rc=$rc)"
        ;;

    shell)
        init_cluster
        start_cluster
        echo "Cluster up on /tmp:$PORT (postgres/trust). Type 'exit' to stop."
        exec bash
        ;;

    *)
        echo "usage: $0 [check|refresh|shell]" >&2
        exit 2
        ;;
esac
