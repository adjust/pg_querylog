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
	select pid, query, params, start_time, overflow from get_queries(true, false);
