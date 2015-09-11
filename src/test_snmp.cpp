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
#include "snmp_event_accumulator_table.h"
#include "snmp_continuous_accumulator_table.h"
#include "snmp_counter_table.h"
#include "snmp_success_fail_count_table.h"
#include "snmp_ip_count_table.h"
#include "snmp_scalar.h"
#include "test_interposer.hpp"
#include "snmp_single_count_by_node_type_table.h"
#include "snmp_success_fail_count_by_request_type_table.h"
#include "snmp_cx_counter_table.h"

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

static int snmp_get(std::string oid)
{
  // Returns integer value found at that OID.
  std::string command = "snmpget -v2c -Ovq -c clearwater 127.0.0.1:16161 " + oid;
  std::string mode = "r";
  FILE* fd = popen(command.c_str(), mode.c_str());
  char buf[1024];
  fgets(buf, sizeof(buf), fd);
  return atoi(buf);
}

static std::vector<std::string> snmp_walk(std::string oid)
{
  // Returns the results of an snmpwalk performed at that oid as a list of
  // strings.
  std::vector<std::string> res;
  std::string entry;

  std::string command = "snmpwalk -v2c -OQn -c clearwater 127.0.0.1:16161 " + oid;
  std::string mode = "r";
  FILE* fd = popen(command.c_str(), mode.c_str());
  char buf[1024];
  fgets(buf, sizeof(buf), fd);
  entry = buf;
  std::size_t end = entry.find("No more variables left in this MIB View");
  while (end == std::string::npos)
  {
    res.push_back(buf);
    fgets(buf, sizeof(buf), fd);
    entry = buf;
    end = entry.find("No more variables left in this MIB View");
  }

  return res;
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
  
  // Check that it has the right OID, value and type
  ASSERT_EQ(42, snmp_get(".1.2.2"));
}

TEST_F(SNMPTest, TableOrdering)
{
  // Create a table
  SNMP::EventAccumulatorTable* tbl = SNMP::EventAccumulatorTable::create("latency", test_oid);

  // Shell out to snmpwalk to find all entries in that table
  std::vector<std::string> entries = snmp_walk(".1.2.2");

  // Check that the table has the right number of entries (3 time periods * five
  // entries)
  ASSERT_EQ(15, entries.size());
  
  // Check that they come in the right order - column 2 of row 1, column 2 of row 2, column 2 of row
  // 3, column 3 of row 1, column 3 of row 2....
  ASSERT_STREQ(".1.2.2.1.2.1 = 0\n", entries[0].c_str());
  ASSERT_STREQ(".1.2.2.1.2.2 = 0\n", entries[1].c_str());
  ASSERT_STREQ(".1.2.2.1.2.3 = 0\n", entries[2].c_str());
  ASSERT_STREQ(".1.2.2.1.3.1 = 0\n", entries[3].c_str());
  ASSERT_STREQ(".1.2.2.1.3.2 = 0\n", entries[4].c_str());

  delete tbl;
}

TEST_F(SNMPTest, LatencyCalculations)
{
  cwtest_completely_control_time();

  // Create a table
  SNMP::EventAccumulatorTable* tbl = SNMP::EventAccumulatorTable::create("latency", test_oid);

  // Just put one sample in (which should have a variance of 0).
  tbl->accumulate(100);

  // Move on five seconds. The "previous five seconds" stat should now reflect that sample.
  cwtest_advance_time_ms(5000);

  // Average should be 100
  ASSERT_EQ(100, snmp_get(".1.2.2.1.2.1"));
  // Variance should be 0
  ASSERT_EQ(0, snmp_get(".1.2.2.1.3.1"));

  // Now input two samples in this latency period.
  tbl->accumulate(300);
  tbl->accumulate(500);

  // Move on five seconds. The "previous five seconds" stat should now reflect those two samples.
  cwtest_advance_time_ms(5000);

  // Average should be 400
  ASSERT_EQ(400, snmp_get(".1.2.2.1.2.1"));
  // HWM should be 500
  ASSERT_EQ(500, snmp_get(".1.2.2.1.4.1"));
  // LWM should be 300
  ASSERT_EQ(300, snmp_get(".1.2.2.1.5.1"));
  // Count should be 2
  ASSERT_EQ(2, snmp_get(".1.2.2.1.6.1"));

  cwtest_reset_time();
  delete tbl;
}

