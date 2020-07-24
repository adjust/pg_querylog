/*
 * pg_querylog.c
 *      PostgreSQL running queries viewer
 */
#include "postgres.h"
#include "access/xact.h"
#include "fmgr.h"
#include "libpq/libpq-be.h"
#include "miscadmin.h"
#include "postmaster/autovacuum.h"
#include "storage/dsm.h"
#include "storage/ipc.h"
#include "storage/shm_mq.h"
#include "storage/shm_toc.h"
#include "string.h"
#include "tcop/tcopprot.h"
#include "utils/builtins.h"
#include "utils/guc.h"
#include "utils/ps_status.h"
#include "executor/executor.h"
#include "catalog/pg_type.h"
#include "lib/stringinfo.h"
#include "utils/lsyscache.h"

#include "pg_querylog.h"

PG_MODULE_MAGIC;

void		_PG_init(void);
void		_PG_fini(void);

/* global variables */
static int				buffer_size_setting = 0;
static shm_toc		   *toc = NULL;
BacklogDataHdr		   *pgl_shared_hdr = NULL;
CollectedQuery		   *pgl_shared_queries = NULL;
char				   *pgl_shared_buffer = NULL;		/* the whole buffer */
static bool				shmem_initialized = false;

/* local */
static CollectedQuery  *backend_query = NULL;
static bool				using_dsm = false;
static dsm_segment	   *seg = NULL;

static ExecutorStart_hook_type	pg_querylog_executor_start_hook_next = NULL;
static ExecutorEnd_hook_type	pg_querylog_executor_end_hook_next = NULL;

#define safe_strlen(s) ((s) ? strlen(s) : 0)

/* handles error cases */
static void
xact_finish_callback(XactEvent event, void *arg)
{
	if (backend_query && backend_query->running)
		backend_query->running = false;
}

static void
setup_gucs(bool basic)
{
	if (basic)
	{
		DefineCustomIntVariable(
			"pg_querylog.buffer_size",
			"Buffer size for each query", NULL,
			&buffer_size_setting,
			10,		/* 10KB */
			10,
			INT_MAX,
			PGC_SUSET,
			GUC_UNIT_KB,
			NULL, NULL, NULL
		);
	}
	else
	{
		DefineCustomBoolVariable(
			"pg_querylog.enabled",
			"Enable logging queries", NULL,
			&pgl_shared_hdr->enabled,
			true,
			PGC_SUSET,
			0, NULL, NULL, NULL
		);
	}
}

/* copied from pg code */
static void
print_literal(StringInfo s, Oid typid, char *outputstr)
{
	const char *valptr;

	switch (typid)
	{
		case INT2OID:
		case INT4OID:
		case INT8OID:
		case OIDOID:
		case FLOAT4OID:
		case FLOAT8OID:
		case NUMERICOID:
			/* NB: We don't care about Inf, NaN et al. */
			appendStringInfoString(s, outputstr);
			break;

		case BITOID:
		case VARBITOID:
			appendStringInfo(s, "B'%s'", outputstr);
			break;

		case BOOLOID:
			if (strcmp(outputstr, "t") == 0)
				appendStringInfoString(s, "true");
			else
				appendStringInfoString(s, "false");
			break;

		default:
			appendStringInfoChar(s, '\'');
			for (valptr = outputstr; *valptr; valptr++)
			{
				char		ch = *valptr;

				if (SQL_STR_DOUBLE(ch, false))
					appendStringInfoChar(s, ch);
				appendStringInfoChar(s, ch);
			}
			appendStringInfoChar(s, '\'');
			break;
	}
}

