# Clearwater FV Test

This repository contains "Functional Verification" tests for Clearwater. These
are tests that do not fit well into per-project unit tests. This might be
because they use multiple components or external services / databases (for
example).

## Dependencies

The FV tests depend on a number of tools and libraries.  Some of these are
included as git submodules, but the rest must be installed separately.

On Ubuntu 14.04,

1.  update the package list

        sudo apt-get update

2.  install the required packages

        sudo apt-get install git cmake make gcc g++ bison flex libsctp-dev libgnutls-dev libgcrypt-dev libidn11-dev libtool autoconf libboost-dev libboost-test-dev automake libssl-dev libcloog-ppl1 libxml2-utils valgrind memcached snmp

## Getting the Code

To get all the code for the FV tests, clone this repository with the `--recursive` flag.

    git clone --recursive git@github.com:Metaswitch/clearwater-fv-test.git

This accesses the repository over SSH on Github, and will not work unless you have a Github account and registered SSH key. If you do not have both of these, you will need to use HTTPS instead:

    git clone --recursive https://github.com/Metaswitch/clearwater-fv-test.git

## Running the Tests

The easiest way to run the tests is to run `make test` from the root of the
repository. This will build all necessary dependencies and run the test suite.

Ralf unit tests use the [Google Test](https://code.google.com/p/googletest/)
framework, so the output from the test run looks something like this.

    Memcached port: 44444
    Memcached PID:  11736
    Running with random seed: 1425588638
    [==========] Running 18 tests from 5 test cases.
    [----------] Global test environment set-up.
    [----------] 5 tests from SingleMemcachedStoreTest/0, where TypeParam = TombstoneConfig
    [ RUN      ] SingleMemcachedStoreTest/0.SetDeleteSequence
    [       OK ] SingleMemcachedStoreTest/0.SetDeleteSequence (4 ms)
    ...
    [----------] Global test environment tear-down
    [==========] 18 tests from 5 test cases ran. (59 ms total)
    [  PASSED  ] 18 tests.


`make test` also automatically runs memory leak checks (using [Valgrind](http://valgrind.org/)).
If memory is leaked during the tests, an error is displayed.

The test suite supports the following options, which are passed as enviroment
variables:

* `JUSTTEST=testname` just runs the specified test case.
* `NOISY=T` enables verbose logging during the tests; you can add a logging
  level (e.g. `NOISY=T:5` to control which logs you see.
* `MEMCACHED_PORT=number`: the tests start up a memcached instance to run
    against. This allows you to override the default value (44444) if needed.
* `RANDOM_SEED=number`: use a specific random seed. Useful when trying to
    reproduce a test failure.
* `MEMCACHED_FLAGS=<flags>`: additional options to pass to memcached. For
    example us `MEMCACHED_FLAGS="-vv" to turn on verbose logging.

### Advanced Usage

There are some additional ways of running the tests, which are specified by
different Makefile targets:

*   `make run_test` just runs the tests without doing memory leak checks.
*   `make debug` runs the tests under gdb.
*   `make vg_raw` just runs the memory leak checks.

To use any of these advanced options, you must first change to the `src/`
directory below the project root.

You may also run `make test` from this directory, to avoid rebuilding the
dependencies. This is quicker, but is only recommended if you are sure the
dependencies haven't changed.
