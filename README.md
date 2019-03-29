[![Build Status](https://travis-ci.org/ildus/pg_querylog.svg?branch=master)](https://travis-ci.org/ildus/pg_querylog)

pg_querylog
===========

Show queries running on PostgreSQL backends.

Installation
------------

	make && make install
	echo `session_preload_libraries='pg_querylog'` > postgresql.conf
	psql postgres -c "create schema querylog; create extension pg_querylog schema querylog;"

Using
-----

Get all saved queries using `get_queries` function:

	select * from querylog.get_queries()

Result columns:

* `pid` - backend process ID
* `query` - running (or runned) query
* `params` - query parameters in case of parametrized query
* `start_time` - query start time
* `end_time` - query end time (if `running` == false)
* `running` - running status
* `overflow` - shows that `buffer_size` is enough for this item

Parameters:

* `pg_querylog.buffer_size` - buffer size for each saved query, calculated as sum of lenghts of query and its params.
* `pg_querylog.enabled` - controls saving queries.

To get only running queries `running_queries` view is used:

	select * from querylog.running_queries;
