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
#include "processinstance.h"

#include <vector>
#include <iostream>
#include <fstream>
#include <stdio.h>

static const SAS::TrailId DUMMY_TRAIL_ID = 0x12345678;
static const int BASE_MEMCACHED_PORT = 33333;
static const int ASTAIRE_PORT = 11311;

// Fixture for all memcached solution tests.
//
// This fixture:
// - Creates a unique key and an instance of TopologyNeutralMemcachedStore for
//   each test.
// - Tidies up instances of memcached and Astaire and any config files between
//   sets of tests.
// - Provides helper methods for memcached operations and managing instances of
//   memcached and Astaire
class BaseMemcachedSolutionTest : public ::testing::Test
{
public:
  /// Kill and clear all the memcached and Astaire instances, and remove the
  /// cluster_settings file.
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

  /// Create a new store and a new unique key for this test.
  virtual void SetUp()
  {
    _store = new TopologyNeutralMemcachedStore();

    // Create a new key for every test (to prevent tests from interacting with
    // each other).
    _key = std::to_string(_next_key++);
  }

  /// Make sure all the memcached and Astaire instances are running before
  /// starting finishing the test.
  virtual void TearDown()
  {
    EXPECT_TRUE(wait_for_instances());

    delete _store; _store = NULL;
  }

  /// Helper method for generating a new unique key in the middle of a test.
  void get_new_key()
  {
    _key = std::to_string(_next_key++);
  }

  /// Creates and starts up the specified number of memcached instances. Also
  /// sets up the appropriate cluster_settings file (which just contains a
  /// single line of the form:
  ///
  ///   servers=127.0.0.1:33333,127.0.0.1:33334,...
  ///
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

      // Each instance should listen on a new port.
      int port = BASE_MEMCACHED_PORT + ii;
      _memcached_instances.emplace_back(new MemcachedInstance(port));
      _memcached_instances.back()->start_instance();

      cluster_settings << "127.0.0.1:";
      cluster_settings << std::to_string(port).c_str();
    }

    cluster_settings.close();
  }

  /// Creates and starts up the specified number of Astaire instances. We
  /// currently only support one Astaire instance (since the port Astaire
  /// listens on is not currently configurable).
  static void create_and_start_astaire_instances(int astaire_instances)
  {
    for (int ii = 0; ii < astaire_instances; ++ii)
    {
      _astaire_instances.emplace_back(new AstaireInstance(ASTAIRE_PORT));
      _astaire_instances.back()->start_instance();
    }
  }

  /// Wait for all existing memcached and Astaire instances to come up by
  /// checking they're listening on the correct ports. Returns false if any of
  /// the instances fail to come up.
  static bool wait_for_instances()
  {
    bool success = true;

    for (std::vector<std::shared_ptr<MemcachedInstance>>::iterator inst = _memcached_instances.begin();
         inst != _memcached_instances.end();
         ++inst)
    {
      (*inst)->wait_for_instance();

      if (!success)
      {
        return success;
      }
    }

    for (std::vector<std::shared_ptr<AstaireInstance>>::iterator inst = _astaire_instances.begin();
         inst != _astaire_instances.end();
         ++inst)
    {
      (*inst)->wait_for_instance();

      if (!success)
      {
        return success;
      }
    }

    return success;
  }

  /// Helper method for setting data in memcached for the test's default key.
  Store::Status set_data(std::string& data, uint64_t cas, int expiry = 5)
  {
    return set_data(_key, data, cas, expiry);
  }

  /// Helper method for setting data in memcached for a specified key.
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

  /// Helper method for getting data from memcached for the test's default key.
  Store::Status get_data(std::string& data, uint64_t& cas)
  {
    return get_data(_key, data, cas);
  }

  /// Helper method for getting data from memcached for a specified key.
  Store::Status get_data(std::string& key, std::string& data, uint64_t& cas)
  {
    return _store->get_data(_table,
                            key,
                            data,
                            cas,
                            DUMMY_TRAIL_ID);
  }

  /// Helper method for deleting data from memcached for the test's default key.
  Store::Status delete_data()
  {
    return _store->delete_data(_table, _key, DUMMY_TRAIL_ID);
  }

  TopologyNeutralMemcachedStore* _store;

  /// Use shared pointers for managing the instances so that the memory gets
  /// freed when the vector is cleared.
  static std::vector<std::shared_ptr<MemcachedInstance>> _memcached_instances;
  static std::vector<std::shared_ptr<AstaireInstance>> _astaire_instances;

  /// Tests that use this fixture use a monotonically incrementing numerical key
  /// (so that tests are isolated from each other). This variable stores the
  /// next key to use.
  static unsigned int _next_key;

  // The table and key to use for the test. The table is the same in all tests.
  const static std::string _table;
  std::string _key;
};

