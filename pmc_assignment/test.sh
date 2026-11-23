#!/bin/sh
set -eu

if [ "$#" -ne 1 ]; then
    printf 'Usage: %s /path/to/installed/postgresql/bin\n' "$0" >&2
    exit 2
fi

bin=$1
project=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
testdir=$(mktemp -d "${TMPDIR:-/tmp}/pg-pmc-test.XXXXXXXX")
started=0

stop_server() {
    if [ "$started" -eq 1 ]; then
        "$bin/pg_ctl" -D "$testdir/data" -m immediate stop >/dev/null
    fi
}
trap stop_server EXIT HUP INT TERM

"$bin/initdb" -D "$testdir/data" -A trust --no-locale >"$testdir/init.log"
"$bin/pg_ctl" -D "$testdir/data" -l "$testdir/server.log" \
    -o "-k $testdir -p 35473 -c listen_addresses=''" start >/dev/null
started=1
psql="$bin/psql"

"$psql" -X -h "$testdir" -p 35473 -d postgres -v ON_ERROR_STOP=1 -f "$project/test.sql" >"$testdir/output.log" 2>&1

grep -q 'Hash Join' "$testdir/output.log"
grep -q 'BASELINE_OK' "$testdir/output.log"
grep -q 'MISSING_TABLE_OK' "$testdir/output.log"
grep -q 'halo PMC: public.pmc_name does not exist' "$testdir/output.log"
grep -q 'halo PMC: unknown counter does_not_exist' "$testdir/output.log"
grep -q 'UNKNOWN_COUNTER_OK' "$testdir/output.log"
grep -q 'VALID_COUNTER_OK' "$testdir/output.log"
grep -q 'INVALID_LIST_OK' "$testdir/output.log"
grep -q 'invalid halo_pmc_list' "$testdir/output.log"
grep -Eq 'halo PMC cycles: whole=|halo PMC: cannot open cycles:' "$testdir/output.log"
if [ "${PMC_REQUIRE_FAST:-0}" = 1 ]; then
    grep -Eq 'halo PMC cycles: .*samples=[1-9][0-9]* missed=0' "$testdir/output.log"
fi
printf 'SQL tests passed. Test logs: %s\n' "$testdir"
grep -E 'BASELINE_OK|MISSING_TABLE_OK|UNKNOWN_COUNTER_OK|VALID_COUNTER_OK|INVALID_LIST_OK|halo PMC' "$testdir/output.log"
