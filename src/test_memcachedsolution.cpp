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
#include <thread>

static const SAS::TrailId DUMMY_TRAIL_ID = 0x12345678;
static const int BASE_MEMCACHED_PORT = 33333;
static const int ASTAIRE_PORT = 11311;

void signal_handler(int signal);

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
  /// Register the signal handler to tidy up if we crash and set _key to a
  /// random number.
  static void SetUpTestCase()
  {
    signal(SIGSEGV, signal_handler);

    _next_key = std::rand();
  }

  /// Clear all the memcached and Astaire instances. This calls their
  /// destructors which will kill the underlying processes. Also remove the
  /// cluster_settings file.
  static void TearDownTestCase()
  {
    signal(SIGSEGV, SIG_DFL);

    _memcached_instances.clear();
    _astaire_instances.clear();
    _dnsmasq_instance.reset();

    if (remove("cluster_settings") != 0)
    {
      perror("remove cluster_settings");
    }
  }

  /// Create a new store and a new unique key for this test. Also make sure that
  /// all of the memcached and Astaire instances are running before starting the
  /// new test. Any killed instances should be restarted at the end of the
  /// previous test, and the new test will assume this.
  virtual void SetUp()
  {
    _dns_client = new DnsCachedResolver("127.0.0.1", 5353);
    _resolver = new AstaireResolver(_dns_client, AF_INET);
    _store = new TopologyNeutralMemcachedStore("astaire.local", _resolver);

    // Create a new key for every test (to prevent tests from interacting with
    // each other).
    _key = std::to_string(_next_key++);

    // Ensure all our instances are running.
    EXPECT_TRUE(wait_for_instances());
  }

  virtual void TearDown()
  {
    delete _store; _store = NULL;
    delete _resolver; _resolver = NULL;
    delete _dns_client; _dns_client = NULL;
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
      std::string ip = "127.0.0." + std::to_string(ii + 1);
      _astaire_instances.emplace_back(new AstaireInstance(ip, ASTAIRE_PORT));
      _astaire_instances.back()->start_instance();
    }
  }

  /// Creates and starts up a dnsmasq instance to allow the store to find
  /// Astaire instances.
  static void create_and_start_dns_for_astaire(
    const std::vector<std::shared_ptr<AstaireInstance>>& astaires)
  {
    std::vector<std::string> hosts;

    for(std::vector<std::shared_ptr<AstaireInstance>>::const_iterator instance = astaires.begin();
        instance != astaires.end();
        ++instance)
    {
      hosts.push_back((*instance)->ip());
    }

    _dnsmasq_instance = std::shared_ptr<DnsmasqInstance>(
      new DnsmasqInstance("127.0.0.1", 5353, {{"astaire.local", hosts}}));
    _dnsmasq_instance->start_instance();
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

    if (_dnsmasq_instance)
    {
      _dnsmasq_instance->wait_for_instance();

      if (!success)
      {
        return success;
      }
    }

    return success;
  }

  /// Helper method for setting data in memcached for the test's default key.
  Store::Status set_data(std::string& data, uint64_t cas, int expiry = 60)
  {
    return set_data(_key, data, cas, expiry);
  }

  /// Helper method for setting data in memcached for a specified key.
  Store::Status set_data(const std::string& key,
                         const std::string& data,
                         uint64_t cas,
                         int expiry = 60)
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

  DnsCachedResolver* _dns_client;
  AstaireResolver* _resolver;
  TopologyNeutralMemcachedStore* _store;

  /// Use shared pointers for managing the instances so that the memory gets
  /// freed when the vector is cleared.
  static std::vector<std::shared_ptr<MemcachedInstance>> _memcached_instances;
  static std::vector<std::shared_ptr<AstaireInstance>> _astaire_instances;
  static std::shared_ptr<DnsmasqInstance> _dnsmasq_instance;

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
std::shared_ptr<DnsmasqInstance> BaseMemcachedSolutionTest::_dnsmasq_instance;

unsigned int BaseMemcachedSolutionTest::_next_key;
const std::string BaseMemcachedSolutionTest::_table = "test_table";

