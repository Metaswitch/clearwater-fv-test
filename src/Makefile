# FV test Makefile

all: stage-build

ROOT := $(abspath $(shell pwd)/../)
MK_DIR := ${ROOT}/mk

# We use some of Astaire's libraries (e.g. libmemcached) rather than using
# another copy of them as a submodule.
ASTAIRE_LIBS := ${ROOT}/modules/astaire/usr/lib
ASTAIRE_INCLUDES := ${ROOT}/modules/astaire/usr/include

GTEST_DIR := $(ROOT)/modules/gmock/gtest
GMOCK_DIR := $(ROOT)/modules/gmock

TARGET_TEST := fvtest

TARGET_SOURCES_TEST := memcachedstore.cpp \
                       baseresolver.cpp \
                       astaire_resolver.cpp \
                       a_record_resolver.cpp \
                       accesslogger.cpp \
                       health_checker.cpp \
                       exception_handler.cpp \
                       httpstack.cpp \
                       http_request.cpp \
                       http_connection_pool.cpp \
                       httpclient.cpp \
                       chronosconnection.cpp \
                       utils.cpp \
                       memcached_connection_pool.cpp \
                       memcachedstoreview.cpp \
                       memcached_config.cpp \
                       signalhandler.cpp \
                       snmp_agent.cpp \
                       snmp_row.cpp \
                       snmp_ip_row.cpp \
                       event_statistic_accumulator.cpp \
                       snmp_event_accumulator_table.cpp \
                       snmp_event_accumulator_by_scope_table.cpp \
                       snmp_continuous_accumulator_table.cpp \
                       snmp_counter_table.cpp \
                       snmp_cx_counter_table.cpp \
                       snmp_success_fail_count_table.cpp \
                       snmp_ip_count_table.cpp \
                       snmp_single_count_by_node_type_table.cpp \
                       snmp_success_fail_count_by_request_type_table.cpp \
                       snmp_ip_time_based_counter_table.cpp \
                       snmp_time_and_string_based_event_table.cpp \
                       snmp_scalar.cpp \
                       snmp_agent.cpp \
                       log.cpp \
                       logger.cpp \
                       fakelogger.cpp \
                       dnscachedresolver.cpp \
                       static_dns_cache.cpp \
                       dnsparser.cpp \
                       aor.cpp \
                       associated_uris.cpp \
                       astaire_aor_store.cpp \
                       s4.cpp \
                       s4_handlers.cpp \
                       s4_chronoshandlers.cpp \
                       test_main.cpp \
                       test_dns.cpp \
                       test_snmp.cpp \
                       site.cpp \
                       processinstance.cpp \
                       test_interposer.cpp \
                       test_memcachedsolution.cpp \
                       test_s4solution.cpp

TARGET_EXTRA_OBJS_TEST := gmock-all.o \
                          gtest-all.o

TEST_XML = $(TEST_OUT_DIR)/test_detail_$(TARGET_TEST).xml
VG_XML = $(TEST_OUT_DIR)/vg_$(TARGET_TEST).memcheck
VG_OUT = $(TEST_OUT_DIR)/vg_$(TARGET_TEST).txt
VG_LIST = $(TEST_OUT_DIR)/vg_$(TARGET_TEST)_list

VG_SUPPRESS = $(TARGET_TEST).supp