TEST_F(SNMPTest, CounterTimePeriods)
{
  cwtest_completely_control_time();
  // Create a table indexed by time period
  SNMP::CounterTable* tbl = SNMP::CounterTable::create("counter", test_oid);

  // At first, all three rows (previous 5s, current 5m, previous 5m) should have a zero value
  ASSERT_EQ(0, snmp_get(".1.2.2.1.2.1"));
  ASSERT_EQ(0, snmp_get(".1.2.2.1.2.2"));
  ASSERT_EQ(0, snmp_get(".1.2.2.1.2.3"));

  // Increment the counter. This should show up in the current-five-minute stats, but nowhere else.
  tbl->increment();

  ASSERT_EQ(0, snmp_get(".1.2.2.1.2.1"));
  ASSERT_EQ(1, snmp_get(".1.2.2.1.2.2")); // Current 5 minutes
  ASSERT_EQ(0, snmp_get(".1.2.2.1.2.3"));

  // Move on five seconds. The "previous five seconds" stat should now also reflect the increment.
  cwtest_advance_time_ms(5000);

  ASSERT_EQ(1, snmp_get(".1.2.2.1.2.1"));
  ASSERT_EQ(1, snmp_get(".1.2.2.1.2.2")); // Current 5 minutes
  ASSERT_EQ(0, snmp_get(".1.2.2.1.2.3"));

  // Move on five more seconds. The "previous five seconds" stat should no longer reflect the increment.
  cwtest_advance_time_ms(5000);

  ASSERT_EQ(0, snmp_get(".1.2.2.1.2.1"));
  ASSERT_EQ(1, snmp_get(".1.2.2.1.2.2")); // Current 5 minutes
  ASSERT_EQ(0, snmp_get(".1.2.2.1.2.3"));

  // Move on five minutes. Only the "previous five minutes" stat should now reflect the increment.
  cwtest_advance_time_ms(300000);

  ASSERT_EQ(0, snmp_get(".1.2.2.1.2.1"));
  ASSERT_EQ(0, snmp_get(".1.2.2.1.2.2"));
  ASSERT_EQ(1, snmp_get(".1.2.2.1.2.3"));

  // Increment the counter again and move on ten seconds
  tbl->increment();
  cwtest_advance_time_ms(10000);

  // That increment shouldn't be in the "previous 5 seconds" stat (because it was made 10 seconds
  // ago).
  ASSERT_EQ(0, snmp_get(".1.2.2.1.2.1"));

  cwtest_reset_time();
  delete tbl;
}

TEST_F(SNMPTest, IPCountTable)
{
  // Create a table
  SNMP::IPCountTable* tbl = SNMP::IPCountTable::create("ip-counter", test_oid);

  tbl->get("127.0.0.1")->increment();
  
  // Shell out to snmpwalk to find all entries in that table
  std::vector<std::string> entries = snmp_walk(".1.2.2");

  ASSERT_EQ(1, entries.size());
  ASSERT_STREQ(".1.2.2.1.3.1.4.127.0.0.1 = 1\n", entries[0].c_str());
  delete tbl;
}

TEST_F(SNMPTest, SuccessFailCountTable)
{
  cwtest_completely_control_time();
  
  // Create table
  SNMP::SuccessFailCountTable* tbl = SNMP::SuccessFailCountTable::create("success_fail_count", test_oid);

  tbl->increment_attempts();
  tbl->increment_successes();
  tbl->increment_attempts();
  tbl->increment_failures();
  // Should be 2 attempts, 1 success, 1 failures.
  ASSERT_EQ(2, snmp_get(".1.2.2.1.2.2"));
  ASSERT_EQ(1, snmp_get(".1.2.2.1.3.2"));
  ASSERT_EQ(1, snmp_get(".1.2.2.1.4.2"));

  // Move on five seconds. The "previous five seconds" stat should now also reflect the increment.
  cwtest_advance_time_ms(5000);

  ASSERT_EQ(2, snmp_get(".1.2.2.1.2.1"));
  ASSERT_EQ(1, snmp_get(".1.2.2.1.3.1"));
  ASSERT_EQ(1, snmp_get(".1.2.2.1.4.1"));

  cwtest_reset_time();
  delete tbl;
}