/// Clear all the memcached and Astaire instances. This calls their
/// destructors which will kill the underlying processes. Also remove the
/// cluster_settings file.
void signal_handler(int sig)
{
  signal(SIGSEGV, SIG_DFL);

  BaseMemcachedSolutionTest::_memcached_instances.clear();
  BaseMemcachedSolutionTest::_astaire_instances.clear();
  BaseMemcachedSolutionTest::_dnsmasq_instance.reset();

  if (remove("cluster_settings") != 0)
  {
    perror("remove cluster_settings");
  }
}

/// A test fixture that can be parameterized over different "scenarios". This
/// allows different deployment topologies and different failure modes.
template <class T>
class ParameterizedMemcachedSolutionTest : public BaseMemcachedSolutionTest
{
  static void SetUpTestCase()
  {
    create_and_start_memcached_instances(T::num_memcached_instances());
    create_and_start_astaire_instances(T::num_astaire_instances());
    create_and_start_dns_for_astaire(_astaire_instances);

    BaseMemcachedSolutionTest::SetUpTestCase();
  }
};

/// Useful pre-canned scenarios.

/// Scenario in which everything is fine and dandy.
class NoFailuresScenario
{
  static int num_memcached_instances() { return 2; }
  static int num_astaire_instances() { return 2; }
  static void trigger_failure(BaseMemcachedSolutionTest* fixture) {}
  static void fix_failure(BaseMemcachedSolutionTest* fixture) {}
};

/// Scenario in which a memcached instance fails and does not restart.
class MemcachedFailsScenario
{
  static int num_memcached_instances() { return 2; }
  static int num_astaire_instances() { return 2; }

  static void trigger_failure(BaseMemcachedSolutionTest* fixture)
  {
    EXPECT_TRUE(fixture->_memcached_instances.back()->kill_instance());
  }

  static void fix_failure(BaseMemcachedSolutionTest* fixture)
  {
    EXPECT_TRUE(fixture->_memcached_instances.back()->start_instance());
    EXPECT_TRUE(fixture->_memcached_instances.back()->wait_for_instance());
  }
};

/// Scenario in which a memcached instance restarts.
class MemcachedRestartsScenario
{
  static int num_memcached_instances() { return 2; }
  static int num_astaire_instances() { return 2; }

  static void trigger_failure(BaseMemcachedSolutionTest* fixture)
  {
    EXPECT_TRUE(fixture->_memcached_instances.back()->restart_instance());
    EXPECT_TRUE(fixture->_memcached_instances.back()->wait_for_instance());
  }

  static void fix_failure(BaseMemcachedSolutionTest* fixture)
  {
  }
};

/// Scenario in which a Astaire instance fails and does not restart.
class AstaireFailsScenario
{
  static int num_memcached_instances() { return 2; }
  static int num_astaire_instances() { return 2; }

  static void trigger_failure(BaseMemcachedSolutionTest* fixture)
  {
    EXPECT_TRUE(fixture->_astaire_instances.back()->kill_instance());
  }

  static void fix_failure(BaseMemcachedSolutionTest* fixture)
  {
    EXPECT_TRUE(fixture->_astaire_instances.back()->start_instance());
    EXPECT_TRUE(fixture->_astaire_instances.back()->wait_for_instance());
  }
};

/// Scenario in which a Astaire instance fails and does not restart.
class AstaireRestartsScenario
{
  static int num_memcached_instances() { return 2; }
  static int num_astaire_instances() { return 2; }

  static void trigger_failure(BaseMemcachedSolutionTest* fixture)
  {
    EXPECT_TRUE(fixture->_astaire_instances.back()->restart_instance());
    EXPECT_TRUE(fixture->_astaire_instances.back()->wait_for_instance());
  }

  static void fix_failure(BaseMemcachedSolutionTest* fixture)
  {
  }
};

////////////////////////////////////////////////////////////////////////////////
///
/// SimpleMemcachedSolutionTest testcases start here.
///
////////////////////////////////////////////////////////////////////////////////

/// Test fixture that sets up 2 Astaires and 2 memcacheds.
class SimpleMemcachedSolutionTest : public BaseMemcachedSolutionTest
{
  static void SetUpTestCase()
  {
    create_and_start_memcached_instances(2);
    create_and_start_astaire_instances(2);
    create_and_start_dns_for_astaire(_astaire_instances);

    BaseMemcachedSolutionTest::SetUpTestCase();
  }
};

