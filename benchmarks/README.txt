A set of tools and configurations for benchmarking qdrouterd
performance.  (see setup.sh)

Notes:
  * The dispatch build environment enables malloc() debugging by
    default.  This is done by defining the environment variables
    MALLOC_CHECK_ and MALLOC_PERTURB_.  Be sure these environment
    variables are NOT defined when running benchmarks:

    $ export -n MALLOC_PERTURB_; export -n MALLOC_CHECK_

  * Set "-DCMAKE_BUILD_TYPE=Release" in the CMake configuration

  * Ensure logging is disabled in the qdrouterd.conf file:
      enabled: none

  * If possible use numactl --physcpubind=xx to force all qdrouterd
    threads to share the same physical CPU core.  It is also
    recommended to isolate the benchmark clients on their own CPU to
    minimize their affect on the qdrouterd process(es).

  * (cpu-id, core-id)
    (0, 0), (1, 1), (2, 2), (3, 3)
    (4, 0), (5, 1)  (6, 2), (7, 3)

  * Set all cpu's governor to 'performance':
    $ sudo cpupower -c all frequency-set -g performance