TEST_F(SNMPTest, SingleCountByNodeTypeTable)
{
  cwtest_completely_control_time();

  // Create a table
  SNMP::SingleCountByNodeTypeTable* tbl = SNMP::SingleCountByNodeTypeTable::create("single-count", test_oid, {SNMP::NodeTypes::SCSCF, SNMP::NodeTypes::ICSCF});

  // To start with, all values should be 0.
  ASSERT_EQ(0, snmp_get(".1.2.2.1.3.1.0"));
  ASSERT_EQ(0, snmp_get(".1.2.2.1.3.1.2"));
  ASSERT_EQ(0, snmp_get(".1.2.2.1.3.2.0"));
  ASSERT_EQ(0, snmp_get(".1.2.2.1.3.2.2"));
  ASSERT_EQ(0, snmp_get(".1.2.2.1.3.3.0"));
  ASSERT_EQ(0, snmp_get(".1.2.2.1.3.3.2"));

  // Add an entry for each supported node type. Only the current five minutes
  // should have a count.
  tbl->increment(SNMP::NodeTypes::SCSCF);
  tbl->increment(SNMP::NodeTypes::ICSCF);

  ASSERT_EQ(0, snmp_get(".1.2.2.1.3.1.0"));
  ASSERT_EQ(0, snmp_get(".1.2.2.1.3.1.2"));
  ASSERT_EQ(1, snmp_get(".1.2.2.1.3.2.0"));
  ASSERT_EQ(1, snmp_get(".1.2.2.1.3.2.2"));
  ASSERT_EQ(0, snmp_get(".1.2.2.1.3.3.0"));
  ASSERT_EQ(0, snmp_get(".1.2.2.1.3.3.2"));

  // Move on five seconds. The "previous five seconds" stat should now also reflect the increment.
  cwtest_advance_time_ms(5000);

  ASSERT_EQ(1, snmp_get(".1.2.2.1.3.1.0"));
  ASSERT_EQ(1, snmp_get(".1.2.2.1.3.1.2"));
  ASSERT_EQ(1, snmp_get(".1.2.2.1.3.2.0"));
  ASSERT_EQ(1, snmp_get(".1.2.2.1.3.2.2"));
  ASSERT_EQ(0, snmp_get(".1.2.2.1.3.3.0"));
  ASSERT_EQ(0, snmp_get(".1.2.2.1.3.3.2"));

  cwtest_reset_time();
  delete tbl;
}

