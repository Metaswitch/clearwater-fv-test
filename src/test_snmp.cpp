/**
 * Project Clearwater - IMS in the cloud.
 * Copyright (C) 2015  Metaswitch Networks Ltd
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version, along with the "Special Exception" for use of
 * the program along with SSL, set forth below. This program is distributed
 * in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR
 * A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details. You should have received a copy of the GNU General Public
 * License along with this program.  If not, see
 * <http://www.gnu.org/licenses/>.
 *
 * The author can be reached by email at clearwater@metaswitch.com or by
 * post at Metaswitch Networks Ltd, 100 Church St, Enfield EN2 6BQ, UK
 *
 * Special Exception
 * Metaswitch Networks Ltd  grants you permission to copy, modify,
 * propagate, and distribute a work formed by combining OpenSSL with The
 * Software, or a work derivative of such a combination, even if such
 * copying, modification, propagation, or distribution would otherwise
 * violate the terms of the GPL. You must comply with the GPL in all
 * respects for all of the code used other than OpenSSL.
 * "OpenSSL" means OpenSSL toolkit software distributed by the OpenSSL
 * Project and licensed under the OpenSSL Licenses, or a work based on such
 * software and licensed under the OpenSSL Licenses.
 * "OpenSSL Licenses" means the OpenSSL License and Original SSLeay License
 * under which the OpenSSL Project distributes the OpenSSL toolkit software,
 * as those licenses appear in the file LICENSE-OPENSSL.
 */

#include "gtest/gtest.h"
#include "gmock/gmock.h"
#include "snmp_accumulator_table.h"
#include "snmp_counter_table.h"
#include "snmp_success_fail_count_table.h"
#include "snmp_ip_count_table.h"
#include "snmp_scalar.h"
#include "test_interposer.hpp"
#include "snmp_single_count_by_node_type_table.h"

#ifdef READ
#error "netsnmp includes have polluted the namespace!"
#endif

#include "snmp_internal/snmp_includes.h"
using ::testing::AnyOf;
using ::testing::Contains;

static void* snmp_thread(void* data)
{
  while (1)
  {
    agent_check_and_process(1);
  }
  return NULL;
}


static pthread_t thr;
std::string test_oid = ".1.2.2";

class SNMPTest : public ::testing::Test
{
  SNMPTest() {};

  // Sets up an SNMP master agent on port 16161 for us to register tables with and query
  static void SetUpTestCase()
  {
    // Configure SNMPd to use the fvtest.conf in the local directory
    char cwd[256];
    getcwd(cwd, sizeof(cwd));
    netsnmp_ds_set_string(NETSNMP_DS_LIBRARY_ID,
                          NETSNMP_DS_LIB_CONFIGURATION_DIR,
                          cwd);

    // Log SNMPd output to a file
    snmp_enable_filelog("fvtest-snmpd.out", 0);

    init_agent("fvtest");
    init_snmp("fvtest");
    init_master_agent();

    // Run a thread to handle SNMP requests
    pthread_create(&thr, NULL, snmp_thread, NULL);
  }
  
  static void TearDownTestCase()
  {
    pthread_cancel(thr);
    pthread_join(thr, NULL);
    snmp_shutdown("fvtest");
  }
  
};

TEST_F(SNMPTest, ScalarValue)
{
  // Create a scalar
  SNMP::U32Scalar scalar("answer", test_oid);
  scalar.value = 42;

  // Shell out to snmpget to query that scalar
  FILE* fd = popen("snmpget -v2c -On -c clearwater 127.0.0.1:16161 .1.2.2", "r");
  char buf[1024];
  fgets(buf, sizeof(buf), fd);

  // Check that it has the right OID, value and type
  ASSERT_STREQ(".1.2.2 = Gauge32: 42\n", buf);
}


