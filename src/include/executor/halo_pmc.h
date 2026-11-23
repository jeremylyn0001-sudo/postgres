#ifndef HALO_PMC_H
#define HALO_PMC_H

#include "postgres.h"

#define HALO_PMC_MAX 8

typedef struct HaloPmcEvent
{
	char name[40];
	int fd;
	void *page;
	uint64 before;
	uint64 total;
	uint64 samples;
	uint64 missed;
	bool before_valid;
} HaloPmcEvent;

typedef struct HaloPmc
{
	int count;
	HaloPmcEvent events[HALO_PMC_MAX];
} HaloPmc;

extern char *halo_pmc_list;
extern bool halo_pmc_start(HaloPmc *pmc);
extern void halo_pmc_sample_begin(HaloPmc *pmc);
extern void halo_pmc_sample_end(HaloPmc *pmc);
extern void halo_pmc_finish(HaloPmc *pmc, bool report);

#endif
