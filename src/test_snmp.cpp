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
#include "snmp_includes.h"
#include "snmp_latency_table.h"
#include "snmp_scalar.h"
#include <net-snmp/agent/net-snmp-agent-includes.h>


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

class SNMPTest : public ::testing::Test
{
  SNMPTest() {};

  static void SetUpTestCase()
  {
    char cwd[256];
    getcwd(cwd, sizeof(cwd));
    netsnmp_ds_set_string(NETSNMP_DS_LIBRARY_ID,
                          NETSNMP_DS_LIB_CONFIGURATION_DIR,
                          cwd);
    netsnmp_ds_set_string(NETSNMP_DS_LIBRARY_ID,
                          NETSNMP_DS_LIB_MIBDIRS,
                          "");

    snmp_enable_filelog("fvtest-snmpd.out", 0);
    init_agent("fvtest");
    init_snmp("fvtest");
    init_master_agent();

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
  oid test_oid[] = { 1, 2, 2 };
  SNMP::U32Scalar scalar("answer", test_oid, OID_LENGTH(test_oid));
  scalar.value = 42;

  FILE* fd = popen("snmpget -v2c -On -c clearwater 127.0.0.1:16161 .1.2.2", "r");
  char buf[1024];
  fgets(buf, sizeof(buf), fd);
  ASSERT_STREQ(".1.2.2 = Gauge32: 42\n", buf);
}


TEST_F(SNMPTest, TableOrdering)
{
  oid test_oid[] = { 1, 2, 2 };
  SNMP::AccumulatorTable tbl("latency", test_oid, OID_LENGTH(test_oid));

  FILE* fd = popen("snmpwalk -v2c -On -c clearwater 127.0.0.1:16161 .1.2.2", "r");
  char buf[1024];
  fgets(buf, sizeof(buf), fd);
  ASSERT_STREQ(".1.2.2.1.2.0 = Gauge32: 0\n", buf);
  fgets(buf, sizeof(buf), fd);
  ASSERT_STREQ(".1.2.2.1.2.1 = Gauge32: 0\n", buf);
  fgets(buf, sizeof(buf), fd);
  ASSERT_STREQ(".1.2.2.1.3.0 = Gauge32: 0\n", buf);
  fgets(buf, sizeof(buf), fd);
  ASSERT_STREQ(".1.2.2.1.3.1 = Gauge32: 0\n", buf);
}