TEST_F(SNMPTest, SuccessFailCountByRequestTypeTable)
{
  cwtest_completely_control_time();

  // Create a table
  SNMP::SuccessFailCountByRequestTypeTable* tbl = SNMP::SuccessFailCountByRequestTypeTable::create("success_fail_by_request", test_oid);

  // To start with, all values should be 0 (check the INVITE and ACK entries).
  // Check previous 5 second attempts.
  ASSERT_EQ(0, snmp_get(".1.2.2.1.3.1.0"));
  ASSERT_EQ(0, snmp_get(".1.2.2.1.3.1.1"));

  // Check previous 5 second period successes.
  ASSERT_EQ(0, snmp_get(".1.2.2.1.4.1.0"));
  ASSERT_EQ(0, snmp_get(".1.2.2.1.4.1.1"));

  // Check previous 5 second period failures.
  ASSERT_EQ(0, snmp_get(".1.2.2.1.5.1.0"));
  ASSERT_EQ(0, snmp_get(".1.2.2.1.5.1.1"));

  // Check current 5 minute period attempts.
  ASSERT_EQ(0, snmp_get(".1.2.2.1.3.2.0"));
  ASSERT_EQ(0, snmp_get(".1.2.2.1.3.2.1"));

  // Check current 5 minute period successes.
  ASSERT_EQ(0, snmp_get(".1.2.2.1.4.2.0"));
  ASSERT_EQ(0, snmp_get(".1.2.2.1.4.2.1"));

  // Check current 5 minute period failures.
  ASSERT_EQ(0, snmp_get(".1.2.2.1.5.2.0"));
  ASSERT_EQ(0, snmp_get(".1.2.2.1.5.2.1"));

  // Check previous 5 minute period attempts.
  ASSERT_EQ(0, snmp_get(".1.2.2.1.3.3.0"));
  ASSERT_EQ(0, snmp_get(".1.2.2.1.3.3.1"));

  // Check previous 5 minute period successes.
  ASSERT_EQ(0, snmp_get(".1.2.2.1.4.3.0"));
  ASSERT_EQ(0, snmp_get(".1.2.2.1.4.3.1"));

  // Check previous 5 minute period failures.
  ASSERT_EQ(0, snmp_get(".1.2.2.1.5.3.0"));
  ASSERT_EQ(0, snmp_get(".1.2.2.1.5.3.1"));

  // Increment an attempt and success for INVITE, and an attempt and failure for ACK. Only the current five minutes should have a count.
  tbl->increment_attempts(SNMP::SIPRequestTypes::INVITE);
  tbl->increment_successes(SNMP::SIPRequestTypes::INVITE);
  tbl->increment_attempts(SNMP::SIPRequestTypes::ACK);
  tbl->increment_failures(SNMP::SIPRequestTypes::ACK);

  // Check previous 5 second period attempts.
  ASSERT_EQ(0, snmp_get(".1.2.2.1.3.1.0"));
  ASSERT_EQ(0, snmp_get(".1.2.2.1.3.1.1"));

  // Check previous 5 second period successes.
  ASSERT_EQ(0, snmp_get(".1.2.2.1.4.1.0"));
  ASSERT_EQ(0, snmp_get(".1.2.2.1.4.1.1"));

  // Check previous 5 second period failures.
  ASSERT_EQ(0, snmp_get(".1.2.2.1.5.1.0"));
  ASSERT_EQ(0, snmp_get(".1.2.2.1.5.1.1"));

  // Check current 5 minute period attempts.
  ASSERT_EQ(1, snmp_get(".1.2.2.1.3.2.0"));
  ASSERT_EQ(1, snmp_get(".1.2.2.1.3.2.1"));

  // Check current 5 minute period successes.
  ASSERT_EQ(1, snmp_get(".1.2.2.1.4.2.0"));
  ASSERT_EQ(0, snmp_get(".1.2.2.1.4.2.1"));

  // Check current 5 minute period failures.
  ASSERT_EQ(0, snmp_get(".1.2.2.1.5.2.0"));
  ASSERT_EQ(1, snmp_get(".1.2.2.1.5.2.1"));

  // Move on five seconds. The "previous five seconds" stat should now also reflect the increment.
  cwtest_advance_time_ms(5000);

  // Check previous 5 second period attempts.
  ASSERT_EQ(1, snmp_get(".1.2.2.1.3.1.0"));
  ASSERT_EQ(1, snmp_get(".1.2.2.1.3.1.1"));

  // Check previous 5 second period successes.
  ASSERT_EQ(1, snmp_get(".1.2.2.1.4.1.0"));
  ASSERT_EQ(0, snmp_get(".1.2.2.1.4.1.1"));

  // Check previous 5 second period failures.
  ASSERT_EQ(0, snmp_get(".1.2.2.1.5.1.0"));
  ASSERT_EQ(1, snmp_get(".1.2.2.1.5.1.1"));

  cwtest_reset_time();
  delete tbl;
}

// Advance to the next start of interval - accurate to within the first
// second. i.e. May jump to 12:00:00:634, but never before 12:00:00:000
void jump_to_next_periodstart(uint32_t interval_ms)
{
  struct timespec now;
  clock_gettime(CLOCK_REALTIME_COARSE, &now);

  // Calculate the current time in ms
  uint64_t ms_since_epoch = (now.tv_sec * 1000) + (now.tv_nsec / 1000000);

  // Move time forward
  cwtest_advance_time_ms(interval_ms - (ms_since_epoch % interval_ms));
}

