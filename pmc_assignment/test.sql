\set ON_ERROR_STOP on
SET client_min_messages = info;
SET max_parallel_workers_per_gather = 0;
SET enable_mergejoin = off;
SET enable_nestloop = off;
CREATE TABLE pmc_test_left AS SELECT generate_series(1, 1000) AS id;
CREATE TABLE pmc_test_right AS SELECT generate_series(1, 1000) AS id;
ANALYZE pmc_test_left;
ANALYZE pmc_test_right;
EXPLAIN (COSTS OFF) SELECT count(*) FROM pmc_test_left a JOIN pmc_test_right b USING (id);
DO $$ BEGIN
  IF (SELECT count(*) FROM pmc_test_left a JOIN pmc_test_right b USING (id)) <> 1000 THEN
    RAISE EXCEPTION 'incorrect baseline result';
  END IF;
END $$;
SELECT 'BASELINE_OK';
SET halo_pmc_list = 'cycles';
SELECT count(*) FROM pmc_test_left a JOIN pmc_test_right b USING (id);
SELECT 'MISSING_TABLE_OK';
SET halo_pmc_list = '';
CREATE TABLE public.pmc_name (pcm_name varchar(40), pmutype int, pe_config int, config1 int, config2 int);
INSERT INTO public.pmc_name VALUES ('cycles', 0, 0, 0, 1), ('Instructions', 0, 1, 0, 1);
SET halo_pmc_list = 'does_not_exist';
SELECT count(*) FROM pmc_test_left a JOIN pmc_test_right b USING (id);
SELECT 'UNKNOWN_COUNTER_OK';
SET halo_pmc_list = 'cycles,Instructions';
DO $$ BEGIN
  IF (SELECT count(*) FROM pmc_test_left a JOIN pmc_test_right b USING (id)) <> 1000 THEN
    RAISE EXCEPTION 'PMC changed query results';
  END IF;
END $$;
SELECT 'VALID_COUNTER_OK';
SET halo_pmc_list = '"';
SELECT count(*) FROM pmc_test_left a JOIN pmc_test_right b USING (id);
SELECT 'INVALID_LIST_OK';
SET halo_pmc_list = '';