TEST_F(SNMPTest, TableOrdering)
{
  // Create a table
  SNMP::AccumulatorTable* tbl = SNMP::AccumulatorTable::create("latency", test_oid);

  // Shell out to snmpwalk to find all entries in that table
  FILE* fd = popen("snmpwalk -v2c -On -c clearwater 127.0.0.1:16161 .1.2.2", "r");
  char buf[1024];

  // Check that they come in the right order - column 2 of row 1, column 2 of row 2, column 2 of row
  // 3, column 3 of row 1, column 3 of row 2....
  fgets(buf, sizeof(buf), fd);
  ASSERT_STREQ(".1.2.2.1.2.1 = Gauge32: 0\n", buf);
  fgets(buf, sizeof(buf), fd);
  ASSERT_STREQ(".1.2.2.1.2.2 = Gauge32: 0\n", buf);
  fgets(buf, sizeof(buf), fd);
  ASSERT_STREQ(".1.2.2.1.2.3 = Gauge32: 0\n", buf);
  fgets(buf, sizeof(buf), fd);
  ASSERT_STREQ(".1.2.2.1.3.1 = Gauge32: 0\n", buf);
  fgets(buf, sizeof(buf), fd);
  ASSERT_STREQ(".1.2.2.1.3.2 = Gauge32: 0\n", buf);

  delete tbl;
}

TEST_F(SNMPTest, LatencyCalculations)
{
  cwtest_completely_control_time();
  
  char buf[1024];
  FILE* fd;
  
  // Create a table
  SNMP::AccumulatorTable* tbl = SNMP::AccumulatorTable::create("latency", test_oid);

  // Just put one sample in (which should have a variance of 0).
  tbl->accumulate(100);

  // Move on five seconds. The "previous five seconds" stat should now reflect that sample.
  cwtest_advance_time_ms(5000);

  // Average should be 100
  fd = popen("snmpwalk -v2c -On -c clearwater 127.0.0.1:16161 .1.2.2.1.2.1", "r");
  fgets(buf, sizeof(buf), fd);
  ASSERT_STREQ(".1.2.2.1.2.1 = Gauge32: 100\n", buf);
  // Variance should be 0
  fd = popen("snmpwalk -v2c -On -c clearwater 127.0.0.1:16161 .1.2.2.1.3.1", "r");
  fgets(buf, sizeof(buf), fd);
  ASSERT_STREQ(".1.2.2.1.3.1 = Gauge32: 0\n", buf);

  // Now input two samples in this latency period.
  tbl->accumulate(300);
  tbl->accumulate(500);

  // Move on five seconds. The "previous five seconds" stat should now reflect those two samples.
  cwtest_advance_time_ms(5000);

  // Average should be 400
  fd = popen("snmpwalk -v2c -On -c clearwater 127.0.0.1:16161 .1.2.2.1.2.1", "r");
  fgets(buf, sizeof(buf), fd);
  ASSERT_STREQ(".1.2.2.1.2.1 = Gauge32: 400\n", buf);
  // HWM should be 500
  fd = popen("snmpwalk -v2c -On -c clearwater 127.0.0.1:16161 .1.2.2.1.4.1", "r");
  fgets(buf, sizeof(buf), fd);
  ASSERT_STREQ(".1.2.2.1.4.1 = Gauge32: 500\n", buf);
  // LWM should be 300
  fd = popen("snmpwalk -v2c -On -c clearwater 127.0.0.1:16161 .1.2.2.1.5.1", "r");
  fgets(buf, sizeof(buf), fd);
  ASSERT_STREQ(".1.2.2.1.5.1 = Gauge32: 300\n", buf);
  // Count should be 2
  fd = popen("snmpwalk -v2c -On -c clearwater 127.0.0.1:16161 .1.2.2.1.6.1", "r");
  fgets(buf, sizeof(buf), fd);
  ASSERT_STREQ(".1.2.2.1.6.1 = Gauge32: 2\n", buf);

  cwtest_reset_time();
  delete tbl;
}