std::vector<std::shared_ptr<MemcachedInstance>> BaseMemcachedSolutionTest::_memcached_instances;
std::vector<std::shared_ptr<AstaireInstance>> BaseMemcachedSolutionTest::_astaire_instances;

unsigned int BaseMemcachedSolutionTest::_next_key;
const std::string BaseMemcachedSolutionTest::_table = "test_table";

/// A class for killing and restoring a memcached instance. Used as a template
/// along with MemcachedRestarter in the tests below.
class MemcachedKiller
{
  static void fail_memcached_instance(std::shared_ptr<MemcachedInstance> instance)
  {
    instance->kill_instance();
  }

  static void restore_memcached_instance(std::shared_ptr<MemcachedInstance> instance)
  {
    instance->start_instance();
    EXPECT_TRUE(instance->wait_for_instance());
  }
};

/// A class for restarting a memcached instance. Used as a template along with
/// MemcachedKiller in the tests below.
class MemcachedRestarter
{
  static void fail_memcached_instance(std::shared_ptr<MemcachedInstance> instance)
  {
    instance->restart_instance();
    EXPECT_TRUE(instance->wait_for_instance());
  }

  static void restore_memcached_instance(std::shared_ptr<MemcachedInstance> instance)
  {
  }
};

typedef ::testing::Types<MemcachedKiller, MemcachedRestarter> MemcachedFailHelpers;

/// A fixture for testing a deployment with 1 Astaire and 2 memcacheds.
/// Templated so that we can test that behaviour is the same in the scenarios
/// where a memcached dies and where a memcached bounces.
template <class T>
class SimpleMemcachedSolutionTest : public BaseMemcachedSolutionTest
{
  static void SetUpTestCase()
  {
    _next_key = std::rand();
    create_and_start_memcached_instances(2);
    create_and_start_astaire_instances(1);
    EXPECT_TRUE(wait_for_instances());
  }
};

TYPED_TEST_CASE(SimpleMemcachedSolutionTest, MemcachedFailHelpers);

/// Add a key and retrieve it.
TYPED_TEST(SimpleMemcachedSolutionTest, AddGet)
{
  uint64_t cas = 0;
  Store::Status rc;
  std::string data_in = "SimpleMemcachedSolutionTest.AddGet";
  std::string data_out;

  rc = this->set_data(data_in, cas);
  EXPECT_EQ(Store::Status::OK, rc);

  rc = this->get_data(data_out, cas);
  EXPECT_EQ(Store::Status::OK, rc);
  EXPECT_EQ(data_out, data_in);
}

/// Add two keys and retrieve them.
TYPED_TEST(SimpleMemcachedSolutionTest, AddGetTwoKeys)
{
  uint64_t cas1 = 0;
  uint64_t cas2 = 0;
  Store::Status rc;
  std::string key1 = this->_key;
  this->get_new_key();
  std::string key2 = this->_key;
  std::string data_in1 = "SimpleMemcachedSolutionTest.AddGetTwoKeys1";
  std::string data_in2 = "SimpleMemcachedSolutionTest.AddGetTwoKeys2";
  std::string data_out1;
  std::string data_out2;

  rc = this->set_data(key1, data_in1, cas1);
  EXPECT_EQ(Store::Status::OK, rc);
  rc = this->set_data(key2, data_in2, cas2);
  EXPECT_EQ(Store::Status::OK, rc);

  rc = this->get_data(key1, data_out1, cas1);
  EXPECT_EQ(Store::Status::OK, rc);
  EXPECT_EQ(data_out1, data_in1);
  rc = this->get_data(key2, data_out2, cas2);
  EXPECT_EQ(Store::Status::OK, rc);
  EXPECT_EQ(data_out2, data_in2);
}

