/**
 * @file test_memcachedsolution.cpp FV tests for Clearwater's memcached
 * solution.
 *
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

#include "memcachedstore.h"
#include "memcachedinstance.h"
#include "astaireinstance.h"
#include "test_interposer.hpp"

#include <vector>
#include <iostream>
#include <fstream>
#include <stdio.h>

static const SAS::TrailId DUMMY_TRAIL_ID = 0x12345678;
static int BASE_MEMCACHED_PORT = 33333;
static int ASTAIRE_PORT = 11311;

// Fixture for all memcached tests.
//
// This fixture:
// - Creates a unique key for the test.
// - Connects to the memcached server using libmemcached, and provides utility
//   methods for using it.
class BaseMemcachedSolutionTest : public ::testing::Test
{
public:
  static void TearDownTestCase()
  {
    for (std::vector<std::shared_ptr<MemcachedInstance>>::iterator inst = _memcached_instances.begin();
         inst != _memcached_instances.end();
         ++inst)
    {
      (*inst)->kill_instance();
    }
    _memcached_instances.clear();

    for (std::vector<std::shared_ptr<AstaireInstance>>::iterator inst = _astaire_instances.begin();
         inst != _astaire_instances.end();
         ++inst)
    {
      (*inst)->kill_instance();
    }
    _astaire_instances.clear();

    if (remove("cluster_settings") != 0)
    {
      perror("remove cluster_settings");
    }
  }

  virtual void SetUp()
  {
    _store = new TopologyNeutralMemcachedStore();

    // Create a new key for every test (to prevent tests from interacting with
    // each other).
    _key = std::to_string(_next_key++);
  }

  virtual void TearDown()
  {
    delete _store; _store = NULL;
  }

  static void create_and_start_memcached_instances(int memcached_instances)
  {
    std::ofstream cluster_settings("cluster_settings");

    for (int ii = 0; ii < memcached_instances; ++ii)
    {
      if (ii == 0)
      {
        cluster_settings << "servers=";
      }
      else
      {
        cluster_settings << ",";
      }

      int port = BASE_MEMCACHED_PORT + ii;
      _memcached_instances.emplace_back(new MemcachedInstance(port));
      _memcached_instances.back()->start_instance();
      cluster_settings << "127.0.0.1:";
      cluster_settings << std::to_string(port).c_str();
    }

    cluster_settings.close();
  }

  static void create_and_start_astaire_instances(int astaire_instances)
  {
    for (int ii = 0; ii < astaire_instances; ++ii)
    {
      _astaire_instances.emplace_back(new AstaireInstance(ASTAIRE_PORT));
      _astaire_instances.back()->start_instance();
    }
  }

  static void wait_for_instances()
  {
    for (size_t ii = 0; ii < _memcached_instances.size(); ++ii)
    {
      wait_for_instance(BASE_MEMCACHED_PORT + ii);
    }

    for (size_t ii = 0; ii < _astaire_instances.size(); ++ii)
    {
      wait_for_instance(ASTAIRE_PORT);
    }
  }

  static void wait_for_instance(int port)
  {
    struct addrinfo hints, *res;
    int sockfd;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    getaddrinfo("127.0.0.1", std::to_string(port).c_str(), &hints, &res);
    sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);

    bool connected = false;
    while (!connected)
    {
      if (connect(sockfd, res->ai_addr, res->ai_addrlen) == 0)
      {
        connected = true;
        close(sockfd);
      }
    }
  }

  Store::Status set_data(std::string& data, uint64_t cas, int expiry = 5)
  {
    return set_data(_key, data, cas, expiry);
  }

  Store::Status set_data(std::string& key,
                         std::string& data,
                         uint64_t cas,
                         int expiry = 5)
  {
    return _store->set_data(_table,
                            key,
                            data,
                            cas,
                            expiry,
                            DUMMY_TRAIL_ID);
  }

  Store::Status get_data(std::string& data, uint64_t& cas)
  {
    return get_data(_key, data, cas);
  }

  Store::Status get_data(std::string& key, std::string& data, uint64_t& cas)
  {
    return _store->get_data(_table,
                            key,
                            data,
                            cas,
                            DUMMY_TRAIL_ID);
  }

  Store::Status delete_data()
  {
    return _store->delete_data(_table, _key, DUMMY_TRAIL_ID);
  }

  TopologyNeutralMemcachedStore* _store;
  static std::vector<std::shared_ptr<MemcachedInstance>> _memcached_instances;
  static std::vector<std::shared_ptr<AstaireInstance>> _astaire_instances;

  // Tests that use this fixture use a monotonically incrementing numerical key
  // (so that tests are isolated from each other). This variable stores the
  // next key to use.
  static unsigned int _next_key;

  // The table and key to use for the test. The table is the same in all tests.
  const static std::string _table;
  std::string _key;
};

std::vector<std::shared_ptr<MemcachedInstance>> BaseMemcachedSolutionTest::_memcached_instances;
std::vector<std::shared_ptr<AstaireInstance>> BaseMemcachedSolutionTest::_astaire_instances;

unsigned int BaseMemcachedSolutionTest::_next_key;
const std::string BaseMemcachedSolutionTest::_table = "test_table";

class SimpleMemcachedSolutionTest : public BaseMemcachedSolutionTest
{
  static void SetUpTestCase()
  {
    _next_key = std::rand();
    create_and_start_memcached_instances(2);
    create_and_start_astaire_instances(1);
    wait_for_instances();
  }
};

TEST_F(SimpleMemcachedSolutionTest, AddGet)
{
  uint64_t cas = 0;
  Store::Status rc;
  std::string data_in = "SimpleMemcachedSolutionTest.AddGet";
  std::string data_out;

  rc = set_data(data_in, cas);
  EXPECT_EQ(Store::Status::OK, rc);

  rc = get_data(data_out, cas);
  EXPECT_EQ(Store::Status::OK, rc);
  EXPECT_EQ(data_out, data_in);
}

TEST_F(SimpleMemcachedSolutionTest, AddGetTwoKeys)
{
  uint64_t cas1 = 0;
  uint64_t cas2 = 0;
  Store::Status rc;
  std::string key1 = _key;
  _key = std::to_string(_next_key++);
  std::string key2 = _key;
  std::string data_in1 = "SimpleMemcachedSolutionTest.AddGetTwoKeys1";
  std::string data_in2 = "SimpleMemcachedSolutionTest.AddGetTwoKeys2";
  std::string data_out1;
  std::string data_out2;

  rc = set_data(key1, data_in1, cas1);
  EXPECT_EQ(Store::Status::OK, rc);
  rc = set_data(key2, data_in2, cas2);
  EXPECT_EQ(Store::Status::OK, rc);

  rc = get_data(key1, data_out1, cas1);
  EXPECT_EQ(Store::Status::OK, rc);
  EXPECT_EQ(data_out1, data_in1);
  rc = get_data(key2, data_out2, cas2);
  EXPECT_EQ(Store::Status::OK, rc);
  EXPECT_EQ(data_out2, data_in2);
}

TEST_F(SimpleMemcachedSolutionTest, AddGetExpire)
{
  uint64_t cas = 0;
  Store::Status rc;
  std::string data_in = "SimpleMemcachedSolutionTest.AddGetExpire";
  std::string data_out;

  rc = set_data(data_in, cas, 1);
  EXPECT_EQ(Store::Status::OK, rc);

  rc = get_data(data_out, cas);
  EXPECT_EQ(Store::Status::OK, rc);
  EXPECT_EQ(data_out, data_in);

  sleep(1);

  rc = get_data(data_out, cas);
  EXPECT_EQ(Store::Status::NOT_FOUND, rc);

  cwtest_reset_time();
}

TEST_F(SimpleMemcachedSolutionTest, AddSetSetDataContentionSet)
{
  uint64_t cas = 0;
  Store::Status rc;
  std::string data_in = "SimpleMemcachedSolutionTest.AddSetSetDataContentionSet";
  std::string data_out;

  rc = set_data(data_in, cas);
  EXPECT_EQ(Store::Status::OK, rc);

  rc = get_data(data_out, cas);
  EXPECT_EQ(Store::Status::OK, rc);
  EXPECT_EQ(data_out, data_in);

  data_in = "SimpleMemcachedSolutionTest.AddSetSetDataContentionSet_New1";
  rc = set_data(data_in, cas);
  EXPECT_EQ(Store::Status::OK, rc);

  std::string failed_data_in = "FAIL";
  rc = set_data(data_in, cas);
  EXPECT_EQ(Store::Status::DATA_CONTENTION, rc);

  rc = get_data(data_out, cas);
  EXPECT_EQ(Store::Status::OK, rc);
  EXPECT_EQ(data_out, data_in);

  data_in = "SimpleMemcachedSolutionTest.AddSetSetDataContentionSet_New2";
  rc = set_data(data_in, cas);
  EXPECT_EQ(Store::Status::OK, rc);

  rc = get_data(data_out, cas);
  EXPECT_EQ(Store::Status::OK, rc);
  EXPECT_EQ(data_out, data_in);
}

TEST_F(SimpleMemcachedSolutionTest, AddSetCASDeleteDataContentionCASDelete)
{
  uint64_t cas = 0;
  Store::Status rc;
  std::string data_in = "SimpleMemcachedSolutionTest.AddSetCASDeleteDataContentionCASDelete";
  std::string data_out;

  rc = set_data(data_in, cas);
  EXPECT_EQ(Store::Status::OK, rc);

  rc = get_data(data_out, cas);
  EXPECT_EQ(Store::Status::OK, rc);
  EXPECT_EQ(data_out, data_in);

  data_in = "SimpleMemcachedSolutionTest.AddSetCASDeleteDataContentionCASDelete_New";
  rc = set_data(data_in, cas);
  EXPECT_EQ(Store::Status::OK, rc);

  std::string failed_data_in = "FAIL";
  rc = set_data(data_in, cas, 0);
  EXPECT_EQ(Store::Status::DATA_CONTENTION, rc);

  rc = get_data(data_out, cas);
  EXPECT_EQ(Store::Status::OK, rc);
  EXPECT_EQ(data_out, data_in);

  data_in = "DELETE";
  rc = set_data(data_in, cas, 0);
  EXPECT_EQ(Store::Status::OK, rc);

  rc = get_data(data_out, cas);
  EXPECT_EQ(Store::Status::NOT_FOUND, rc);
}

TEST_F(SimpleMemcachedSolutionTest, AddAddDataContention)
{
  uint64_t cas = 0;
  Store::Status rc;
  std::string data_in = "SimpleMemcachedSolutionTest.AddAddDataContention";
  std::string data_out;

  rc = set_data(data_in, cas);
  EXPECT_EQ(Store::Status::OK, rc);

  std::string new_data_in = "SimpleMemcachedSolutionTest.AddAddDataContention_New";
  uint64_t new_cas = 0;
  rc = set_data(new_data_in, new_cas);
  EXPECT_EQ(Store::Status::DATA_CONTENTION, rc);

  rc = get_data(data_out, cas);
  EXPECT_EQ(Store::Status::OK, rc);
  EXPECT_EQ(data_out, data_in);
}

TEST_F(SimpleMemcachedSolutionTest, AddDelete)
{
  uint64_t cas = 0;
  Store::Status rc;
  std::string data_in = "SimpleMemcachedSolutionTest.AddDelete";
  std::string data_out;

  rc = set_data(data_in, cas);
  EXPECT_EQ(Store::Status::OK, rc);

  rc = get_data(data_out, cas);
  EXPECT_EQ(Store::Status::OK, rc);
  EXPECT_EQ(data_out, data_in);

  rc = delete_data();
  EXPECT_EQ(Store::Status::OK, rc);

  rc = get_data(data_out, cas);
  EXPECT_EQ(Store::Status::NOT_FOUND, rc);
}

TEST_F(SimpleMemcachedSolutionTest, Delete)
{
  Store::Status rc;
  rc = delete_data();
  EXPECT_EQ(Store::Status::OK, rc);
}

TEST_F(SimpleMemcachedSolutionTest, AddDeleteAdd)
{
  uint64_t cas = 0;
  Store::Status rc;
  std::string data_in = "SimpleMemcachedSolutionTest.AddDeleteAdd";
  std::string data_out;

  rc = set_data(data_in, cas);
  EXPECT_EQ(Store::Status::OK, rc);

  rc = get_data(data_out, cas);
  EXPECT_EQ(Store::Status::OK, rc);
  EXPECT_EQ(data_out, data_in);

  rc = delete_data();
  EXPECT_EQ(Store::Status::OK, rc);

  rc = get_data(data_out, cas);
  EXPECT_EQ(Store::Status::NOT_FOUND, rc);

  rc = set_data(data_in, cas);
  EXPECT_EQ(Store::Status::OK, rc);

  rc = get_data(data_out, cas);
  EXPECT_EQ(Store::Status::OK, rc);
  EXPECT_EQ(data_out, data_in);
}

TEST_F(SimpleMemcachedSolutionTest, AddDeleteSetDataContention)
{
  uint64_t cas = 0;
  Store::Status rc;
  std::string data_in = "SimpleMemcachedSolutionTest.AddDeleteSetDataContention";
  std::string data_out;

  rc = set_data(data_in, cas);
  EXPECT_EQ(Store::Status::OK, rc);

  rc = get_data(data_out, cas);
  EXPECT_EQ(Store::Status::OK, rc);
  EXPECT_EQ(data_out, data_in);

  rc = delete_data();
  EXPECT_EQ(Store::Status::OK, rc);

  data_in = "SimpleMemcachedSolutionTest.AddDeleteSetDataContention_New";
  rc = set_data(data_in, cas);
  EXPECT_EQ(Store::Status::DATA_CONTENTION, rc);
}

class LargerClustersMemcachedSolutionTest : public BaseMemcachedSolutionTest
{
  static void SetUpTestCase()
  {
    _next_key = std::rand();
    create_and_start_memcached_instances(3);
    create_and_start_astaire_instances(1);
    wait_for_instances();
  }
};

TEST_F(LargerClustersMemcachedSolutionTest, AddGet)
{
  uint64_t cas = 0;
  Store::Status rc;
  std::string data_in = "LargerClustersMemcachedSolutionTest.AddGet";
  std::string data_out;

  rc = set_data(data_in, cas);
  EXPECT_EQ(Store::Status::OK, rc);

  rc = get_data(data_out, cas);
  EXPECT_EQ(Store::Status::OK, rc);
  EXPECT_EQ(data_out, data_in);
}
