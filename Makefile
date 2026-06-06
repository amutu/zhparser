# contrib/zhparser/Makefile

MODULE_big = zhparser
OBJS = zhparser.o

EXTENSION = zhparser
DATA = zhparser--1.0.sql zhparser--unpackaged--1.0.sql \
       zhparser--1.0--2.0.sql zhparser--2.0.sql \
       zhparser--2.0--2.1.sql zhparser--2.1.sql \
       zhparser--2.1--2.2.sql zhparser--2.2.sql \
       zhparser--2.3.sql \
       zhparser--2.3--2.4.sql zhparser--2.4.sql
DATA_TSEARCH = dict.utf8.xdb rules.utf8.ini

REGRESS = zhparser zhparser_hardening

# ----------------------------------------------------------------------------
# SCWS detection
#
# Order of precedence:
#   1. SCWS_HOME explicitly set (legacy behavior; kept for back-compat).
#   2. pkg-config --exists scws  ->  use pkg-config flags.
#   3. fall back to /usr/local.
# ----------------------------------------------------------------------------
ifeq ($(origin SCWS_HOME), undefined)
  ifeq ($(shell pkg-config --exists scws && echo yes),yes)
    SCWS_CFLAGS := $(shell pkg-config --cflags scws)
    SCWS_LIBS   := $(shell pkg-config --libs scws)
  else
    SCWS_HOME ?= /usr/local
    SCWS_CFLAGS := -I$(SCWS_HOME)/include/scws
    SCWS_LIBS   := -lscws -L$(SCWS_HOME)/lib -Wl,-rpath -Wl,$(SCWS_HOME)/lib
  endif
else
  SCWS_CFLAGS := -I$(SCWS_HOME)/include/scws
  SCWS_LIBS   := -lscws -L$(SCWS_HOME)/lib -Wl,-rpath -Wl,$(SCWS_HOME)/lib
endif

PG_CPPFLAGS = $(SCWS_CFLAGS) -Wformat -Wformat-security
SHLIB_LINK  = $(SCWS_LIBS)

PG_CONFIG ?= pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