TEST_F(SNMPTest, CounterTimePeriods)
{
  cwtest_completely_control_time();
  // Create a table indexed by time period
  SNMP::CounterTable* tbl = SNMP::CounterTable::create("counter", test_oid);

  // Shell out to snmpwalk to find all entries in that table
  FILE* fd = popen("snmpwalk -v2c -On -c clearwater 127.0.0.1:16161 .1.2.2", "r");
  char buf[1024];

  // At first, all three rows (previous 5s, current 5m, previous 5m) should have a zero value
  fgets(buf, sizeof(buf), fd);
  ASSERT_STREQ(".1.2.2.1.2.1 = Gauge32: 0\n", buf);
  fgets(buf, sizeof(buf), fd);
  ASSERT_STREQ(".1.2.2.1.2.2 = Gauge32: 0\n", buf);
  fgets(buf, sizeof(buf), fd);
  ASSERT_STREQ(".1.2.2.1.2.3 = Gauge32: 0\n", buf);

  // Increment the counter. This should show up in the current-five-minute stats, but nowhere else.
  tbl->increment();

  fd = popen("snmpwalk -v2c -On -c clearwater 127.0.0.1:16161 .1.2.2", "r");
  fgets(buf, sizeof(buf), fd);
  ASSERT_STREQ(".1.2.2.1.2.1 = Gauge32: 0\n", buf);
  fgets(buf, sizeof(buf), fd);
  ASSERT_STREQ(".1.2.2.1.2.2 = Gauge32: 1\n", buf); // Current 5 minutes
  fgets(buf, sizeof(buf), fd);
  ASSERT_STREQ(".1.2.2.1.2.3 = Gauge32: 0\n", buf);

  // Move on five seconds. The "previous five seconds" stat should now also reflect the increment.
  cwtest_advance_time_ms(5000);

  fd = popen("snmpwalk -v2c -On -c clearwater 127.0.0.1:16161 .1.2.2", "r");
  fgets(buf, sizeof(buf), fd);
  ASSERT_STREQ(".1.2.2.1.2.1 = Gauge32: 1\n", buf);
  fgets(buf, sizeof(buf), fd);
  ASSERT_STREQ(".1.2.2.1.2.2 = Gauge32: 1\n", buf); // Current 5 minutes
  fgets(buf, sizeof(buf), fd);
  ASSERT_STREQ(".1.2.2.1.2.3 = Gauge32: 0\n", buf);

  // Move on five more seconds. The "previous five seconds" stat should no longer reflect the increment.
  cwtest_advance_time_ms(5000);

  fd = popen("snmpwalk -v2c -On -c clearwater 127.0.0.1:16161 .1.2.2", "r");
  fgets(buf, sizeof(buf), fd);
  ASSERT_STREQ(".1.2.2.1.2.1 = Gauge32: 0\n", buf);
  fgets(buf, sizeof(buf), fd);
  ASSERT_STREQ(".1.2.2.1.2.2 = Gauge32: 1\n", buf); // Current 5 minutes
  fgets(buf, sizeof(buf), fd);
  ASSERT_STREQ(".1.2.2.1.2.3 = Gauge32: 0\n", buf);

  // Move on five minutes. Only the "previous five minutes" stat should now reflect the increment.
  cwtest_advance_time_ms(300000);

  fd = popen("snmpwalk -v2c -On -c clearwater 127.0.0.1:16161 .1.2.2", "r");
  fgets(buf, sizeof(buf), fd);
  ASSERT_STREQ(".1.2.2.1.2.1 = Gauge32: 0\n", buf);
  fgets(buf, sizeof(buf), fd);
  ASSERT_STREQ(".1.2.2.1.2.2 = Gauge32: 0\n", buf); 
  fgets(buf, sizeof(buf), fd);
  ASSERT_STREQ(".1.2.2.1.2.3 = Gauge32: 1\n", buf);

  // Increment the counter again and move on ten seconds
  tbl->increment();
  cwtest_advance_time_ms(10000);

  // That increment shouldn't be in the "previous 5 seconds" stat (because it was made 10 seconds
  // ago).
  fd = popen("snmpwalk -v2c -On -c clearwater 127.0.0.1:16161 .1.2.2", "r");
  fgets(buf, sizeof(buf), fd);
  ASSERT_STREQ(".1.2.2.1.2.1 = Gauge32: 0\n", buf);

  cwtest_reset_time();
  delete tbl;
}

