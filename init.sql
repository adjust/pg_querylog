/* don't change order of fields, it will break `get_queries` function */
create type query_item as (
	pid			int,
	query		text,
	params		text,
	start_time	timestamp with time zone,
	end_time	timestamp with time zone,
	running		bool,
	overflow	bool
);

create or replace function get_queries(
	only_running	boolean default false,
	skip_overflow	boolean default false
)
returns query_item as 'MODULE_PATHNAME'
language c strict;

create or replace view running_queries as
	select pid, params, start_time, overflow, regexp_replace(query, '\s*\n', E'\n', 'g') as query from get_queries(true, false);
