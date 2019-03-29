# contrib/pg_querylog/Makefile

MODULE_big = pg_querylog
OBJS= pg_querylog.o pl_funcs.o $(WIN32RES)

EXTENSION = pg_querylog
EXTVERSION = 0.1
PGFILEDESC = "PostgreSQL running queries viewer"
REGRESS = basic

DATA_built = $(EXTENSION)--$(EXTVERSION).sql

ifndef PG_CONFIG
PG_CONFIG = pg_config
endif

EXTRA_REGRESS_OPTS=--temp-config=$(CURDIR)/conf.add

PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

$(EXTENSION)--$(EXTVERSION).sql: init.sql
	cat $^ > $@