/// Add a key that expires.
TYPED_TEST(SimpleMemcachedSolutionTest, AddGetExpire)
{
  uint64_t cas = 0;
  Store::Status rc;
  std::string data_in = "SimpleMemcachedSolutionTest.AddGetExpire";
  std::string data_out;

  rc = this->set_data(data_in, cas, 1);
  EXPECT_EQ(Store::Status::OK, rc);

  rc = this->get_data(data_out, cas);
  EXPECT_EQ(Store::Status::OK, rc);
  EXPECT_EQ(data_out, data_in);

  sleep(2);

  rc = this->get_data(data_out, cas);
  EXPECT_EQ(Store::Status::NOT_FOUND, rc);
}

/// Add a key, retrieve it and try to update it twice. The second attempt fails
/// due to data contention.
TYPED_TEST(SimpleMemcachedSolutionTest, AddSetSetDataContentionSet)
{
  uint64_t cas = 0;
  Store::Status rc;
  std::string data_in = "SimpleMemcachedSolutionTest.AddSetSetDataContentionSet";
  std::string data_out;

  rc = this->set_data(data_in, cas);
  EXPECT_EQ(Store::Status::OK, rc);

  rc = this->get_data(data_out, cas);
  EXPECT_EQ(Store::Status::OK, rc);
  EXPECT_EQ(data_out, data_in);

  data_in = "SimpleMemcachedSolutionTest.AddSetSetDataContentionSet_New1";
  rc = this->set_data(data_in, cas);
  EXPECT_EQ(Store::Status::OK, rc);

  std::string failed_data_in = "FAIL";
  rc = this->set_data(data_in, cas);
  EXPECT_EQ(Store::Status::DATA_CONTENTION, rc);

  rc = this->get_data(data_out, cas);
  EXPECT_EQ(Store::Status::OK, rc);
  EXPECT_EQ(data_out, data_in);

  data_in = "SimpleMemcachedSolutionTest.AddSetSetDataContentionSet_New2";
  rc = this->set_data(data_in, cas);
  EXPECT_EQ(Store::Status::OK, rc);

  rc = this->get_data(data_out, cas);
  EXPECT_EQ(Store::Status::OK, rc);
  EXPECT_EQ(data_out, data_in);
}

/// Add a key and retrieve it. Try to update the key with a new value and also
/// to update it with an expiry of 0. The delete update fails due to data
/// contention. Try it again and check the key has gone.
TYPED_TEST(SimpleMemcachedSolutionTest, AddSetCASDeleteDataContentionCASDelete)
{
  uint64_t cas = 0;
  Store::Status rc;
  std::string data_in = "SimpleMemcachedSolutionTest.AddSetCASDeleteDataContentionCASDelete";
  std::string data_out;

  rc = this->set_data(data_in, cas);
  EXPECT_EQ(Store::Status::OK, rc);

  rc = this->get_data(data_out, cas);
  EXPECT_EQ(Store::Status::OK, rc);
  EXPECT_EQ(data_out, data_in);

  data_in = "SimpleMemcachedSolutionTest.AddSetCASDeleteDataContentionCASDelete_New";
  rc = this->set_data(data_in, cas);
  EXPECT_EQ(Store::Status::OK, rc);

  std::string failed_data_in = "FAIL";
  rc = this->set_data(data_in, cas, 0);
  EXPECT_EQ(Store::Status::DATA_CONTENTION, rc);

  rc = this->get_data(data_out, cas);
  EXPECT_EQ(Store::Status::OK, rc);
  EXPECT_EQ(data_out, data_in);

  data_in = "DELETE";
  rc = this->set_data(data_in, cas, 0);
  EXPECT_EQ(Store::Status::OK, rc);

  rc = this->get_data(data_out, cas);
  EXPECT_EQ(Store::Status::NOT_FOUND, rc);
}

/// Add a key twice. The second one fails due to data contention.
TYPED_TEST(SimpleMemcachedSolutionTest, AddAddDataContention)
{
  uint64_t cas = 0;
  Store::Status rc;
  std::string data_in = "SimpleMemcachedSolutionTest.AddAddDataContention";
  std::string data_out;

  rc = this->set_data(data_in, cas);
  EXPECT_EQ(Store::Status::OK, rc);

  std::string new_data_in = "SimpleMemcachedSolutionTest.AddAddDataContention_New";
  uint64_t new_cas = 0;
  rc = this->set_data(new_data_in, new_cas);
  EXPECT_EQ(Store::Status::DATA_CONTENTION, rc);

  rc = this->get_data(data_out, cas);
  EXPECT_EQ(Store::Status::OK, rc);
  EXPECT_EQ(data_out, data_in);
}

