#ifndef PG_BACKLOG_H
#define PG_BACKLOG_H

#include "utils/timestamp.h"

#if PG_VERSION_NUM < 100000
#error "this extension support only postgres starting from 10"
#endif

#define	PG_QUERYLOG_MAGIC		0xAABBCCEE
#define	PG_QUERYLOG_ITEM_MAGIC	0x06054AB5

typedef struct CollectedQuery
{
	uint32			magic;		//used to find corruptions
	pg_atomic_flag	is_free;

	volatile uint64	gen;		//used to check consistency
	int		pid;
	bool	running;
	bool	overflow;			//buffer is not enough for this query

	TimestampTz		start;
	TimestampTz		end;

	int		querylen;
	int		datalen;
	char	*buf;				//pointer to start of the buffer
	char	*params;			//pointer to start of params in the buffer
} CollectedQuery;

typedef struct BacklogDataHdr
{
	int				count;		/* basicly equal to MaxConnections */
	bool			enabled;
	Size			bufsize;
	CollectedQuery	*queries;
	char			*buffer;	/* the whole buffer */
} BacklogDataHdr;

extern BacklogDataHdr	*hdr;

// view attributes, get_backend_queries
enum {
	att_queries_pid,
	att_queries_query,
	att_queries_params,
	att_queries_start,
	att_queries_end,
	att_queries_running,
	att_queries_overflow,
	natts_queries_view
};

#endif
