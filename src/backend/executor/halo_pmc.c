#include "postgres.h"

#include "catalog/namespace.h"
#include "executor/halo_pmc.h"
#include "executor/spi.h"
#include "utils/builtins.h"
#include "utils/lsyscache.h"
#include "utils/varlena.h"

#ifdef __linux__
#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

char *halo_pmc_list = NULL;

#ifdef __linux__
typedef struct PmcDefinition
{
	char name[40];
	uint32 type;
	uint64 config;
	uint64 config1;
	bool exclude_kernel;
} PmcDefinition;

typedef struct PmcReadResult
{
	uint64 value;
	uint64 enabled;
	uint64 running;
} PmcReadResult;

static bool pmc_loading = false;

static bool
fast_read(HaloPmcEvent *event, uint64 *value)
{
#if defined(__x86_64__) || defined(__i386__)
	struct perf_event_mmap_page *page = event->page;
	uint32 seq;
	uint32 index;
	uint32 width;
	int64 offset;
	uint32 lo;
	uint32 hi;
	int64 count;
	bool available;

	if (page == NULL)
		return false;
	do
	{
		seq = __atomic_load_n(&page->lock, __ATOMIC_ACQUIRE);
		if (seq & 1)
			continue;
		index = page->index;
		offset = page->offset;
		width = page->pmc_width;
		available = page->cap_user_rdpmc && index != 0 && width > 0 && width <= 64;
		if (available)
		{
			__asm__ volatile("rdpmc" : "=a"(lo), "=d"(hi) : "c"(index - 1) : "memory");
			count = (int64) (((uint64) hi << 32) | lo);
			if (width < 64)
				count = (int64) ((uint64) count << (64 - width)) >> (64 - width);
			*value = (uint64) offset + (uint64) count;
		}
		__atomic_thread_fence(__ATOMIC_ACQUIRE);
	} while ((seq & 1) || seq != __atomic_load_n(&page->lock, __ATOMIC_RELAXED));
	return available;
#else
	return false;
#endif
}
#endif