TEST_F(SNMPTest, IPCountTable)
{
  // Create a table
  SNMP::IPCountTable* tbl = SNMP::IPCountTable::create("ip-counter", test_oid);

  tbl->get("127.0.0.1")->increment();

  // Shell out to snmpwalk to find all entries in that table
  FILE* fd = popen("snmpwalk -v2c -On -c clearwater 127.0.0.1:16161 .1.2.2", "r");
  char buf[1024];

  fgets(buf, sizeof(buf), fd);
  ASSERT_STREQ(".1.2.2.1.3.1.4.127.0.0.1 = Gauge32: 1\n", buf);
  delete tbl;
}

TEST_F(SNMPTest, SuccessFailCountTable)
{
  // Create table
  SNMP::SuccessFailCountTable* tbl = SNMP::SuccessFailCountTable::create("success_fail_count", test_oid);
  
  // Shell out to snmpwalk to find all entries in that table
  FILE* fd;
  char buf[1024];
  
  tbl->increment_attempts();
  tbl->increment_successes();
  
  // Should be 1 attempt, 1 success, 0 failures.
  fd = popen("snmpwalk -v2c -On -c clearwater 127.0.0.1:16161 .1.2.2.1.2.2", "r");
  fgets(buf, sizeof(buf), fd);
  ASSERT_STREQ(".1.2.2.1.2.2 = Gauge32: 1\n", buf);
  fd = popen("snmpwalk -v2c -On -c clearwater 127.0.0.1:16161 .1.2.2.1.3.2", "r");
  fgets(buf, sizeof(buf), fd);
  ASSERT_STREQ(".1.2.2.1.3.2 = Gauge32: 1\n", buf);
  fd = popen("snmpwalk -v2c -On -c clearwater 127.0.0.1:16161 .1.2.2.1.4.2", "r");
  fgets(buf, sizeof(buf), fd);
  ASSERT_STREQ(".1.2.2.1.4.2 = Gauge32: 0\n", buf);

  tbl->increment_attempts();
  tbl->increment_failures();
  
  // Should be 2 attempts, 1 success, 1 failure.
  fd = popen("snmpwalk -v2c -On -c clearwater 127.0.0.1:16161 .1.2.2.1.2.2", "r");
  fgets(buf, sizeof(buf), fd);
  ASSERT_STREQ(".1.2.2.1.2.2 = Gauge32: 2\n", buf);
  fd = popen("snmpwalk -v2c -On -c clearwater 127.0.0.1:16161 .1.2.2.1.3.2", "r");
  fgets(buf, sizeof(buf), fd);
  ASSERT_STREQ(".1.2.2.1.3.2 = Gauge32: 1\n", buf);
  fd = popen("snmpwalk -v2c -On -c clearwater 127.0.0.1:16161 .1.2.2.1.4.2", "r");
  fgets(buf, sizeof(buf), fd);
  ASSERT_STREQ(".1.2.2.1.4.2 = Gauge32: 1\n", buf);

  delete tbl;
}

