# PostgreSQL 16 PMC homework

This branch adds Linux performance counters to the private hash-build path
`MultiExecPrivateHash`. The `halo_pmc_list` session setting selects events from
`public.pmc_name`, using the schema and event definitions from the course.
The default is empty and leaves instrumentation disabled.

## Build

```sh
./configure --prefix=/path/to/isolated/pg-pmc --enable-cassert
make -j4
make install
```

Use a separate installation and database cluster when testing. Do not install
over a running PostgreSQL instance. The source is based on PostgreSQL 16.

## Use

Run `pmc_assignment/setup.sql` once in the test database. It creates the
course-compatible event table; do not run it in a database where the table
already exists.

```sql
SET max_parallel_workers_per_gather = 0;
SET halo_pmc_list = 'cycles,Instructions';
SELECT count(*) FROM table_a a JOIN table_b b ON a.id = b.id;
SET halo_pmc_list = '';
```

Select a query plan containing `Hash Join` and a private `Hash` node. The
server reports one INFO line per successfully opened event:

- `whole`: raw event count from enabling the event until the end of the hash
  build, including per-tuple sampling overhead.
- `enabled` and `running`: total event times in nanoseconds from the kernel.
- `loop`: sum of the differences read before and after the hash/insert work
  for each tuple; it excludes the call that fetches the next input tuple.
- `samples` and `missed`: successful and unavailable per-tuple reads.

Counters run continuously during the build. On x86, the fast path reads the
perf metadata page and executes `RDPMC` only when the kernel permits it. It
does not enable or disable counters inside the loop. The values are raw, not
multiplex-scaled: consult `enabled`, `running`, and `missed` before comparing
measurements. If fast reads are unavailable, loop samples are marked missed;
the final `read()` still reports the whole-build count. Unsupported events,
missing tables, and denied perf permissions produce warnings. They do not
prevent an otherwise valid query from completing.

The example covers private hash builds, not parallel shared hash builds. A
`pmc_name` table must be readable by the current database role. The event
definitions are machine dependent; raw CPU events in particular are not
portable. Do not assume zero counts mean the workload did no work: a VM may
not expose a usable virtual PMU even when event opening succeeds.

## Tests

```sh
sh pmc_assignment/test.sh /path/to/isolated/pg-pmc/bin
```

The script creates its own temporary cluster and Unix socket, stops the server
on exit, and prints the temporary log directory. It checks that the plan uses
Hash Join; the query result is unchanged; missing tables, unknown events,
invalid input, and available or denied perf events are handled. To require
the x86 fast path, run the same script in an environment where the PostgreSQL
process can open perf events and `RDPMC` is enabled:

```sh
PMC_REQUIRE_FAST=1 sh pmc_assignment/test.sh /path/to/isolated/pg-pmc/bin
```

The course development VM was tested both as the ordinary `postgres` user and
with `CAP_PERFMON` granted only to the test process. The former reports denied
events; the latter exercises 1000 fast reads with zero misses. On that VMware
VM user-space cycles and instructions were zero, as also observed using
`perf stat -e cycles:u,instructions:u`; this does not establish valid numerical
counter accuracy. Meaningful performance validation requires a host or VM
with a functioning PMU and appropriate permissions.

Reference: Linux `perf_event_open(2)` and the `perf_event_mmap_page` metadata
protocol in `linux/perf_event.h`. The course `halo_pmc.c`, `halo_pmc.h` and
`创建PMC表.txt` informed the design and table schema; this branch implements a
smaller self-contained version and does not use their original code verbatim.
