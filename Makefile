# contrib/zhparser/Makefile

MODULE_big = zhparser
OBJS = zhparser.o

EXTENSION = zhparser
DATA = zhparser--1.0.sql zhparser--unpackaged--1.0.sql \
	   zhparser--1.0--2.0.sql zhparser--2.0.sql zhparser--2.0--2.1.sql zhparser--2.1.sql zhparser--2.1--2.2.sql
DATA_TSEARCH = dict.utf8.xdb rules.utf8.ini

REGRESS = zhparser

SCWS_HOME ?= /usr/local
PG_CPPFLAGS = -I$(SCWS_HOME)/include/scws 
SHLIB_LINK = -lscws -L$(SCWS_HOME)/lib -Wl,-rpath -Wl,$(SCWS_HOME)/lib

PG_CONFIG ?= pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