TEST_F(SNMPTest, SingleCountByNodeTypeTable)
{
  cwtest_completely_control_time();

  // Create a table
  SNMP::SingleCountByNodeTypeTable* tbl = SNMP::SingleCountByNodeTypeTable::create("single-count", test_oid);

  // Shell out to snmpwalk to find all entries in that table
  FILE* fd = popen("snmpwalk -v2c -On -c clearwater 127.0.0.1:16161 .1.2.2", "r");
  char buf[1024];

  // To start with, all values should be 0.
  fgets(buf, sizeof(buf), fd);
  ASSERT_STREQ(".1.2.2.1.3.1.0 = Gauge32: 0\n", buf);
  fgets(buf, sizeof(buf), fd);
  ASSERT_STREQ(".1.2.2.1.3.1.2 = Gauge32: 0\n", buf);
  fgets(buf, sizeof(buf), fd);
  ASSERT_STREQ(".1.2.2.1.3.1.5 = Gauge32: 0\n", buf);
  fgets(buf, sizeof(buf), fd);
  ASSERT_STREQ(".1.2.2.1.3.2.0 = Gauge32: 0\n", buf);
  fgets(buf, sizeof(buf), fd);
  ASSERT_STREQ(".1.2.2.1.3.2.2 = Gauge32: 0\n", buf);
  fgets(buf, sizeof(buf), fd);
  ASSERT_STREQ(".1.2.2.1.3.2.5 = Gauge32: 0\n", buf);
  fgets(buf, sizeof(buf), fd);
  ASSERT_STREQ(".1.2.2.1.3.3.0 = Gauge32: 0\n", buf);
  fgets(buf, sizeof(buf), fd);
  ASSERT_STREQ(".1.2.2.1.3.3.2 = Gauge32: 0\n", buf);
  fgets(buf, sizeof(buf), fd);
  ASSERT_STREQ(".1.2.2.1.3.3.5 = Gauge32: 0\n", buf);

  // Add an entry for SCSCF/ICSCF/BGCF. Only the current five minutes should have a count.
  tbl->increment(SNMP::NodeTypes::SCSCF);
  tbl->increment(SNMP::NodeTypes::ICSCF);
  tbl->increment(SNMP::NodeTypes::BGCF);

  fd = popen("snmpwalk -v2c -On -c clearwater 127.0.0.1:16161 .1.2.2", "r");

  fgets(buf, sizeof(buf), fd);
  ASSERT_STREQ(".1.2.2.1.3.1.0 = Gauge32: 0\n", buf);
  fgets(buf, sizeof(buf), fd);
  ASSERT_STREQ(".1.2.2.1.3.1.2 = Gauge32: 0\n", buf);
  fgets(buf, sizeof(buf), fd);
  ASSERT_STREQ(".1.2.2.1.3.1.5 = Gauge32: 0\n", buf);
  fgets(buf, sizeof(buf), fd);
  ASSERT_STREQ(".1.2.2.1.3.2.0 = Gauge32: 1\n", buf);
  fgets(buf, sizeof(buf), fd);
  ASSERT_STREQ(".1.2.2.1.3.2.2 = Gauge32: 1\n", buf);
  fgets(buf, sizeof(buf), fd);
  ASSERT_STREQ(".1.2.2.1.3.2.5 = Gauge32: 1\n", buf);
  fgets(buf, sizeof(buf), fd);
  ASSERT_STREQ(".1.2.2.1.3.3.0 = Gauge32: 0\n", buf);
  fgets(buf, sizeof(buf), fd);
  ASSERT_STREQ(".1.2.2.1.3.3.2 = Gauge32: 0\n", buf);
  fgets(buf, sizeof(buf), fd);
  ASSERT_STREQ(".1.2.2.1.3.3.5 = Gauge32: 0\n", buf);

  // Move on five seconds. The "previous five seconds" stat should now also reflect the increment.
  cwtest_advance_time_ms(5000);
  fd = popen("snmpwalk -v2c -On -c clearwater 127.0.0.1:16161 .1.2.2", "r");

  fgets(buf, sizeof(buf), fd);
  ASSERT_STREQ(".1.2.2.1.3.1.0 = Gauge32: 1\n", buf);
  fgets(buf, sizeof(buf), fd);
  ASSERT_STREQ(".1.2.2.1.3.1.2 = Gauge32: 1\n", buf);
  fgets(buf, sizeof(buf), fd);
  ASSERT_STREQ(".1.2.2.1.3.1.5 = Gauge32: 1\n", buf);
  fgets(buf, sizeof(buf), fd);
  ASSERT_STREQ(".1.2.2.1.3.2.0 = Gauge32: 1\n", buf);
  fgets(buf, sizeof(buf), fd);
  ASSERT_STREQ(".1.2.2.1.3.2.2 = Gauge32: 1\n", buf);
  fgets(buf, sizeof(buf), fd);
  ASSERT_STREQ(".1.2.2.1.3.2.5 = Gauge32: 1\n", buf);
  fgets(buf, sizeof(buf), fd);
  ASSERT_STREQ(".1.2.2.1.3.3.0 = Gauge32: 0\n", buf);
  fgets(buf, sizeof(buf), fd);
  ASSERT_STREQ(".1.2.2.1.3.3.2 = Gauge32: 0\n", buf);
  fgets(buf, sizeof(buf), fd);
  ASSERT_STREQ(".1.2.2.1.3.3.5 = Gauge32: 0\n", buf);

  cwtest_reset_time();
  delete tbl;
}
