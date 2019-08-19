#!/bin/bash

set -eux

echo CHECK_CODE=$CHECK_CODE

status=0

# don't forget to "make clean"
make USE_PGXS=1 clean

# perform code analysis if necessary
if [ "$CHECK_CODE" = "clang" ]; then
    scan-build --status-bugs make USE_PGXS=1 || status=$?
    exit $status
fi

# initialize database
initdb

# build extension
make USE_PGXS=1 install

# check build
status=$?
if [ $status -ne 0 ]; then exit $status; fi

echo "shared_preload_libraries = 'pg_querylog'" >> $PGDATA/postgresql.conf
echo "port = 55435" >> $PGDATA/postgresql.conf
pg_ctl start -l /tmp/postgres.log -w

# check startup
status=$?
if [ $status -ne 0 ]; then cat /tmp/postgres.log; fi

# run regression tests
export PG_REGRESS_DIFF_OPTS="-w -U3" # for alpine's diff (BusyBox)
PGPORT=55435 make USE_PGXS=1 installcheck || status=$?

# show diff if it exists
if test -f regression.diffs; then cat regression.diffs; fi

# something's wrong, exit now!
if [ $status -ne 0 ]; then exit 1; fi

# run python tests
export VIRTUAL_ENV_DISABLE_PROMPT=1
set +x
virtualenv /tmp/env && source /tmp/env/bin/activate && pip install testgres pytest pytest-xdist
pytest -n 2 || status=$?
deactivate
set -x

exit $status
