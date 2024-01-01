ARG PG_CONTAINER_VERSION=16
FROM docker.io/library/postgres:${PG_CONTAINER_VERSION}-alpine as builder

RUN set -ex \
  && apk --no-cache add git build-base linux-headers make postgresql-dev automake libtool autoconf m4

RUN set -ex \
  && git clone --branch 1.2.3 --single-branch --depth 1 https://github.com/hightman/scws.git \
  && cd scws \
  && touch README;aclocal;autoconf;autoheader;libtoolize;automake --add-missing \
  && ./configure \
  && make install

RUN set -ex \
  && git clone --branch master --single-branch --depth 1 https://github.com/amutu/zhparser.git \
  && cd zhparser \
  && make install

FROM docker.io/library/postgres:${PG_CONTAINER_VERSION}-alpine
ENV LANG zh_CN.UTF-8

COPY --from=builder /usr/local/lib/postgresql/zhparser.so /usr/local/lib/postgresql/
COPY --from=builder /usr/local/lib/libscws.* /usr/local/lib/
COPY --from=builder /usr/local/share/postgresql/extension/zhparser* /usr/local/share/postgresql/extension/
COPY --from=builder /usr/local/lib/postgresql/bitcode/zhparser* /usr/local/lib/postgresql/bitcode/
COPY --from=builder /usr/local/share/postgresql/tsearch_data/dict.utf8.xdb /usr/local/share/postgresql/tsearch_data/
COPY --from=builder /usr/local/share/postgresql/tsearch_data/rules.utf8.ini /usr/local/share/postgresql/tsearch_data/