/// Add a key and retrieve it.
TEST_F(SimpleMemcachedSolutionTest, AddGet)
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
TEST_F(SimpleMemcachedSolutionTest, AddGetTwoKeys)
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
TEST_F(SimpleMemcachedSolutionTest, AddGetExpire)
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
TEST_F(SimpleMemcachedSolutionTest, AddSetSetDataContentionSet)
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
/// to update it with an expiry of 0. The update fails due to data contention.
/// Try it again and check the key has gone.
TEST_F(SimpleMemcachedSolutionTest, AddSetCASDeleteDataContentionCASDelete)
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

  // Check that the data has been deleted.
  //
  // We have to sleep a bit here as replications to the non-primary memcacheds
  // is asynchronous so can race against the GET we are about to perform.
  usleep(10000);
  rc = this->get_data(data_out, cas);
  EXPECT_EQ(Store::Status::NOT_FOUND, rc);
}

/// Add a key twice. The second one fails due to data contention.
TEST_F(SimpleMemcachedSolutionTest, AddAddDataContention)
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
TEST_F(SimpleMemcachedSolutionTest, AddDelete)
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
TEST_F(SimpleMemcachedSolutionTest, Delete)
{
  Store::Status rc;
  rc = this->delete_data();
  EXPECT_EQ(Store::Status::OK, rc);
}

/// Add a key and delete it. Add it again.
TEST_F(SimpleMemcachedSolutionTest, AddDeleteAdd)
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
TEST_F(SimpleMemcachedSolutionTest, AddDeleteSetDataContention)
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

