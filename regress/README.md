# zhparser regression-test container

Minimal, single-purpose Docker image that builds SCWS + zhparser from the
working copy on disk and runs `pg_regress` against it. Intended to give a
fast green/red signal after editing the C code.

## Files

| Path | Purpose |
| --- | --- |
| `Dockerfile`     | 2-stage build: builder (toolchain + SCWS + zhparser) → runtime (PG image with artifacts copied in) |
| `run-regress.sh` | Container entrypoint. Modes: `check` / `refresh` / `shell` |
| `regress.sh`     | Host-side wrapper. Hides `docker build` / `docker run` ceremony, supports `matrix` mode |

## Quick start

```bash
# From the project root.
regress/regress.sh check          # PG 16 by default
regress/regress.sh check 17       # PG 17
regress/regress.sh matrix         # 16 + 17 + 18

# Drop into a shell with the test cluster running:
regress/regress.sh shell 16
# (inside) psql -h /tmp -p 55432 -U postgres
```

## Refreshing expected output

The 2.4 patch includes a real bug-fix for lex-type truncation: tokens of
type `y` (modal) and `z` (status) used to be silently coerced into `x`
(unknown). The pre-existing `expected/zhparser-{alpine,debian}.out` files
encode the old, buggy behaviour and must be regenerated:

```bash
regress/regress.sh refresh 16
git diff expected/        # review carefully
```

`refresh` writes the freshly produced `results/*.out` back into your
working tree's `expected/` directory.

## How it works

The runtime stage runs as the `postgres` user with no superuser daemon:
the entry script calls `initdb`, `pg_ctl start`, then `pg_regress`. The
cluster lives in `$PGDATA=/var/lib/postgresql/regress` and listens on a
unix socket under `/tmp` (port 55432, no TCP). Everything is torn down
on exit via a `trap`.

`pg_regress` runs against the source tree at `/home/postgres/zhparser`,
which is the patched tree copied in from the builder. Both
`sql/zhparser.sql` (the upstream tokenization smoke test) and
`sql/zhparser_hardening.sql` (the 2.4 hardening assertions) execute as
part of the run.

## Why not `docker compose`?

Out of scope. The whole point is a single ephemeral container that
exits 0 / non-zero. If you need multi-service tests later (e.g. a
client + server pair), that's a different picture and warrants compose.