static void
pg_querylog_executor_start_hook(QueryDesc *queryDesc, int eflags)
{
	if (pgl_shared_hdr && pgl_shared_hdr->enabled)
	{
		char		   *itembuf;

		if (!backend_query && MyBackendId)
		{
			backend_query = &pgl_shared_queries[MyBackendId - 1];
			backend_query->magic = PG_QUERYLOG_ITEM_MAGIC;
			backend_query->running = false;
			backend_query->gen = 0;
			pg_atomic_init_flag(&backend_query->is_free);
			backend_query->pid = MyProcPid;

			itembuf = QUERYBUF(pgl_shared_hdr, MyBackendId - 1);
			memset(itembuf, 0, pgl_shared_hdr->bufsize);
			pg_write_barrier();
		}

		if (backend_query)
		{
			StringInfoData	data;
			int				i;

			Assert(backend_query->magic = PG_QUERYLOG_ITEM_MAGIC);
			initStringInfo(&data);

			// mark is not free,
			// we don't check for result here, since there could some
			// error before clearing flag, and we don't want to depend
			// on that.
			pg_atomic_test_set_flag(&backend_query->is_free);

			backend_query->gen++;
			backend_query->running = true;
			backend_query->start = GetCurrentTimestamp();
			backend_query->end = 0;

			appendStringInfoString(&data, queryDesc->sourceText);
			backend_query->querylen = data.len;
			itembuf = QUERYBUF(pgl_shared_hdr, MyBackendId - 1);

			// collect params
			if (queryDesc->params && data.len < pgl_shared_hdr->bufsize)
			{
				backend_query->params_offset = data.len;

				for (i = 0; i < queryDesc->params->numParams; i++)
				{
					ParamExternData	*param = &queryDesc->params->params[i];

					if (param->isnull)
					{
						if (i > 0)
							appendStringInfoChar(&data, ',');

						appendStringInfoString(&data, "NULL");
					}
					else if (param->ptype != InvalidOid)
					{
						Oid			typoutput;	/* output function */
						bool		typisvarlena;
						char	   *val;

						getTypeOutputInfo(param->ptype, &typoutput, &typisvarlena);
						val = OidOutputFunctionCall(typoutput, param->value);
						if (i > 0)
							appendStringInfoChar(&data, ',');

						print_literal(&data, param->ptype, val);
						pfree(val);
					}
				}
			}
			else
				backend_query->params_offset = 0;

			backend_query->datalen = data.len;
			backend_query->overflow = (data.len >= pgl_shared_hdr->bufsize);

			memcpy(itembuf, data.data, backend_query->overflow ?
				pgl_shared_hdr->bufsize - 1: data.len + 1);
			itembuf[pgl_shared_hdr->bufsize - 1] = '\0';
			resetStringInfo(&data);
			pg_atomic_clear_flag(&backend_query->is_free);
		}
	}

	if (pg_querylog_executor_start_hook_next)
		pg_querylog_executor_start_hook_next(queryDesc, eflags);
	else
		standard_ExecutorStart(queryDesc, eflags);
}

static void
pg_querylog_executor_end_hook(QueryDesc *queryDesc)
{
	if (pgl_shared_hdr->enabled && backend_query)
	{
		pg_atomic_test_set_flag(&backend_query->is_free);
		backend_query->gen++;
		backend_query->end = GetCurrentTimestamp();
		backend_query->running = false;
		pg_atomic_clear_flag(&backend_query->is_free);
	}

	if (pg_querylog_executor_end_hook_next)
		pg_querylog_executor_end_hook_next(queryDesc);
	else
		standard_ExecutorEnd(queryDesc);
}

static void
install_hooks()
{
	pg_querylog_executor_start_hook_next = ExecutorStart_hook;
	ExecutorStart_hook = pg_querylog_executor_start_hook;

	pg_querylog_executor_end_hook_next = ExecutorEnd_hook;
	ExecutorEnd_hook = pg_querylog_executor_end_hook;
}

static void
uninstall_hooks(void)
{
	ExecutorStart_hook = pg_querylog_executor_start_hook_next;
	ExecutorEnd_hook = pg_querylog_executor_end_hook_next;
}

