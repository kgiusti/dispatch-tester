#!/bin/bash
#
# Heavily customized version of Crolke's run-perf script
#
# Generate a call tree flamegraph for qdrouterd
#
# Arguments:
#   $1 (optional) the pid for the qdrouterd process to analyze. By
#   default run perf on all qdrouterd processes on the system.
#
# 1) setup the qdrouterd configuration - start all routers
# 2) prepare to start whatever traffic/test
# 3) run qdr-flamegraph.sh
# 4) execute the test scenario
# 5) press <ENTER> to finish collection
#
# outputs a flamegraph for each thread

## for older intel CPUs that do not support --call-graph=lbr you must use -fno-omit-frame-pointer
## Example:
## cmake .. -DCMAKE_INSTALL_PREFIX=/opt/kgiusti -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_C_FLAGS_RELWITHDEBINFO="-O2 -g -DNDEBUG -fno-omit-frame-pointer"
##

set -x

ROUTERPIDS=$1

if [ -z "$ROUTERPIDS" ]; then
    ROUTERPIDS=$(pidof qdrouterd)
    if [ -z "$ROUTERPIDS" ]; then
        echo "No running qdrouterd found"
        exit 1
    fi
fi

# Generate perf data files per thread
for QDRPID in $ROUTERPIDS; do
    ps -L --pid $QDRPID -o tid= |\
        while read tid
        do
            perf record -e cache-misses -F max --call-graph=lbr --per-thread -s --tid=$tid --output=qdr_cache_${QDRPID}_${tid}.pdata &
            echo "Started perf for thread ${tid} in process $QDRPID"
        done
done

read -p "Perf is collecting data. Press Enter to stop collecting."

killall --wait perf


# Generate call graph perf reports
for QDRPID in $ROUTERPIDS; do
    ALLWORKERS=$(mktemp)
    ps -L --pid $QDRPID -o tid= |\
        while read tid
        do
            # Generate per-thread call graph as text file
            TMPFILE=$(mktemp)
            perf report -i ./qdr_cache_${QDRPID}_${tid}.pdata --header -g --call-graph --stdio > $TMPFILE

            # Detect the core/worker threads. Concatenate all worker thread stacks
            if grep -q router_core_thread $TMPFILE; then
                TYPE=core
            else
                TYPE=worker
                cat $TMPFILE >> $ALLWORKERS
            fi

            # Generate per-thread stack-collapse
            perf script -i ./qdr_cache_${QDRPID}_${tid}.pdata | stackcollapse-perf.pl > $TMPFILE

            # Generate flamegraph
            title="qdrouterd $QDRPID thread $tid ($TYPE) cache-miss"
            flamegraph.pl --hash --colors purple --title "${title}" --height 48 --width 1600 $TMPFILE > ./qdr_cache_${TYPE}_${QDRPID}_${tid}.svg
            rm $TMPFILE
        done
    rm $ALLWORKERS
done

exit 0
