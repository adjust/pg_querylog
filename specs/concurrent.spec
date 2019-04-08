setup
{
	create schema querylog;
	create extension pg_querylog schema querylog;
	create table one(a int);
}

teardown
{
	drop schema querylog cascade;
}

session "s1"
step "s1init" {
	begin work;
	lock table one in access exclusive mode;
}
step "getr" { select query, params from querylog.running_queries; }
step "getq" { select query, params from querylog.running_queries; }
step "s1end" { commit work; }

session "s2"
step "q1" {
	prepare p1 as select $1, * from one;
	execute p1(1);
}

session "s3"
step "q2" {
	select 1;
}

permutation "s1init" "q1" "q2" "getr" "getq" "s1end"