TEST_F(SNMPTest, ContinuousAccumulatorTable)
{
  cwtest_completely_control_time();

  // Consider a 5 minute period
  jump_to_next_periodstart(300000);

  // Advance to 59s into the period
  cwtest_advance_time_ms(59000);

  // Create table at this point
  SNMP::ContinuousAccumulatorTable* tbl = SNMP::ContinuousAccumulatorTable::create("continuous", test_oid);

  // Add one value to the table and advance 120 seconds
  tbl->accumulate(100);
  cwtest_advance_time_ms(120000);

  // Average should not take into account the time before the table was created
  // (therefore average should still be 100)
  ASSERT_EQ(100, snmp_get(".1.2.2.1.2.2")); // Current 5 minutes

  // Add another value to the table and advance 120 seconds
  tbl->accumulate(200);
  cwtest_advance_time_ms(120000);

  // This period has spent half the time at 200, and half at 100
  // So average value should be 150
  ASSERT_EQ(150, snmp_get(".1.2.2.1.2.2")); // Current 5 minutes

  // Jump forward to the next period, and move halfway through it
  // As there is only a second left of this period, the average should not
  // change
  jump_to_next_periodstart(300000);

  cwtest_advance_time_ms(150000);

  // The average value should be 200 for the current 5 minutes as it is carried
  // over, additionally, should be value for HWM and LWM, and variance 0
  ASSERT_EQ(200, snmp_get(".1.2.2.1.2.2")); // Current 5 minutes - Avg

  ASSERT_EQ(0, snmp_get(".1.2.2.1.3.2")); // Current 5 minutes - Variance

  ASSERT_EQ(200, snmp_get(".1.2.2.1.4.2")); // Current 5 minutes - HWM

  ASSERT_EQ(200, snmp_get(".1.2.2.1.5.2")); // Current 5 minutes - LWM

  // Add a HWM and LWM 5 seconds apart
  tbl->accumulate(150);
  cwtest_advance_time_ms(5000);

  tbl->accumulate(250);
  cwtest_advance_time_ms(5000);

  // The average value should remain at 200, but the LWM and HWM are adjusted
  ASSERT_EQ(200, snmp_get(".1.2.2.1.2.2")); // Current 5 minutes - Avg

  ASSERT_EQ(250, snmp_get(".1.2.2.1.4.2")); // Current 5 minutes - HWM

  ASSERT_EQ(150, snmp_get(".1.2.2.1.5.2")); // Current 5 minutes - LWM

  // The variance is calculated as sqsum = 200 * 200 * 150000 +
  //                                       150 * 150 * 5000 +
  //                                       250 * 250 * 5000 = 6425000000
  //                               sum = 200 * 150000 + 150 * 5000 + 250 * 5000
  //                                   = 32000000
  //                               var = 642500000 / 160000 - (32000000 /
  //                               160000)^2
  //                               = 40156 - 40000
  //                               = 156
  ASSERT_EQ(156, snmp_get(".1.2.2.1.3.2")); // Current 5 minutes - Variance

  // The previous 5 minutes should not have changed value
  ASSERT_EQ(150, snmp_get(".1.2.2.1.2.3")); // Previous 5 minutes - Avg

  cwtest_reset_time();
  delete tbl;
}

TEST_F(SNMPTest, CxCounterTable)
{
  cwtest_completely_control_time();
  
  // Create table
  SNMP::CxCounterTable* tbl = SNMP::CxCounterTable::create("cx_counter", test_oid);
  
  // Check that the rows that are there are the ones we expect and that initial
  // values are zero.
  std::vector<std::string> entries = snmp_walk(".1.2.2");

  // Check that there are the right number of entries in the table (3 time
  // periods * (33 base result-codes plus 14 3GPP result-codes plus 1 timeout)
  ASSERT_EQ(144, entries.size());

  // Check the first base protocol rows.
  ASSERT_STREQ(".1.2.2.1.4.1.0.1001 = 0\n", entries[0].c_str());
  ASSERT_STREQ(".1.2.2.1.4.1.0.2001 = 0\n", entries[1].c_str());
  ASSERT_STREQ(".1.2.2.1.4.1.0.2002 = 0\n", entries[2].c_str());

  // Check the first 3GPP rows.
  ASSERT_STREQ(".1.2.2.1.4.1.1.2001 = 0\n", entries[33].c_str());
  ASSERT_STREQ(".1.2.2.1.4.1.1.2002 = 0\n", entries[34].c_str());
  ASSERT_STREQ(".1.2.2.1.4.1.1.2003 = 0\n", entries[35].c_str());
  
  tbl->increment(SNMP::DiameterAppId::BASE, 2001);
  tbl->increment(SNMP::DiameterAppId::_3GPP, 5011);

  // Only the current five minute values should reflect the increment.
  ASSERT_EQ(0, snmp_get(".1.2.2.1.4.1.0.2001"));
  ASSERT_EQ(1, snmp_get(".1.2.2.1.4.2.0.2001"));
  ASSERT_EQ(0, snmp_get(".1.2.2.1.4.3.0.2001"));
  ASSERT_EQ(0, snmp_get(".1.2.2.1.4.1.1.5011"));
  ASSERT_EQ(1, snmp_get(".1.2.2.1.4.2.1.5011"));
  ASSERT_EQ(0, snmp_get(".1.2.2.1.4.3.1.5011"));

  // Move on five seconds. The "previous five seconds" stat should now also
  // reflect the increment.
  cwtest_advance_time_ms(5000);
  ASSERT_EQ(1, snmp_get(".1.2.2.1.4.1.0.2001"));
  ASSERT_EQ(1, snmp_get(".1.2.2.1.4.1.1.5011"));

  cwtest_reset_time();
  delete tbl;
}
