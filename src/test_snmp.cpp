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

#ifdef READ
#error "netsnmp includes have polluted the namespace!"
#endif

#include "test_snmp.h"

TEST_F(SNMPTest, ScalarValue)
{
  // Create a scalar
  SNMP::U32Scalar scalar("answer", test_oid);
  scalar.value = 42;

  // Check that it has the right OID, value and type. Note that OID scalars are
  // exposed under an additional element with the value 0, hence the trailing
  // ".0".
  ASSERT_EQ(42, snmp_get(".1.2.2.0"));
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
  ASSERT_EQ(".1.2.2.1.2.1 = 0", entries[0]);
  ASSERT_EQ(".1.2.2.1.2.2 = 0", entries[1]);
  ASSERT_EQ(".1.2.2.1.2.3 = 0", entries[2]);
  ASSERT_EQ(".1.2.2.1.3.1 = 0", entries[3]);
  ASSERT_EQ(".1.2.2.1.3.2 = 0", entries[4]);

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
  ASSERT_EQ(".1.2.2.1.3.1.4.127.0.0.1 = 1", entries[0]);
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
  ASSERT_EQ(".1.2.2.1.4.1.0.1001 = 0", entries[0]);
  ASSERT_EQ(".1.2.2.1.4.1.0.2001 = 0", entries[1]);
  ASSERT_EQ(".1.2.2.1.4.1.0.2002 = 0", entries[2]);

  // Check the first 3GPP rows.
  ASSERT_EQ(".1.2.2.1.4.1.1.2001 = 0", entries[33]);
  ASSERT_EQ(".1.2.2.1.4.1.1.2002 = 0", entries[34]);
  ASSERT_EQ(".1.2.2.1.4.1.1.2003 = 0", entries[35]);

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

TEST_F(SNMPTest, IPTimeBasedCounterTableSingleIPZeroCount)
{
  cwtest_completely_control_time();

  // Create table
  SNMP::IPTimeBasedCounterTable* tbl =
    SNMP::IPTimeBasedCounterTable::create("ip_time_based_counter", test_oid);

  // Add an IP.
  tbl->add_ip("192.168.0.1");

  // Advance time so we can see any counts in the prev 5s row.
  cwtest_advance_time_ms(5000);

  // All counts should be zero.
  std::vector<std::string> entries = snmp_walk(".1.2.2");

  EXPECT_EQ(entries.size(), 3);
  EXPECT_EQ(".1.2.2.1.4.1.4.192.168.0.1.1 = 0", entries[0]);
  EXPECT_EQ(".1.2.2.1.4.1.4.192.168.0.1.2 = 0", entries[1]);
  EXPECT_EQ(".1.2.2.1.4.1.4.192.168.0.1.3 = 0", entries[2]);

  cwtest_reset_time();
  delete tbl;
}

TEST_F(SNMPTest, IPTimeBasedCounterTableSingleIP)
{
  cwtest_completely_control_time();

  // Create table
  SNMP::IPTimeBasedCounterTable* tbl =
    SNMP::IPTimeBasedCounterTable::create("ip_time_based_counter", test_oid);

  // Add an IP and increment the count two times.
  tbl->add_ip("192.168.0.1");
  tbl->increment("192.168.0.1");
  tbl->increment("192.168.0.1");

  // Advance time so we can see the count in the prev 5s row.
  cwtest_advance_time_ms(5000);

  // Check that the rows that are there are the ones we expect.
  std::vector<std::string> entries = snmp_walk(".1.2.2");

  EXPECT_EQ(entries.size(), 3);
  EXPECT_EQ(".1.2.2.1.4.1.4.192.168.0.1.1 = 2", entries[0]);
  EXPECT_EQ(".1.2.2.1.4.1.4.192.168.0.1.2 = 2", entries[1]);
  EXPECT_EQ(".1.2.2.1.4.1.4.192.168.0.1.3 = 0", entries[2]);

  cwtest_reset_time();
  delete tbl;
}

TEST_F(SNMPTest, IPTimeBasedCounterTableMultipleIPs)
{
  cwtest_completely_control_time();

  // Create table
  SNMP::IPTimeBasedCounterTable* tbl =
    SNMP::IPTimeBasedCounterTable::create("ip_time_based_counter", test_oid);

  // Add three IPs and increment some counts.
  tbl->add_ip("192.168.0.1");
  tbl->increment("192.168.0.1");

  tbl->add_ip("192.168.0.2");
  tbl->increment("192.168.0.2");
  tbl->increment("192.168.0.2");

  tbl->add_ip("192.168.0.3");
  tbl->increment("192.168.0.3");
  tbl->increment("192.168.0.3");
  tbl->increment("192.168.0.3");

  // Advance time so we can see the count in the prev 5s row.
  cwtest_advance_time_ms(5000);

  // Check that the rows that are there are the ones we expect.
  std::vector<std::string> entries = snmp_walk(".1.2.2");

  EXPECT_EQ(entries.size(), 9);
  EXPECT_EQ(".1.2.2.1.4.1.4.192.168.0.1.1 = 1", entries[0]);
  EXPECT_EQ(".1.2.2.1.4.1.4.192.168.0.1.2 = 1", entries[1]);
  EXPECT_EQ(".1.2.2.1.4.1.4.192.168.0.1.3 = 0", entries[2]);
  EXPECT_EQ(".1.2.2.1.4.1.4.192.168.0.2.1 = 2", entries[3]);
  EXPECT_EQ(".1.2.2.1.4.1.4.192.168.0.2.2 = 2", entries[4]);
  EXPECT_EQ(".1.2.2.1.4.1.4.192.168.0.2.3 = 0", entries[5]);
  EXPECT_EQ(".1.2.2.1.4.1.4.192.168.0.3.1 = 3", entries[6]);
  EXPECT_EQ(".1.2.2.1.4.1.4.192.168.0.3.2 = 3", entries[7]);
  EXPECT_EQ(".1.2.2.1.4.1.4.192.168.0.3.3 = 0", entries[8]);

  cwtest_reset_time();
  delete tbl;
}

TEST_F(SNMPTest, IPTimeBasedCounterTableRemoveIP)
{
  cwtest_completely_control_time();

  // Create table
  SNMP::IPTimeBasedCounterTable* tbl =
    SNMP::IPTimeBasedCounterTable::create("ip_time_based_counter", test_oid);

  // Add two IPs and increment some counts.
  tbl->add_ip("192.168.0.1");
  tbl->increment("192.168.0.1");

  tbl->add_ip("192.168.0.2");
  tbl->increment("192.168.0.2");
  tbl->increment("192.168.0.2");

  // Advance time so we can see the count in the prev 5s row.
  cwtest_advance_time_ms(5000);

  // Check that the rows that are there are the ones we expect.
  std::vector<std::string> entries = snmp_walk(".1.2.2");

  EXPECT_EQ(entries.size(), 6);
  EXPECT_EQ(".1.2.2.1.4.1.4.192.168.0.1.1 = 1", entries[0]);
  EXPECT_EQ(".1.2.2.1.4.1.4.192.168.0.1.2 = 1", entries[1]);
  EXPECT_EQ(".1.2.2.1.4.1.4.192.168.0.1.3 = 0", entries[2]);
  EXPECT_EQ(".1.2.2.1.4.1.4.192.168.0.2.1 = 2", entries[3]);
  EXPECT_EQ(".1.2.2.1.4.1.4.192.168.0.2.2 = 2", entries[4]);
  EXPECT_EQ(".1.2.2.1.4.1.4.192.168.0.2.3 = 0", entries[5]);

  // Delete a row and check it disappears from the table.
  tbl->remove_ip("192.168.0.1");

  entries = snmp_walk(".1.2.2");
  EXPECT_EQ(entries.size(), 3);
  EXPECT_EQ(".1.2.2.1.4.1.4.192.168.0.2.1 = 2", entries[0]);
  EXPECT_EQ(".1.2.2.1.4.1.4.192.168.0.2.2 = 2", entries[1]);
  EXPECT_EQ(".1.2.2.1.4.1.4.192.168.0.2.3 = 0", entries[2]);

  cwtest_reset_time();
  delete tbl;
}


TEST_F(SNMPTest, IPTimeBasedCounterTableAddCountsAgeOut)
{
  cwtest_completely_control_time();

  // Create table
  SNMP::IPTimeBasedCounterTable* tbl =
    SNMP::IPTimeBasedCounterTable::create("ip_time_based_counter", test_oid);

  tbl->add_ip("192.168.0.1");
  tbl->increment("192.168.0.1");

  // Initially the count should only be non-zero in the current 5min row.
  std::vector<std::string> entries = snmp_walk(".1.2.2");

  EXPECT_EQ(entries.size(), 3);
  EXPECT_EQ(".1.2.2.1.4.1.4.192.168.0.1.1 = 0", entries[0]);
  EXPECT_EQ(".1.2.2.1.4.1.4.192.168.0.1.2 = 1", entries[1]);
  EXPECT_EQ(".1.2.2.1.4.1.4.192.168.0.1.3 = 0", entries[2]);

  // After 5s the count appears in the previous 5s row.
  cwtest_advance_time_ms(5000);
  entries = snmp_walk(".1.2.2");
  EXPECT_EQ(entries.size(), 3);
  EXPECT_EQ(".1.2.2.1.4.1.4.192.168.0.1.1 = 1", entries[0]);

  // After another 5s the count disappears in the previous 5s row.
  cwtest_advance_time_ms(5000);
  entries = snmp_walk(".1.2.2");
  EXPECT_EQ(entries.size(), 3);
  EXPECT_EQ(".1.2.2.1.4.1.4.192.168.0.1.1 = 0", entries[0]);

  // After another 4m50s the count moves from the current 5min row to the
  // previous 5min row.
  cwtest_advance_time_ms(5 * 60 * 1000 - 5000);
  entries = snmp_walk(".1.2.2");
  EXPECT_EQ(entries.size(), 3);
  EXPECT_EQ(".1.2.2.1.4.1.4.192.168.0.1.1 = 0", entries[0]);
  EXPECT_EQ(".1.2.2.1.4.1.4.192.168.0.1.2 = 0", entries[1]);
  EXPECT_EQ(".1.2.2.1.4.1.4.192.168.0.1.3 = 1", entries[2]);

  // After another 5 mins the counts have disappeared entirely. .
  cwtest_advance_time_ms(5 * 60 * 1000);
  entries = snmp_walk(".1.2.2");
  EXPECT_EQ(entries.size(), 3);
  EXPECT_EQ(".1.2.2.1.4.1.4.192.168.0.1.1 = 0", entries[0]);
  EXPECT_EQ(".1.2.2.1.4.1.4.192.168.0.1.2 = 0", entries[1]);
  EXPECT_EQ(".1.2.2.1.4.1.4.192.168.0.1.3 = 0", entries[2]);

  cwtest_reset_time();
  delete tbl;
}