/// Add a key and delete it.
TYPED_TEST(SimpleMemcachedSolutionTest, AddDelete)
{
  uint64_t cas = 0;
  Store::Status rc;
  std::string data_in = "SimpleMemcachedSolutionTest.AddDelete";
  std::string data_out;

  rc = this->set_data(data_in, cas);
  EXPECT_EQ(Store::Status::OK, rc);

  rc = this->get_data(data_out, cas);
  EXPECT_EQ(Store::Status::OK, rc);
  EXPECT_EQ(data_out, data_in);

  rc = this->delete_data();
  EXPECT_EQ(Store::Status::OK, rc);

  rc = this->get_data(data_out, cas);
  EXPECT_EQ(Store::Status::NOT_FOUND, rc);
}

/// Delete a key that doesn't exist.
TYPED_TEST(SimpleMemcachedSolutionTest, Delete)
{
  Store::Status rc;
  rc = this->delete_data();
  EXPECT_EQ(Store::Status::OK, rc);
}

/// Add a key and delete it. Add it again.
TYPED_TEST(SimpleMemcachedSolutionTest, AddDeleteAdd)
{
  uint64_t cas = 0;
  Store::Status rc;
  std::string data_in = "SimpleMemcachedSolutionTest.AddDeleteAdd";
  std::string data_out;

  rc = this->set_data(data_in, cas);
  EXPECT_EQ(Store::Status::OK, rc);

  rc = this->get_data(data_out, cas);
  EXPECT_EQ(Store::Status::OK, rc);
  EXPECT_EQ(data_out, data_in);

  rc = this->delete_data();
  EXPECT_EQ(Store::Status::OK, rc);

  rc = this->get_data(data_out, cas);
  EXPECT_EQ(Store::Status::NOT_FOUND, rc);

  rc = this->set_data(data_in, cas);
  EXPECT_EQ(Store::Status::OK, rc);

  rc = this->get_data(data_out, cas);
  EXPECT_EQ(Store::Status::OK, rc);
  EXPECT_EQ(data_out, data_in);
}

/// Add a key and delete it. Try to update the key. This fails due to data
/// contention.
TYPED_TEST(SimpleMemcachedSolutionTest, AddDeleteSetDataContention)
{
  uint64_t cas = 0;
  Store::Status rc;
  std::string data_in = "SimpleMemcachedSolutionTest.AddDeleteSetDataContention";
  std::string data_out;

  rc = this->set_data(data_in, cas);
  EXPECT_EQ(Store::Status::OK, rc);

  rc = this->get_data(data_out, cas);
  EXPECT_EQ(Store::Status::OK, rc);
  EXPECT_EQ(data_out, data_in);

  rc = this->delete_data();
  EXPECT_EQ(Store::Status::OK, rc);

  data_in = "SimpleMemcachedSolutionTest.AddDeleteSetDataContention_New";
  rc = this->set_data(data_in, cas);
  EXPECT_EQ(Store::Status::DATA_CONTENTION, rc);
}

/// Kill a memcached instance. Add a key and retrieve it.
TYPED_TEST(SimpleMemcachedSolutionTest, KillAddGet)
{
  TypeParam::fail_memcached_instance(this->_memcached_instances.back());

  uint64_t cas = 0;
  Store::Status rc;
  std::string data_in = "SimpleMemcachedSolutionTest.KillAddGet";
  std::string data_out;

  rc = this->set_data(data_in, cas);
  EXPECT_EQ(Store::Status::OK, rc);

  rc = this->get_data(data_out, cas);
  EXPECT_EQ(Store::Status::OK, rc);
  EXPECT_EQ(data_out, data_in);

  TypeParam::restore_memcached_instance(this->_memcached_instances.back());
}

/// Add a key. Kill a memcached instance. Retrieve the key.
TYPED_TEST(SimpleMemcachedSolutionTest, AddKillGet)
{
  uint64_t cas = 0;
  Store::Status rc;
  std::string data_in = "SimpleMemcachedSolutionTest.AddKillGet";
  std::string data_out;

  rc = this->set_data(data_in, cas);
  EXPECT_EQ(Store::Status::OK, rc);

  TypeParam::fail_memcached_instance(this->_memcached_instances.back());

  rc = this->get_data(data_out, cas);
  EXPECT_EQ(Store::Status::OK, rc);
  EXPECT_EQ(data_out, data_in);

  TypeParam::restore_memcached_instance(this->_memcached_instances.back());
}

