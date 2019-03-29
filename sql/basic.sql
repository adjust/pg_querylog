create schema backlog;
create extension pg_querylog schema backlog;
select pid > 0 as true, query, params,
	extract(year from start_time) = extract(year from now()) as true,
	end_time, running, overflow
from backlog.get_queries();

select pid > 0 as true, query, params,
	extract(year from start_time) > 0 as true
from backlog.running_queries;

prepare p1 as select $1 as c1, $2 as c2, pid > 0 as true, query, params,
	extract(year from start_time) > 0 as true
from backlog.running_queries;

execute p1(1, 2);
execute p1('{1,2}'::int[], 1);
execute p1('{"one": "two"}'::jsonb, '{"one", "two"}'::text[]);

drop schema backlog cascade;