TEST_F(SimpleMemcachedSolutionTest, ConnectUsingIpAddress)
{
  delete _store; _store = NULL;
  _store = new TopologyNeutralMemcachedStore("127.0.0.1", _resolver);

  uint64_t cas = 0;
  Store::Status rc;
  std::string data_in = "SimpleMemcachedSolutionTest.AddGet";
  std::string data_out;

  rc = this->set_data(data_in, cas);
  EXPECT_EQ(Store::Status::OK, rc);

  rc = this->get_data(data_out, cas);
  EXPECT_EQ(Store::Status::OK, rc);
  EXPECT_EQ(data_out, data_in);

  data_in = "SimpleMemcachedSolutionTest.AddGet_1";
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

TEST_F(SimpleMemcachedSolutionTest, BadDomainName)
{
  delete _store; _store = NULL;
  _store = new TopologyNeutralMemcachedStore("bad.domain.name", _resolver);

  uint64_t cas = 0;
  Store::Status rc;
  std::string data_in = "SimpleMemcachedSolutionTest.AddGet";
  std::string data_out;

  rc = this->set_data(data_in, cas);
  EXPECT_EQ(Store::Status::ERROR, rc);

  rc = this->get_data(data_out, cas);
  EXPECT_EQ(Store::Status::ERROR, rc);

  rc = this->delete_data();
  EXPECT_EQ(Store::Status::ERROR, rc);
}

TEST_F(SimpleMemcachedSolutionTest, DomainAndPort)
{
  delete _store; _store = NULL;
  _store = new TopologyNeutralMemcachedStore("astaire.local:11311", _resolver);

  uint64_t cas = 0;
  Store::Status rc;
  std::string data_in = "SimpleMemcachedSolutionTest.AddGet";
  std::string data_out;

  rc = this->set_data(data_in, cas);
  EXPECT_EQ(Store::Status::OK, rc);

  rc = this->get_data(data_out, cas);
  EXPECT_EQ(Store::Status::OK, rc);
  EXPECT_EQ(data_out, data_in);

  data_in = "SimpleMemcachedSolutionTest.AddGet_1";
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

////////////////////////////////////////////////////////////////////////////////
///
/// MemcachedSolutionFailureTest testcases start here.
///
////////////////////////////////////////////////////////////////////////////////

/// An additional scenario in which a single Astaire instance is created, which
/// can be restarted.
class LoneAstaireRestartsScenario : public AstaireRestartsScenario
{
  static int num_astaire_instances() { return 1; }
};

/// Define a new test fixture as a simple subclass of the parameterized test.
/// This allows us to define exactly what scenarios we want to run for this
/// fixture.
template<class T>
class MemcachedSolutionFailureTest : public ParameterizedMemcachedSolutionTest<T> {};

typedef ::testing::Types<
  MemcachedFailsScenario,
  MemcachedRestartsScenario,
  AstaireFailsScenario,
  AstaireRestartsScenario,
  LoneAstaireRestartsScenario
> FailureScenarios;

TYPED_TEST_CASE(MemcachedSolutionFailureTest, FailureScenarios);

/// Kill a memcached instance. Add a key and retrieve it.
TYPED_TEST(MemcachedSolutionFailureTest, KillAddGet)
{
  TypeParam::trigger_failure(this);

  uint64_t cas = 0;
  Store::Status rc;
  std::string data_in = "MemcachedSolutionFailureTest.KillAddGet";
  std::string data_out;

  rc = this->set_data(data_in, cas);
  EXPECT_EQ(Store::Status::OK, rc);

  rc = this->get_data(data_out, cas);
  EXPECT_EQ(Store::Status::OK, rc);
  EXPECT_EQ(data_out, data_in);

  TypeParam::fix_failure(this);
}

/// Add a key. Kill a memcached instance. Retrieve the key.
TYPED_TEST(MemcachedSolutionFailureTest, AddKillGet)
{
  uint64_t cas = 0;
  Store::Status rc;
  std::string data_in = "MemcachedSolutionFailureTest.AddKillGet";
  std::string data_out;

  rc = this->set_data(data_in, cas);
  EXPECT_EQ(Store::Status::OK, rc);

  TypeParam::trigger_failure(this);

  rc = this->get_data(data_out, cas);
  EXPECT_EQ(Store::Status::OK, rc);
  EXPECT_EQ(data_out, data_in);

  TypeParam::fix_failure(this);
}

/// Add a key. Kill a memcached instance. Try retrieve the key after it should
/// have expired.
TYPED_TEST(MemcachedSolutionFailureTest, AddKillGetExpire)
{
  uint64_t cas = 0;
  Store::Status rc;
  std::string data_in = "MemcachedSolutionFailureTest.AddKillGetExpire";
  std::string data_out;

  rc = this->set_data(data_in, cas, 1);
  EXPECT_EQ(Store::Status::OK, rc);

  TypeParam::trigger_failure(this);

  sleep(2);

  rc = this->get_data(data_out, cas);
  EXPECT_EQ(Store::Status::NOT_FOUND, rc);

  TypeParam::fix_failure(this);
}

/// Add a key and retrieve it. Kill a memcached instance. Update the key. This
/// sometimes result in data contention depending on whether the primary or
/// backup memcached for this key has been killed. If it does, retrieve the key
/// again and update the key. This will work. If the first update worked, do
/// another update and check this fails due to data contention.
///
/// Repeat this test 10 times so that we sometimes kill the primary and
/// sometimes the backup.
///
/// TODO: This test case contains a few CAS loops. We should never need them but
/// only do because of https://github.com/Metaswitch/cpp-common/issues/286. This
/// testcase should be updated once that is fixed. However, this is actually not
/// that bad since from the client's point of view this could easily be business
/// as usual.
///
/// Why does cpp-common issue 286 result in data contention here? In the case
/// where we restart a memcached, and it's the primary memcached, the GET to the
/// primary memcached still fails with a CONNECTION FAILURE (even though the
/// memcached is back up) and this can happen multiple times (since we have
/// multiple Astaire processes).
TYPED_TEST(MemcachedSolutionFailureTest, AddKillSetSetDataContentionSet)
{
  for (int ii = 0; ii < 10; ++ii)
  {
    int cas_loop_count = 0;
    this->get_new_key();

    SCOPED_TRACE(this->_key);

    uint64_t cas = 0;
    Store::Status rc;
    std::string data_in = "MemcachedSolutionFailureTest.AddKillSetSetDataContentionSet";
    std::string data_out;

    rc = this->set_data(data_in, cas);
    EXPECT_EQ(Store::Status::OK, rc);

    rc = this->get_data(data_out, cas);
    EXPECT_EQ(Store::Status::OK, rc);
    EXPECT_EQ(data_out, data_in);

    TypeParam::trigger_failure(this);

    data_in = "MemcachedSolutionFailureTest.AddKillSetSetDataContentionSet_New1";
    rc = this->set_data(data_in, cas);
    EXPECT_TRUE((rc == Store::Status::DATA_CONTENTION) ||
                (rc == Store::Status::OK));

    cas_loop_count = 0;
    while ((rc == Store::Status::DATA_CONTENTION) && (cas_loop_count < 5))
    {
      rc = this->get_data(data_out, cas);
      EXPECT_EQ(Store::Status::OK, rc);
      EXPECT_NE(data_out, data_in);

      rc = this->set_data(data_in, cas);
      EXPECT_TRUE((rc == Store::Status::DATA_CONTENTION) ||
                  (rc == Store::Status::OK));
    }
    EXPECT_LT(cas_loop_count, 5);

    std::string failed_data_in = "FAIL";
    rc = this->set_data(failed_data_in, cas);
    EXPECT_EQ(Store::Status::DATA_CONTENTION, rc);

    rc = this->get_data(data_out, cas);
    EXPECT_EQ(Store::Status::OK, rc);
    EXPECT_EQ(data_out, data_in);

    data_in = "MemcachedSolutionFailureTest.AddKillSetSetDataContentionSet_New2";
    rc = this->set_data(data_in, cas);
    EXPECT_TRUE((rc == Store::Status::DATA_CONTENTION) ||
                (rc == Store::Status::OK));

    cas_loop_count = 0;
    while ((rc == Store::Status::DATA_CONTENTION) && (cas_loop_count < 5))
    {
      rc = this->get_data(data_out, cas);
      EXPECT_EQ(Store::Status::OK, rc);
      EXPECT_NE(data_out, data_in);

      rc = this->set_data(data_in, cas);
      EXPECT_EQ(Store::Status::OK, rc);
    }
    EXPECT_LT(cas_loop_count, 5);

    rc = this->get_data(data_out, cas);
    EXPECT_EQ(Store::Status::OK, rc);
    EXPECT_EQ(data_out, data_in);

    TypeParam::fix_failure(this);
    usleep(50000);

    // Bounce the store to prevent the failures in this loop iteration from
    // affecting the next one. Although the test fixture tears down the store
    // when it is destroyed, all of the iterations take place in the same
    // fixture.
    delete this->_store; this->_store = NULL;
    this->_store = new TopologyNeutralMemcachedStore("astaire.local",
                                                     this->_resolver);
  }
}

/// Add a key and delete it. Kill a memcached instance. Try to retrieve the key.
TYPED_TEST(MemcachedSolutionFailureTest, AddDeleteKill)
{
  uint64_t cas = 0;
  Store::Status rc;
  std::string data_in = "MemcachedSolutionFailureTest.AddDeleteKill";
  std::string data_out;

  rc = this->set_data(data_in, cas);
  EXPECT_EQ(Store::Status::OK, rc);

  rc = this->get_data(data_out, cas);
  EXPECT_EQ(Store::Status::OK, rc);
  EXPECT_EQ(data_out, data_in);

  rc = this->delete_data();
  EXPECT_EQ(Store::Status::OK, rc);

  TypeParam::trigger_failure(this);

  rc = this->get_data(data_out, cas);
  EXPECT_EQ(Store::Status::NOT_FOUND, rc);

  TypeParam::fix_failure(this);
}

/// Add a key. Kill a memcached instance. Retrieve the key and update it with an
/// expiry of 0. This sometimes result in data contention depending on whether
/// the primary or backup memcached for this key has been killed. If it does,
/// retrieve the key again and update the key with an expiry of 0. Check that
/// the key has gone.
TYPED_TEST(MemcachedSolutionFailureTest, AddKillCASDelete)
{
  uint64_t cas = 0;
  Store::Status rc;
  std::string data_in = "MemcachedSolutionFailureTest.AddKillCASDelete";
  std::string data_out;

  rc = this->set_data(data_in, cas);
  EXPECT_EQ(Store::Status::OK, rc);

  TypeParam::trigger_failure(this);

  rc = this->get_data(data_out, cas);
  EXPECT_EQ(Store::Status::OK, rc);
  EXPECT_EQ(data_out, data_in);

  data_in = "DELETE";
  rc = this->set_data(data_in, cas, 0);

  // TODO: We should never receive DATA_CONTENTION here - we only do so because
  // of https://github.com/Metaswitch/cpp-common/issues/286. This testcase
  // should be updated once that is fixed. However, this is actually not that
  // bad since from the client's point of view this could easily be business as
  // usual.
  //
  // Why does cpp-common issue 286 result in data contention here? In the case
  // where we restart a memcached, and it's the primary memcached, the GET to
  // the primary memcached still fails with a CONNECTION FAILURE (even though
  // the memcached is back up). However, the SET would now work fine, apart from
  // the fact that we're using the wrong CAS - hence the data contention.
  EXPECT_TRUE((rc == Store::Status::DATA_CONTENTION) ||
              (rc == Store::Status::OK));

  if (rc == Store::Status::DATA_CONTENTION)
  {
    rc = this->get_data(data_out, cas);
    EXPECT_EQ(Store::Status::OK, rc);

    rc = this->set_data(data_in, cas, 0);
    EXPECT_EQ(Store::Status::OK, rc);
  }

  rc = this->get_data(data_out, cas);
  EXPECT_EQ(Store::Status::NOT_FOUND, rc);

  TypeParam::fix_failure(this);
}

////////////////////////////////////////////////////////////////////////////////
///
/// LargerClustersMemcachedSolutionTest testcases start here.
///
/// This is not an exhaustive set of testcases and is intended as a kick of the
/// tires.
///
////////////////////////////////////////////////////////////////////////////////

/// Define a new test fixture as a simple subclass of the parameterized test.
/// This allows us to define exactly what scenarios we want to run for this
/// fixture.
template <class T>
class LargerClustersMemcachedSolutionTest : public ParameterizedMemcachedSolutionTest<T> {};

/// Scenario in which a memcached instance fails and does not restart.
class LargeClusterMemcachedFails : public MemcachedFailsScenario
{
  static int num_memcached_instances() { return 3; }
  static int num_astaire_instances() { return 3; }
};

/// Scenario in which a memcached instance fails and does not restart.
class LargeClusterMemcachedRestarts : public MemcachedRestartsScenario
{
  static int num_memcached_instances() { return 3; }
  static int num_astaire_instances() { return 3; }
};

typedef ::testing::Types<
  LargeClusterMemcachedFails,
  LargeClusterMemcachedRestarts
> LargeClusterScenarios;

TYPED_TEST_CASE(LargerClustersMemcachedSolutionTest, LargeClusterScenarios);

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

  TypeParam::trigger_failure(this);

  rc = this->get_data(data_out, cas);
  EXPECT_EQ(Store::Status::OK, rc);
  EXPECT_EQ(data_out, data_in);

  TypeParam::fix_failure(this);
}

/// Add a key. Kill a memcached instance. Get the key and update it. This
/// sometimes result in data contention depending on whether the primary or
/// backup memcached for this key has been killed. If it does, retrieve the key
/// again and update the key with an expiry of 0. Check that the key has gone.
TYPED_TEST(LargerClustersMemcachedSolutionTest, AddKillGetSet)
{
  uint64_t cas = 0;
  Store::Status rc;
  std::string data_in = "LargerClustersMemcachedSolutionTest.AddKillGetSet";
  std::string data_out;

  rc = this->set_data(data_in, cas);
  EXPECT_EQ(Store::Status::OK, rc);

  TypeParam::trigger_failure(this);

  rc = this->get_data(data_out, cas);
  EXPECT_EQ(Store::Status::OK, rc);
  EXPECT_EQ(data_out, data_in);

  data_in = "LargerClustersMemcachedSolutionTest.AddKillGetSet_New";
  rc = this->set_data(data_in, cas);

  // TODO: We should never receive DATA_CONTENTION here - we only do so because
  // of https://github.com/Metaswitch/cpp-common/issues/286. This testcase
  // should be updated once that is fixed. However, this is actually not that
  // bad since from the client's point of view this could easily be business as
  // usual.
  //
  // Why does cpp-common issue 286 result in data contention here? In the case
  // where we restart a memcached, and it's the primary memcached, the GET to
  // the primary memcached still fails with a CONNECTION FAILURE (even though
  // the memcached is back up). However, the SET would now work fine, apart from
  // the fact that we're using the wrong CAS - hence the data contention.
  EXPECT_TRUE((rc == Store::Status::DATA_CONTENTION) ||
              (rc == Store::Status::OK));

  if (rc == Store::Status::DATA_CONTENTION)
  {
    rc = this->get_data(data_out, cas);
    EXPECT_EQ(Store::Status::OK, rc);

    rc = this->set_data(data_in, cas);
    EXPECT_EQ(Store::Status::OK, rc);
  }

  rc = this->get_data(data_out, cas);
  EXPECT_EQ(Store::Status::OK, rc);
  EXPECT_EQ(data_out, data_in);

  TypeParam::fix_failure(this);
}


///////////////////////////////////////////////////////////////////////////////
///
/// MemcachedSolutionThrashTest tests.
///
///////////////////////////////////////////////////////////////////////////////

template <class T>
class MemcachedSolutionThrashTest : public ParameterizedMemcachedSolutionTest<T> {};

const static int NUM_INCR_PER_KEY_PER_THREAD = 10;

// We would like to run the thrash test in various failure scenarios, but we
// lose consistency in these cases due to different nodes disagreeing on which
// memcached is the primary they should be CASing against.
typedef ::testing::Types<
  // MemcachedFailsScenario,
  // MemcachedRestartsScenario,
  // AstaireFailsScenario,
  // AstaireRestartsScenario,
  NoFailuresScenario
> ThrashTestScenarios;

TYPED_TEST_CASE(MemcachedSolutionThrashTest, ThrashTestScenarios);

void thrash_thread_fn(TopologyNeutralMemcachedStore* store,
                      std::string table,
                      std::vector<std::string> keys)
{
  Store::Status rc;

  for (int i = 0; i < NUM_INCR_PER_KEY_PER_THREAD; ++i)
  {
    SCOPED_TRACE("Increment " + std::to_string(i));

    for(std::vector<std::string>::iterator key = keys.begin();
        key != keys.end();
        ++key)
    {
      SCOPED_TRACE("Key " + *key);

      do
      {
        std::string data;
        uint64_t cas;

        rc = store->get_data(table, *key, data, cas, DUMMY_TRAIL_ID);
        EXPECT_EQ(rc, Store::Status::OK);

        int value = atoi(data.c_str());
        value++;

        rc = store->set_data(table,
                             *key,
                             std::to_string(value),
                             cas,
                             300,
                             DUMMY_TRAIL_ID);
        EXPECT_TRUE((rc == Store::Status::OK) ||
                    (rc == Store::Status::DATA_CONTENTION));

      } while (rc == Store::Status::DATA_CONTENTION);
    }
  }
}

// The thrash tests works as follows:
//
// * Set 10 keys to have the value "0".
// * Spawn 10 thrash threads. Each thread increments each key 10 times.
// * The main thread waits for the thrash threads to complete.
// * It then checks that the value of each key is 100.
TYPED_TEST(MemcachedSolutionThrashTest, ThrashTest)
{
  Store::Status rc;
  std::vector<std::string> keys;
  std::vector<std::thread> threads;

  for (int i = 0; i < 10; ++i)
  {
    SCOPED_TRACE(this->_key);
    keys.push_back(this->_key);
    rc = this->set_data(this->_key, "0", 0);
    EXPECT_EQ(rc, Store::Status::OK);
    this->get_new_key();
  }

  for (int i = 0; i < 10; ++i)
  {
    threads.push_back(std::thread(thrash_thread_fn,
                                  this->_store,
                                  this->_table,
                                  keys));
  }

  for (int i = 0; i < 10; ++i)
  {
    threads[i].join();
  }

  // the purpose of this sleep is to allow the connections in the store to
  // become idle so that we hit the code that cleans them up. This isn't really
  // testing the API (as we need to know the connection timeout), but at least
  // we don't place any extra constraints on the API.
  sleep(61);

  for (int i = 0; i < 10; ++i)
  {
    SCOPED_TRACE(keys[i]);

    uint64_t cas;
    std::string data_out;
    int expected_value = NUM_INCR_PER_KEY_PER_THREAD * threads.size();

    rc = this->get_data(keys[i], data_out, cas);
    EXPECT_EQ(rc, Store::Status::OK);
    int actual_value = atoi(data_out.c_str());

    EXPECT_EQ(expected_value, actual_value);
  }
}

