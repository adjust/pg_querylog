#include "postgres.h"
#include "funcapi.h"
#include "port/atomics.h"
#include "utils/builtins.h"
#include "access/htup_details.h"
#include "pg_querylog.h"

PG_FUNCTION_INFO_V1( get_queries );

typedef struct queries_view_fctx {
	int index;
} queries_view_fctx;

Datum
get_queries(PG_FUNCTION_ARGS)
{
	MemoryContext		old_mcxt;
	FuncCallContext	   *funccxt;
	queries_view_fctx	   *usercxt;

	bool	only_running = PG_GETARG_BOOL(0);
	bool	skip_overflow = PG_GETARG_BOOL(1);

	if (SRF_IS_FIRSTCALL())
	{
		TupleDesc	tupdesc;

		funccxt = SRF_FIRSTCALL_INIT();

		old_mcxt = MemoryContextSwitchTo(funccxt->multi_call_memory_ctx);

		usercxt = (queries_view_fctx *) palloc(sizeof(queries_view_fctx));
		usercxt->index = -1;

		if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
			elog(ERROR, "return type must be a row type");

		funccxt->tuple_desc = BlessTupleDesc(tupdesc);
		funccxt->user_fctx = (void *) usercxt;

		MemoryContextSwitchTo(old_mcxt);
	}

	funccxt = SRF_PERCALL_SETUP();
	usercxt = (queries_view_fctx *) funccxt->user_fctx;

	while (hdr && usercxt->index < hdr->count)
	{
		uint64			gen;
		CollectedQuery *item;
		HeapTuple		htup;
		Datum			values[natts_queries_view];
		bool			isnull[natts_queries_view];

		usercxt->index++;

		item = (CollectedQuery *) (hdr->queries + usercxt->index);
		if (item->pid == 0)
			continue;

		if (only_running && !item->running)
			continue;

		if (skip_overflow && item->overflow)
			continue;

		pg_read_barrier();
		gen = item->gen;
		if (!pg_atomic_unlocked_test_flag(&item->is_free))
			continue;

		if (item->magic != PG_QUERYLOG_ITEM_MAGIC)
			elog(ERROR, "magic programming error");

		MemSet(values, 0, sizeof(values));
		MemSet(isnull, 0, sizeof(isnull));

		values[att_queries_start] = TimestampTzGetDatum(item->start);
		if (item->end != 0)
			values[att_queries_end] = TimestampTzGetDatum(item->end);
		else
			isnull[att_queries_end] = true;

		values[att_queries_pid] = Int32GetDatum(item->pid);
		values[att_queries_running] = BoolGetDatum(item->running);
		values[att_queries_overflow] = BoolGetDatum(item->overflow);
		values[att_queries_query] = PointerGetDatum(
				cstring_to_text_with_len(item->buf, item->querylen));

		if (item->params)
			values[att_queries_params] = CStringGetTextDatum(item->params);
		else
			isnull[att_queries_params] = true;

		pg_read_barrier();
		if (gen != item->gen)
			continue;

		/* form output tuple */
		htup = heap_form_tuple(funccxt->tuple_desc, values, isnull);

		SRF_RETURN_NEXT(funccxt, HeapTupleGetDatum(htup));
	}

	SRF_RETURN_DONE(funccxt);
}
