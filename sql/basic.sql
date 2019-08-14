create schema querylog;
create extension pg_querylog schema querylog;

-- enable
set pg_querylog.enabled=on;
show pg_querylog.enabled;

select pid > 0 as true, query, params,
	extract(year from start_time) = extract(year from now()) as true,
	end_time, running, overflow
from querylog.get_queries();

select pid > 0 as true, query, params, overflow,
	extract(year from start_time) > 0 as true
from querylog.running_queries;

prepare p1 as
	select $1 as c1, $2 as c2, pid > 0 as true, query, params,
		extract(year from start_time) > 0 as true
	from querylog.running_queries;

execute p1(1, 2);
execute p1('{1,2}'::int[], 1);
execute p1('{"one": "two"}'::jsonb, '{"one", "two"}'::text[]);

prepare p2 as
	select length($1) as plen, length(query) + length(coalesce(params, '')) as buflen, overflow
	from querylog.running_queries;

execute p2(repeat('a', 10000));
execute p2(repeat('a', 10240));
execute p2(repeat('a', 10340));
execute p2(repeat('b', 20000));
execute p2(repeat('c', 30000));

drop schema querylog cascade;