/// Add a key. Kill a memcached instance. Try retrieve the key after it should
/// have expired.
TYPED_TEST(SimpleMemcachedSolutionTest, AddKillGetExpire)
{
  uint64_t cas = 0;
  Store::Status rc;
  std::string data_in = "SimpleMemcachedSolutionTest.AddKillGetExpire";
  std::string data_out;

  rc = this->set_data(data_in, cas, 1);
  EXPECT_EQ(Store::Status::OK, rc);

  TypeParam::fail_memcached_instance(this->_memcached_instances.back());

  sleep(2);

  rc = this->get_data(data_out, cas);
  EXPECT_EQ(Store::Status::NOT_FOUND, rc);

  TypeParam::restore_memcached_instance(this->_memcached_instances.back());
}

/// Add a key and retrieve it. Kill a memcached instance. Update the key. This
/// sometimes result in data contention depending on whether the primary or
/// backup memcached for this key has been killed. If it does, retrieve the key
/// again and update the key. This will work. If the first update worked, do
/// another update and check this fails due to data contention.
///
/// Repeat this test 10 times so that we sometimes kill the primary and
/// sometimes the backup.
TYPED_TEST(SimpleMemcachedSolutionTest, AddKillSetSetDataContentionSet)
{
  for (int ii = 0; ii < 10; ++ii)
  {
    this->get_new_key();

    uint64_t cas = 0;
    Store::Status rc;
    std::string data_in = "SimpleMemcachedSolutionTest.AddKillSetSetDataContentionSet";
    std::string data_out;

    rc = this->set_data(data_in, cas);
    EXPECT_EQ(Store::Status::OK, rc);

    rc = this->get_data(data_out, cas);
    EXPECT_EQ(Store::Status::OK, rc);
    EXPECT_EQ(data_out, data_in);

    TypeParam::fail_memcached_instance(this->_memcached_instances.back());

    data_in = "SimpleMemcachedSolutionTest.AddKillSetSetDataContentionSet_New1";
    rc = this->set_data(data_in, cas);

    if (rc == Store::Status::DATA_CONTENTION)
    {
      rc = this->get_data(data_out, cas);
      EXPECT_EQ(Store::Status::OK, rc);

      rc = this->set_data(data_in, cas, 0);
      EXPECT_EQ(Store::Status::OK, rc);
    }
    else if (rc == Store::Status::OK)
    {
      std::string failed_data_in = "FAIL";
      rc = this->set_data(data_in, cas);
      EXPECT_EQ(Store::Status::DATA_CONTENTION, rc);

      rc = this->get_data(data_out, cas);
      EXPECT_EQ(Store::Status::OK, rc);
      EXPECT_EQ(data_out, data_in);

      data_in = "SimpleMemcachedSolutionTest.AddKillSetSetDataContentionSet_New2";
      rc = this->set_data(data_in, cas);
      EXPECT_EQ(Store::Status::OK, rc);

      rc = this->get_data(data_out, cas);
      EXPECT_EQ(Store::Status::OK, rc);
      EXPECT_EQ(data_out, data_in);
    }
    else
    {
      EXPECT_TRUE(false);
    }

    TypeParam::restore_memcached_instance(this->_memcached_instances.back());
  }
}

/// Add a key and delete it. Kill a memcached instance. Try to retrieve the key.
TYPED_TEST(SimpleMemcachedSolutionTest, AddDeleteKill)
{
  uint64_t cas = 0;
  Store::Status rc;
  std::string data_in = "SimpleMemcachedSolutionTest.AddDeleteKill";
  std::string data_out;

  rc = this->set_data(data_in, cas);
  EXPECT_EQ(Store::Status::OK, rc);

  rc = this->get_data(data_out, cas);
  EXPECT_EQ(Store::Status::OK, rc);
  EXPECT_EQ(data_out, data_in);

  rc = this->delete_data();
  EXPECT_EQ(Store::Status::OK, rc);

  TypeParam::fail_memcached_instance(this->_memcached_instances.back());

  rc = this->get_data(data_out, cas);
  EXPECT_EQ(Store::Status::NOT_FOUND, rc);

  TypeParam::restore_memcached_instance(this->_memcached_instances.back());
}

