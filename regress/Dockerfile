# syntax=docker/dockerfile:1.6
#
# Minimal regression-test container for zhparser.
#
# Purpose-built for `make installcheck` only — not a production image.
# Source comes from the build context (the patched zhparser tree) so the
# image always tests the working copy you have on disk.
#
# Examples:
#   # Default check against PG 16
#   docker build -f regress/Dockerfile -t zhparser-regress:pg16 .
#   docker run --rm zhparser-regress:pg16
#
#   # PG 17
#   docker build -f regress/Dockerfile --build-arg PG_VERSION=17 \
#       -t zhparser-regress:pg17 .
#   docker run --rm zhparser-regress:pg17
#
#   # Refresh expected/*.out — diffs are written to a host directory.
#   docker run --rm -v "$PWD/expected:/host-expected" \
#       zhparser-regress:pg16 refresh
#
#   # Drop into a debug shell with the cluster running.
#   docker run --rm -it zhparser-regress:pg16 shell

ARG PG_VERSION=16

# ===========================================================================
# Stage 1: build SCWS + zhparser
# ===========================================================================
FROM postgres:${PG_VERSION}-bookworm AS builder

ARG SCWS_VERSION=1.2.3

RUN set -eux; \
    apt-get update; \
    apt-get install -y --no-install-recommends \
        build-essential \
        ca-certificates \
        curl \
        pkg-config \
        autoconf \
        automake \
        libtool \
        m4 \
        postgresql-server-dev-${PG_MAJOR}; \
    rm -rf /var/lib/apt/lists/*

# Build SCWS from source.
RUN set -eux; \
    curl -fsSL "https://github.com/hightman/scws/archive/refs/tags/${SCWS_VERSION}.tar.gz" \
        -o /tmp/scws.tar.gz; \
    mkdir /tmp/scws && tar -xzf /tmp/scws.tar.gz -C /tmp/scws --strip-components=1; \
    cd /tmp/scws; \
    touch README; \
    aclocal; autoconf; autoheader; libtoolize --force; automake --add-missing; \
    ./configure --prefix=/usr/local; \
    make -j"$(nproc)"; \
    make install; \
    ldconfig

# Build zhparser from the build context (the patched tree on disk).
COPY . /src/zhparser
RUN set -eux; \
    cd /src/zhparser; \
    make clean || true; \
    make PG_CONFIG="$(which pg_config)"; \
    make PG_CONFIG="$(which pg_config)" install; \
    PKGLIB="$(pg_config --pkglibdir)"; \
    SHAREDIR="$(pg_config --sharedir)"; \
    mkdir -p /artifacts/lib /artifacts/extension /artifacts/tsearch_data; \
    cp "$PKGLIB/zhparser.so" /artifacts/lib/; \
    cp "$SHAREDIR/extension/"zhparser*    /artifacts/extension/; \
    cp "$SHAREDIR/tsearch_data/dict.utf8.xdb"  /artifacts/tsearch_data/; \
    cp "$SHAREDIR/tsearch_data/rules.utf8.ini" /artifacts/tsearch_data/

# ===========================================================================
# Stage 2: minimal runtime image for pg_regress
# ===========================================================================
FROM postgres:${PG_VERSION}-bookworm

# We need pg_regress (only shipped in -dev) and diffutils.
RUN set -eux; \
    apt-get update; \
    apt-get install -y --no-install-recommends \
        postgresql-server-dev-${PG_MAJOR} \
        diffutils \
        ca-certificates; \
    rm -rf /var/lib/apt/lists/*

# Copy SCWS runtime and zhparser artifacts.
COPY --from=builder /usr/local/lib/libscws.so* /usr/local/lib/

# zhparser artifacts staged at /artifacts/ in builder; install into the
# right pg_config-derived directories of the runtime image.
COPY --from=builder /artifacts/lib/zhparser.so       /tmp/artifacts/lib/
COPY --from=builder /artifacts/extension/            /tmp/artifacts/extension/
COPY --from=builder /artifacts/tsearch_data/         /tmp/artifacts/tsearch_data/

RUN set -eux; \
    PKGLIB="$(pg_config --pkglibdir)"; \
    SHAREDIR="$(pg_config --sharedir)"; \
    install -m 0755 /tmp/artifacts/lib/zhparser.so "$PKGLIB/"; \
    cp /tmp/artifacts/extension/*    "$SHAREDIR/extension/"; \
    cp /tmp/artifacts/tsearch_data/* "$SHAREDIR/tsearch_data/"; \
    rm -rf /tmp/artifacts; \
    ldconfig

# Bring the patched source tree into the runtime image so pg_regress can
# read sql/ and expected/, and write its results/ subdirectory.
COPY --from=builder --chown=postgres:postgres /src/zhparser /home/postgres/zhparser

# Entry-point script comes from the builder stage so we don't need a
# second context scan.
COPY --from=builder /src/zhparser/regress/run-regress.sh /usr/local/bin/run-regress
RUN chmod +x /usr/local/bin/run-regress

ENV PGDATA=/var/lib/postgresql/regress \
    LANG=C.UTF-8 \
    PG_REGRESS_PORT=55432

USER postgres
WORKDIR /home/postgres/zhparser

ENTRYPOINT ["/usr/local/bin/run-regress"]
CMD ["check"]
