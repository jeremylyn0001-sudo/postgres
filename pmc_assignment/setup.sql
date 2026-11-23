CREATE TABLE public.pmc_name (
    pcm_name varchar(40),
    pmutype integer,
    pe_config integer,
    config1 integer,
    config2 integer
);

INSERT INTO public.pmc_name VALUES
    ('cycles', 0, 0, 0, 1),
    ('Instructions', 0, 1, 0, 1),
    ('PERF_COUNT_HW_CACHE_L1D_RD', 3, 0, 0, 1),
    ('PERF_COUNT_HW_CACHE_L1D_MISS_RD', 3, 65536, 0, 1);
