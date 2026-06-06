#!/usr/bin/env bash
set -euo pipefail

pid=$$
container="testpgzhparser-$pid"

cleanup() {
    docker stop "$container" >/dev/null 2>&1 || true
}
trap cleanup EXIT

docker run --rm --name "$container" -p 5432:5432 -d \
    -e POSTGRES_PASSWORD=somepassword@debian-16 \
    zhparser/zhparser:bookworm-16

for _ in $(seq 1 30); do
    if PGPASSWORD=somepassword@debian-16 psql -h 127.0.0.1 -U postgres \
        -tAc 'select 1' postgres >/dev/null 2>&1; then
        break
    fi
    sleep 1
done

export PGPASSWORD=somepassword@debian-16
if psql -h 127.0.0.1 -X -a -q postgres postgres -f sql/zhparser.sql \
        | diff expected/zhparser-debian.out -; then
    echo "pass!"
else
    echo "do not pass!"
    exit 1
fi
