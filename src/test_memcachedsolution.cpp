/**
 * @file test_memcachedsolution.cpp FV tests for Clearwater's memcached
 * solution.
 *
 * Copyright (C) Metaswitch Networks 2017
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
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
static const int ROGERS_PORT = 11311;

void signal_handler(int signal);

// Fixture for all memcached solution tests.
//
// This fixture:
// - Creates a unique key and an instance of TopologyNeutralMemcachedStore for
//   each test.
// - Tidies up instances of memcached and Rogers and any config files between
//   sets of tests.
// - Provides helper methods for memcached operations and managing instances of
//   memcached and Rogers
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

  /// Clear all the memcached and Rogers instances. This calls their
  /// destructors which will kill the underlying processes. Also remove the
  /// cluster_settings file.
  static void TearDownTestCase()
  {
    signal(SIGSEGV, SIG_DFL);

    _memcached_instances.clear();
    _rogers_instances.clear();
    _dnsmasq_instance.reset();

    if (remove("cluster_settings") != 0)
    {
      perror("remove cluster_settings");
    }
  }

  /// Create a new store and a new unique key for this test. Also make sure that
  /// all of the memcached and Rogers instances are running before starting the
  /// new test. Any killed instances should be restarted at the end of the
  /// previous test, and the new test will assume this.
  virtual void SetUp()
  {
    _dns_client = new DnsCachedResolver("127.0.0.1", 5353);
    _resolver = new AstaireResolver(_dns_client, AF_INET);
    _store = new TopologyNeutralMemcachedStore("rogers.local", _resolver, true);

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

  /// Creates and starts up the specified number of ogers instances. We
  /// currently only support one Rogers instance (since the port Rogers
  /// listens on is not currently configurable).
  static void create_and_start_rogers_instances(int rogers_instances)
  {
    for (int ii = 0; ii < rogers_instances; ++ii)
    {
      std::string ip = "127.0.0." + std::to_string(ii + 1);
      _rogers_instances.emplace_back(new RogersInstance(ip, ROGERS_PORT));
      _rogers_instances.back()->start_instance();
    }
  }

  /// Creates and starts up a dnsmasq instance to allow the store to find
  /// Rogers instances.
  static void create_and_start_dns_for_rogers(
    const std::vector<std::shared_ptr<RogersInstance>>& rogers)
  {
    std::vector<std::string> hosts;

    for(const std::shared_ptr<RogersInstance>& instance : rogers)
    {
      hosts.push_back(instance->ip());
    }

    _dnsmasq_instance = std::shared_ptr<DnsmasqInstance>(
      new DnsmasqInstance("127.0.0.1", 5353, {{"rogers.local", hosts}}));
    _dnsmasq_instance->start_instance();
  }

  /// Wait for all existing memcached and Rogers instances to come up by
  /// checking they're listening on the correct ports. Returns false if any of
  /// the instances fail to come up.
  static bool wait_for_instances()
  {
    bool success = true;

    for (const std::shared_ptr<MemcachedInstance>& inst : _memcached_instances)
    {
      success = inst->wait_for_instance();

      if (!success)
      {
        return success;
      }
    }

    for (const std::shared_ptr<RogersInstance>& inst : _rogers_instances)
    {
      success = inst->wait_for_instance();

      if (!success)
      {
        return success;
      }
    }

    if (_dnsmasq_instance)
    {
      success = _dnsmasq_instance->wait_for_instance();

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
  static std::vector<std::shared_ptr<RogersInstance>> _rogers_instances;
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
std::vector<std::shared_ptr<RogersInstance>> BaseMemcachedSolutionTest::_rogers_instances;
std::shared_ptr<DnsmasqInstance> BaseMemcachedSolutionTest::_dnsmasq_instance;

unsigned int BaseMemcachedSolutionTest::_next_key;
const std::string BaseMemcachedSolutionTest::_table = "test_table";

/// Clear all the memcached and Rogers instances. This calls their
/// destructors which will kill the underlying processes. Also remove the
/// cluster_settings file.
void signal_handler(int sig)
{
  signal(SIGSEGV, SIG_DFL);

  BaseMemcachedSolutionTest::_memcached_instances.clear();
  BaseMemcachedSolutionTest::_rogers_instances.clear();
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
    create_and_start_rogers_instances(T::num_rogers_instances());
    create_and_start_dns_for_rogers(_rogers_instances);

    BaseMemcachedSolutionTest::SetUpTestCase();
  }
};

/// Useful pre-canned scenarios.

/// Scenario in which everything is fine and dandy.
class NoFailuresScenario
{
  static int num_memcached_instances() { return 2; }
  static int num_rogers_instances() { return 2; }
  static void trigger_failure(BaseMemcachedSolutionTest* fixture) {}
  static void fix_failure(BaseMemcachedSolutionTest* fixture) {}
};

/// Scenario in which a memcached instance fails and does not restart.
class MemcachedFailsScenario
{
  static int num_memcached_instances() { return 2; }
  static int num_rogers_instances() { return 2; }

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
  static int num_rogers_instances() { return 2; }

  static void trigger_failure(BaseMemcachedSolutionTest* fixture)
  {
    EXPECT_TRUE(fixture->_memcached_instances.back()->restart_instance());
    EXPECT_TRUE(fixture->_memcached_instances.back()->wait_for_instance());
  }

  static void fix_failure(BaseMemcachedSolutionTest* fixture)
  {
  }
};

/// Scenario in which a Rogers instance fails and does not restart.
class RogersFailsScenario
{
  static int num_memcached_instances() { return 2; }
  static int num_rogers_instances() { return 2; }

  static void trigger_failure(BaseMemcachedSolutionTest* fixture)
  {
    EXPECT_TRUE(fixture->_rogers_instances.back()->kill_instance());
  }

  static void fix_failure(BaseMemcachedSolutionTest* fixture)
  {
    EXPECT_TRUE(fixture->_rogers_instances.back()->start_instance());
    EXPECT_TRUE(fixture->_rogers_instances.back()->wait_for_instance());
  }
};

/// Scenario in which a Rogers instance fails and does not restart.
class RogersRestartsScenario
{
  static int num_memcached_instances() { return 2; }
  static int num_rogers_instances() { return 2; }

  static void trigger_failure(BaseMemcachedSolutionTest* fixture)
  {
    EXPECT_TRUE(fixture->_rogers_instances.back()->restart_instance());
    EXPECT_TRUE(fixture->_rogers_instances.back()->wait_for_instance());
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

/// Test fixture that sets up 2 Rogerss and 2 memcacheds.
class SimpleMemcachedSolutionTest : public BaseMemcachedSolutionTest
{
  static void SetUpTestCase()
  {
    create_and_start_memcached_instances(2);
    create_and_start_rogers_instances(2);
    create_and_start_dns_for_rogers(_rogers_instances);

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
  _store = new TopologyNeutralMemcachedStore("127.0.0.1", _resolver, true);

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
  _store = new TopologyNeutralMemcachedStore("bad.domain.name", _resolver, true);

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
  _store = new TopologyNeutralMemcachedStore("rogers.local:11311", _resolver, true);

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

/// An additional scenario in which a single Rogers instance is created, which
/// can be restarted.
class LoneRogersRestartsScenario : public RogersRestartsScenario
{
  static int num_rogers_instances() { return 1; }
};

/// Define a new test fixture as a simple subclass of the parameterized test.
/// This allows us to define exactly what scenarios we want to run for this
/// fixture.
template<class T>
class MemcachedSolutionFailureTest : public ParameterizedMemcachedSolutionTest<T> {};

typedef ::testing::Types<
  MemcachedFailsScenario,
  MemcachedRestartsScenario,
  RogersFailsScenario,
  RogersRestartsScenario,
  LoneRogersRestartsScenario
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
TYPED_TEST(MemcachedSolutionFailureTest, AddKillSetSetDataContentionSet)
{
  for (int ii = 0; ii < 10; ++ii)
  {
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

    if (rc == Store::Status::DATA_CONTENTION)
    {
      rc = this->get_data(data_out, cas);
      EXPECT_EQ(Store::Status::OK, rc);
      EXPECT_NE(data_out, data_in);

      rc = this->set_data(data_in, cas);
      EXPECT_EQ(rc, Store::Status::OK);
    }

    std::string failed_data_in = "FAIL";
    rc = this->set_data(failed_data_in, cas);
    EXPECT_EQ(Store::Status::DATA_CONTENTION, rc);

    rc = this->get_data(data_out, cas);
    EXPECT_EQ(Store::Status::OK, rc);
    EXPECT_EQ(data_out, data_in);

    data_in = "MemcachedSolutionFailureTest.AddKillSetSetDataContentionSet_New2";
    rc = this->set_data(data_in, cas);
    EXPECT_EQ(rc, Store::Status::OK);

    rc = this->get_data(data_out, cas);
    EXPECT_EQ(Store::Status::OK, rc);
    EXPECT_EQ(data_out, data_in);

    TypeParam::fix_failure(this);

    // Bounce the store to prevent the failures in this loop iteration from
    // affecting the next one. Although the test fixture tears down the store
    // when it is destroyed, all of the iterations take place in the same
    // fixture.
    delete this->_store; this->_store = NULL;
    this->_store = new TopologyNeutralMemcachedStore("rogers.local",
                                                     this->_resolver,
                                                     true);
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

  // "Delete" the data by writing a record with a TTL of 0.
  data_in = "DELETE";
  rc = this->set_data(data_in, cas, 0);
  EXPECT_EQ(Store::Status::OK, rc);

  // Allow the "delete" to percolate to all nodes. If we don't sleep the next
  // GET can beat the SET that has been sent to the backup, meaning that we
  // actually see some data being returned.
  usleep(5000);

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
  static int num_rogers_instances() { return 3; }
};

/// Scenario in which a memcached instance fails and does not restart.
class LargeClusterMemcachedRestarts : public MemcachedRestartsScenario
{
  static int num_memcached_instances() { return 3; }
  static int num_rogers_instances() { return 3; }
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
  EXPECT_EQ(rc, Store::Status::OK);

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
  // RogersFailsScenario,
  // RogersRestartsScenario,
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

