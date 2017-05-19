/**
 * Copyright (C) Metaswitch Networks
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */

#include "gtest/gtest.h"
#include "gmock/gmock.h"
#include "dnscachedresolver.h"
#include "processinstance.h"

class DNSTest : public ::testing::Test
{
  DNSTest() {};

};

TEST_F(DNSTest, BasicQuery)
{
  DnsmasqInstance server("127.0.0.201", 5353, {{"test.query", {"1.2.3.4", "5.6.7.8"}}});
  server.start_instance();
  server.wait_for_instance();
  // Send a DNS query to confirm it doesn't leak memory
  DnsCachedResolver* r = new DnsCachedResolver("127.0.0.201", 5353);
  DnsResult answer = r->dns_query("test.query", ns_t_a, 0);
  ASSERT_EQ(answer.records().size(), 2);
  delete r;
}