/// Add a key. Kill a memcached instance. Retrieve the key and update it with an
/// expiry of 0. This sometimes result in data contention depending on whether
/// the primary or backup memcached for this key has been killed. If it does,
/// retrieve the key again and update the key with an expiry of 0. Check that
/// the key has gone.
TYPED_TEST(SimpleMemcachedSolutionTest, AddKillCASDelete)
{
  uint64_t cas = 0;
  Store::Status rc;
  std::string data_in = "SimpleMemcachedSolutionTest.AddKillCASDelete";
  std::string data_out;

  rc = this->set_data(data_in, cas);
  EXPECT_EQ(Store::Status::OK, rc);

  TypeParam::fail_memcached_instance(this->_memcached_instances.back());

  rc = this->get_data(data_out, cas);
  EXPECT_EQ(Store::Status::OK, rc);
  EXPECT_EQ(data_out, data_in);

  data_in = "DELETE";
  rc = this->set_data(data_in, cas, 0);

  // TODO: Why is this an ERROR rather than DATA_CONTENTION?
  if (rc == Store::Status::ERROR)
  {
    rc = this->get_data(data_out, cas);
    EXPECT_EQ(Store::Status::OK, rc);

    rc = this->set_data(data_in, cas, 0);
    EXPECT_EQ(Store::Status::OK, rc);
  }
  else if (rc != Store::Status::OK)
  {
    EXPECT_TRUE(false);
  }

  rc = this->get_data(data_out, cas);
  EXPECT_EQ(Store::Status::NOT_FOUND, rc);

  TypeParam::restore_memcached_instance(this->_memcached_instances.back());
}

/// A fixture for testing a deployment with 1 Astaire and 3 memcacheds.
/// Templated so that we can test that behaviour is the same in the scenarios
/// where a memcached dies and where a memcached bounces.
template <class T>
class LargerClustersMemcachedSolutionTest : public BaseMemcachedSolutionTest
{
  static void SetUpTestCase()
  {
    _next_key = std::rand();
    create_and_start_memcached_instances(3);
    create_and_start_astaire_instances(1);
    EXPECT_TRUE(wait_for_instances());
  }
};

TYPED_TEST_CASE(LargerClustersMemcachedSolutionTest, MemcachedFailHelpers);

/// Add a key and retrieve it.
TYPED_TEST(LargerClustersMemcachedSolutionTest, AddGet)
{
  uint64_t cas = 0;
  Store::Status rc;
  std::string data_in = "LargerClustersMemcachedSolutionTest.AddGet";
  std::string data_out;

  rc = this->set_data(data_in, cas);
  EXPECT_EQ(Store::Status::OK, rc);

  rc = this->get_data(data_out, cas);
  EXPECT_EQ(Store::Status::OK, rc);
  EXPECT_EQ(data_out, data_in);
}

/// Add a key. Kill a memcached instance. Retrieve the key.
TYPED_TEST(LargerClustersMemcachedSolutionTest, AddKillGet)
{
  uint64_t cas = 0;
  Store::Status rc;
  std::string data_in = "LargerClustersMemcachedSolutionTest.AddKillGet";
  std::string data_out;

  rc = this->set_data(data_in, cas);
  EXPECT_EQ(Store::Status::OK, rc);

  TypeParam::fail_memcached_instance(this->_memcached_instances.back());

  rc = this->get_data(data_out, cas);
  EXPECT_EQ(Store::Status::OK, rc);
  EXPECT_EQ(data_out, data_in);

  TypeParam::restore_memcached_instance(this->_memcached_instances.back());
}

/// Add a key. Kill a memcached instance. Get the key and update it.
TYPED_TEST(LargerClustersMemcachedSolutionTest, AddKillGetSet)
{
  uint64_t cas = 0;
  Store::Status rc;
  std::string data_in = "LargerClustersMemcachedSolutionTest.AddKillGetSet";
  std::string data_out;

  rc = this->set_data(data_in, cas);
  EXPECT_EQ(Store::Status::OK, rc);

  TypeParam::fail_memcached_instance(this->_memcached_instances.back());

  rc = this->get_data(data_out, cas);
  EXPECT_EQ(Store::Status::OK, rc);
  EXPECT_EQ(data_out, data_in);

  data_in = "LargerClustersMemcachedSolutionTest.AddKillGetSet_New";
  rc = this->set_data(data_in, cas);
  EXPECT_EQ(Store::Status::OK, rc);

  rc = this->get_data(data_out, cas);
  EXPECT_EQ(Store::Status::OK, rc);
  EXPECT_EQ(data_out, data_in);

  TypeParam::restore_memcached_instance(this->_memcached_instances.back());
}