static Size
calculate_shmem_size(int bufsize)
{
	shm_toc_estimator	e;
	Size				size;

	Assert(bufsize != 0);
	shm_toc_initialize_estimator(&e);
	shm_toc_estimate_chunk(&e, sizeof(BacklogDataHdr));
	shm_toc_estimate_chunk(&e, sizeof(CollectedQuery) * MaxBackends);
	shm_toc_estimate_chunk(&e, bufsize * MaxBackends);
	shm_toc_estimate_keys(&e, 3);
	size = shm_toc_estimate(&e);

	return size;
}

static void
setup_buffers(Size segsize, Size bufsize, void *addr)
{
	toc = shm_toc_create(PG_QUERYLOG_MAGIC, addr, segsize);
	pgl_shared_hdr = shm_toc_allocate(toc, sizeof(BacklogDataHdr));
	pgl_shared_hdr->count = MaxBackends;
	pgl_shared_hdr->bufsize = bufsize;

	pgl_shared_queries = shm_toc_allocate(toc, sizeof(CollectedQuery) * pgl_shared_hdr->count);
	pgl_shared_buffer = shm_toc_allocate(toc, bufsize * pgl_shared_hdr->count);

	shm_toc_insert(toc, 0, pgl_shared_hdr);
	shm_toc_insert(toc, 1, pgl_shared_queries);
	shm_toc_insert(toc, 2, pgl_shared_buffer);

	memset(pgl_shared_queries, 0, sizeof(CollectedQuery) * pgl_shared_hdr->count);
	memset(pgl_shared_buffer, 0, bufsize * pgl_shared_hdr->count);
}

/*
 * Module load callback
 */
void
_PG_init(void)
{
	void   *addr;
	bool	found;
	Size	bufsize,
			segsize;

	setup_gucs(true);
	bufsize = MAXALIGN(buffer_size_setting * 1024);
	segsize = calculate_shmem_size(bufsize);

	if (MyProcPort != NULL)
	{
		// we're going different way, using DSM to store our data, and saving
		// dsm pointer in shmem.
		// that's not a good way but we know that shared memory has some
		// space at the end which we can use here
		LWLockAcquire(AddinShmemInitLock, LW_EXCLUSIVE);

		addr = ShmemInitStruct("pg_querylog dsm", sizeof(dsm_handle), &found);
		if (found)
		{
			seg = dsm_attach(*((dsm_handle *) addr));
			if (seg == NULL)
			{
				elog(LOG, "pg_querylog: could not find dsm segment");
				return;
			}
			dsm_pin_mapping(seg);
			addr = dsm_segment_address(seg);

			toc = shm_toc_attach(PG_QUERYLOG_MAGIC, addr);
			pgl_shared_hdr = shm_toc_lookup(toc, 0, false);
			pgl_shared_queries = shm_toc_lookup(toc, 1, false);
			pgl_shared_buffer = shm_toc_lookup(toc, 2, false);
		} else {
			seg = dsm_create(segsize, DSM_CREATE_NULL_IF_MAXSEGMENTS);
			if (seg == NULL)
			{
				elog(LOG, "pg_querylog: could not create dsm segment");
				return;
			}
			*((dsm_handle *) addr) = dsm_segment_handle(seg);
			addr = dsm_segment_address(seg);
			dsm_pin_segment(seg);
			dsm_pin_mapping(seg);
			setup_buffers(segsize, bufsize, addr);
			elog(LOG, "pg_querylog initialized");
		}

		LWLockRelease(AddinShmemInitLock);

		using_dsm = true;
		shmem_initialized = true;
		install_hooks(false);
		setup_gucs(false);
	}

	RegisterXactCallback(xact_finish_callback, NULL);
}

/*
 * Module unload callback
 */
void
_PG_fini(void)
{
	if (shmem_initialized)
	{
		uninstall_hooks();
		if (seg)
			dsm_detach(seg);
	}
}