bool
halo_pmc_start(HaloPmc *pmc)
{
	memset(pmc, 0, sizeof(*pmc));

#ifdef __linux__
	if (halo_pmc_list && halo_pmc_list[0] && !pmc_loading)
	{
		PmcDefinition definitions[HALO_PMC_MAX];
		int ndefinitions = 0;
		char *raw = pstrdup(halo_pmc_list);
		List *names = NIL;
		ListCell *lc;
		Oid public_oid = get_namespace_oid("public", true);
		int i;
		long page_size = sysconf(_SC_PAGESIZE);

		if (!SplitGUCList(raw, ',', &names))
		{
			elog(WARNING, "invalid halo_pmc_list");
			pfree(raw);
			return false;
		}
		if (!OidIsValid(public_oid) || !OidIsValid(get_relname_relid("pmc_name", public_oid)))
		{
			elog(WARNING, "halo PMC: public.pmc_name does not exist");
			list_free(names);
			pfree(raw);
			return false;
		}
		pmc_loading = true;
		PG_TRY();
		{
			if (SPI_connect() != SPI_OK_CONNECT)
				elog(ERROR, "halo PMC: SPI_connect failed");
			if (SPI_execute("SELECT pcm_name, pmutype, pe_config, config1, config2 FROM public.pmc_name", true, 0) != SPI_OK_SELECT)
				elog(ERROR, "halo PMC: cannot read public.pmc_name");
			foreach(lc, names)
			{
				const char *name = lfirst(lc);
				uint64 row;
				bool found = false;

				for (row = 0; row < SPI_processed; row++)
				{
					HeapTuple tuple = SPI_tuptable->vals[row];
					TupleDesc desc = SPI_tuptable->tupdesc;
					char *table_name = SPI_getvalue(tuple, desc, 1);
					bool nulls[4];
					int32 values[4];
					int k;

					if (!table_name || pg_strcasecmp(table_name, name) != 0)
						continue;
					found = true;
					if (ndefinitions == HALO_PMC_MAX)
					{
						elog(WARNING, "halo PMC: at most %d counters supported", HALO_PMC_MAX);
						break;
					}
					for (k = 0; k < 4; k++)
						values[k] = DatumGetInt32(SPI_getbinval(tuple, desc, k + 2, &nulls[k]));
					if (nulls[0] || nulls[1] || nulls[2] || nulls[3])
					{
						elog(WARNING, "halo PMC: incomplete definition for %s", name);
						break;
					}
					strlcpy(definitions[ndefinitions].name, table_name, sizeof(definitions[0].name));
					definitions[ndefinitions].type = (uint32) values[0];
					definitions[ndefinitions].config = (uint32) values[1];
					definitions[ndefinitions].config1 = (uint32) values[2];
					definitions[ndefinitions].exclude_kernel = values[3] != 0;
					ndefinitions++;
					break;
				}
				if (!found)
					elog(WARNING, "halo PMC: unknown counter %s", name);
			}
			SPI_finish();
		}
		PG_FINALLY();
		{
			pmc_loading = false;
		}
		PG_END_TRY();
		list_free(names);
		pfree(raw);

		for (i = 0; i < ndefinitions; i++)
		{
			struct perf_event_attr attr;
			HaloPmcEvent *event = &pmc->events[pmc->count];
			int fd;

			memset(&attr, 0, sizeof(attr));
			attr.size = sizeof(attr);
			attr.type = definitions[i].type;
			attr.config = definitions[i].config;
			attr.config1 = definitions[i].config1;
			attr.disabled = 1;
			attr.exclude_kernel = definitions[i].exclude_kernel;
			attr.exclude_hv = definitions[i].exclude_kernel;
			attr.read_format = PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING;
			fd = syscall(__NR_perf_event_open, &attr, 0, -1, -1, 0);
			if (fd < 0)
			{
				elog(WARNING, "halo PMC: cannot open %s: %m", definitions[i].name);
				continue;
			}
			event->fd = fd;
			strlcpy(event->name, definitions[i].name, sizeof(event->name));
			if (page_size > 0)
			{
				event->page = mmap(NULL, page_size, PROT_READ, MAP_SHARED, fd, 0);
				if (event->page == MAP_FAILED)
					event->page = NULL;
			}
			if (ioctl(fd, PERF_EVENT_IOC_RESET, 0) < 0 || ioctl(fd, PERF_EVENT_IOC_ENABLE, 0) < 0)
			{
				elog(WARNING, "halo PMC: cannot enable %s: %m", event->name);
				if (event->page)
					munmap(event->page, page_size);
				close(fd);
				continue;
			}
			pmc->count++;
		}
	}
#endif
	return pmc->count > 0;
}

void
halo_pmc_sample_begin(HaloPmc *pmc)
{
#ifdef __linux__
	int i;
	for (i = 0; i < pmc->count; i++)
		pmc->events[i].before_valid = fast_read(&pmc->events[i], &pmc->events[i].before);
#endif
}

void
halo_pmc_sample_end(HaloPmc *pmc)
{
#ifdef __linux__
	int i;
	for (i = 0; i < pmc->count; i++)
	{
		HaloPmcEvent *event = &pmc->events[i];
		uint64 after;
		if (event->before_valid && fast_read(event, &after))
		{
			event->total += after - event->before;
			event->samples++;
		}
		else
			event->missed++;
	}
#endif
}

void
halo_pmc_finish(HaloPmc *pmc, bool report)
{
#ifdef __linux__
	int i;
	long page_size = sysconf(_SC_PAGESIZE);
	for (i = 0; i < pmc->count; i++)
	{
		HaloPmcEvent *event = &pmc->events[i];
		PmcReadResult result;
		ssize_t bytes;
		ioctl(event->fd, PERF_EVENT_IOC_DISABLE, 0);
		bytes = read(event->fd, &result, sizeof(result));
		if (event->page && page_size > 0)
			munmap(event->page, page_size);
		close(event->fd);
		if (report && bytes == sizeof(result))
			elog(INFO, "halo PMC %s: whole=" UINT64_FORMAT " enabled=" UINT64_FORMAT " running=" UINT64_FORMAT " loop=" UINT64_FORMAT " samples=" UINT64_FORMAT " missed=" UINT64_FORMAT,
				 event->name, result.value, result.enabled, result.running,
				 event->total, event->samples, event->missed);
		else if (report)
			elog(WARNING, "halo PMC: cannot read %s", event->name);
	}
#endif
}