EXTRA_CLEANS += $(TEST_XML) \
                $(COVERAGE_XML) \
                $(VG_XML) $(VG_OUT) \
                $(OBJ_DIR_TEST)/*.gcno \
                $(OBJ_DIR_TEST)/*.gcda \
                *.gcov

CPPFLAGS += -Wno-write-strings \
            -ggdb3 -std=c++0x
CPPFLAGS += -I${ROOT}/include \
            -I${ROOT}/usr/include \
            -I${ASTAIRE_INCLUDES} \
            -I${ROOT}/modules/cpp-common/include \
            -I${ROOT}/modules/clearwater-s4/include \
            -I${ROOT}/modules/clearwater-s4/test_utils \
            -I${ROOT}/modules/rapidjson/include

# Add cpp-common and S4 as a VPATH to pull in required production code
VPATH := ${ROOT}/modules/cpp-common/src ${ROOT}/modules/cpp-common/test_utils ${ROOT}/modules/clearwater-s4/src

# Test build:
#
# Turn on code coverage.
# Disable optimization, for speed and coverage accuracy.
# Allow testing of private and protected fields/methods.
# Add the Google Mock / Google Test includes.
CPPFLAGS_TEST  += -DUNIT_TEST \
                  -fprofile-arcs -ftest-coverage \
                  -O0 \
                  -fno-access-control \
                  -I$(GTEST_DIR)/include -I$(GMOCK_DIR)/include \
                  -I${ROOT}/modules/cpp-common/test_utils

LDFLAGS += -L${ASTAIRE_LIBS} -L${ROOT}/usr/lib
LDFLAGS += -lmemcached \
           -levhtp \
           -levent \
           -levent_pthreads \
           -lsas \
           -lpthread \
           -lc \
           -lstdc++ \
           -ldl \
           -lrt \
           -lcares \
           -lcurl \
           -lz \
           -lboost_regex \
           -lboost_filesystem \
           -lboost_system \
           $(shell net-snmp-config --netsnmp-agent-libs)


# Test build uses just-built libraries, which may not be installed
LDFLAGS_TEST += -Wl,-rpath=$(ASTAIRE_LIBS)

# Now the GMock / GTest boilerplate.
GTEST_HEADERS := $(GTEST_DIR)/include/gtest/*.h \
                 $(GTEST_DIR)/include/gtest/internal/*.h
GMOCK_HEADERS := $(GMOCK_DIR)/include/gmock/*.h \
                 $(GMOCK_DIR)/include/gmock/internal/*.h \
                 $(GTEST_HEADERS)

GTEST_SRCS_ := $(GTEST_DIR)/src/*.cc $(GTEST_DIR)/src/*.h $(GTEST_HEADERS)
GMOCK_SRCS_ := $(GMOCK_DIR)/src/*.cc $(GMOCK_HEADERS)
# End of boilerplate

COVERAGEFLAGS = $(OBJ_DIR_TEST) --object-directory=$(shell pwd) \
                --root=$(shell pwd) --exclude=^ut/ \
                --sort-percentage

VGFLAGS = --suppressions=$(VG_SUPPRESS) \
          --gen-suppressions=all \
          --leak-check=full \
          --track-origins=yes \
          --malloc-fill=cc \
          --free-fill=df

# Define JUSTTEST=<testname> to test just that test.  Easier than
# passing the --gtest_filter in EXTRA_TEST_ARGS.
ifdef JUSTTEST
  EXTRA_TEST_ARGS ?= --gtest_filter=*$(JUSTTEST)*
endif

include ${MK_DIR}/platform.mk

# Override some build targets. These don't need to be real targets, they just
# have to be non empty (otherwise platform.mk will try to work out the actual
# values, and try to delete some required directories when cleaning).
TARGET_BIN := dummy_target_bin
TARGET_OBJS := dummy_target_objs

# Override the build target to notify the user that there is no production code
# to build.
.PHONY: build
build:
	@echo 'No production code in this project'
	@echo 'Run `make test` to build and run the FV tests'


.PHONY: stage-build
stage-build: build

.PHONY: test
test: run_test vg vg-check

# Run the test.  You can set:
# -  JUSTTEST to specify a filter to pass to gtest.
# -  EXTRA_TEST_ARGS to pass extra arguments to the test.
#
# Ignore failure here; it will be detected by Jenkins.
.PHONY: run_test
run_test: build_test | $(TEST_OUT_DIR)
	rm -f $(TEST_XML)
	rm -f $(OBJ_DIR_TEST)/*.gcda
	LD_LIBRARY_PATH=${ASTAIRE_LIBS} $(TARGET_BIN_TEST) $(EXTRA_TEST_ARGS) --gtest_output=xml:$(TEST_XML)

.PHONY: debug
debug: build_test
	LD_LIBRARY_PATH=${ASTAIRE_LIBS} gdb --args $(TARGET_BIN_TEST) $(EXTRA_TEST_ARGS)

# Don't run VG against death tests; they don't play nicely.
# Be aware that running this will count towards coverage.
# Don't send output to console, or it might be confused with the full
# unit-test run earlier.
# Test failure should not lead to build failure - instead we observe
# test failure from Jenkins.
.PHONY: vg
vg: build_test | $(TEST_OUT_DIR)
	@echo "!!"
	@echo "!! WARNING. S4 tests are currently not run under valgrind."
	@echo "!!"
	LD_LIBRARY_PATH=${ASTAIRE_LIBS} valgrind --xml=yes --xml-file=$(VG_XML) $(VGFLAGS) \
	  $(TARGET_BIN_TEST) --gtest_filter='-*DeathTest*:*S4*' $(EXTRA_TEST_ARGS) > $(VG_OUT) 2>&1

# Check whether there were any errors from valgrind. Output to screen any errors found,
# and details of where to find the full logs.
# The output file will contain <error><kind>ERROR</kind></error>, or 'XPath set is empty'
# if there are no errors.
.PHONY: vg-check
vg-check: vg
	@xmllint --xpath '//error/kind' $(VG_XML) 2>&1 | \
		sed -e 's#<kind>##g' | \
		sed -e 's#</kind>#\n#g' | \
		sort > $(VG_LIST)
	@if grep -q -v "XPath set is empty" $(VG_LIST) ; then \
		echo "Error: some memory errors have been detected" ; \
		cat $(VG_LIST) ; \
		echo "See $(VG_XML) for further details." ; \
	fi

.PHONY: vg_raw
vg_raw: build_test | $(TEST_OUT_DIR)
	@echo "!!"
	@echo "!! WARNING. S4 tests are currently not run under valgrind."
	@echo "!!"
	LD_LIBRARY_PATH=${ASTAIRE_LIBS} valgrind --gen-suppressions=all $(VGFLAGS) \
	  $(TARGET_BIN_TEST) --gtest_filter='-*DeathTest*:*S4*' $(EXTRA_TEST_ARGS)

.PHONY: distclean
distclean: clean

# Build rules for GMock/GTest library.
$(OBJ_DIR_TEST)/gtest-all.o : $(GTEST_SRCS_)
	$(CXX) $(CPPFLAGS) -I$(GTEST_DIR) -I$(GTEST_DIR)/include -I$(GMOCK_DIR) -I$(GMOCK_DIR)/include \
            -c $(GTEST_DIR)/src/gtest-all.cc -o $@

$(OBJ_DIR_TEST)/gmock-all.o : $(GMOCK_SRCS_)
	$(CXX) $(CPPFLAGS) -I$(GTEST_DIR) -I$(GTEST_DIR)/include -I$(GMOCK_DIR) -I$(GMOCK_DIR)/include \
            -c $(GMOCK_DIR)/src/gmock-all.cc -o $@
