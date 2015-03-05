/**
 * @file memcachedstore_test.cpp FV tests for the memcached store.
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

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "memcachedstore.h"

// Helper macro that expects a "success" memcached return code, but prints out
// a more useful error message if this fails.
#define EXPECT_MEMCACHED_SUCCESS(RC, CLIENT)                                   \
  EXPECT_TRUE(memcached_success(RC))                                           \
    << "Return code was: " << (RC)                                             \
    << " (" << memcached_strerror(CLIENT, RC) << ")";

static const SAS::TrailId DUMMY_TRAIL_ID = 0x12345678;


int memcached_port()
{
  return atoi(getenv("MEMCACHED_PORT"));
}


// Fixture for all memcached tests.
//
// This fixture:
// - Creates a unique key for the test.
// - Connects to the memcached server using libmemcached, and provides utility
//   methods for using it.
class MemcachedTest : public ::testing::Test
{
  static void SetUpTestCase()
  {
    _next_key = std::rand();
  }

  virtual void SetUp()
  {
    // Create a new connection to memcached using libmemcached directly.
    std::string options("--CONNECT-TIMEOUT=10 --SUPPORT-CAS");
    _memcached_client = memcached(options.c_str(), options.length());
    memcached_behavior_set(_memcached_client,
                           MEMCACHED_BEHAVIOR_CONNECT_TIMEOUT,
                           50);
    memcached_server_add(_memcached_client, "127.0.0.1", memcached_port());

    // Create a new key for every test (to prevent tests from interacting with
    // each other).
    _key = std::to_string(_next_key++);
  }

  virtual void TearDown()
  {
    memcached_free(_memcached_client); _memcached_client = NULL;
  }

  memcached_st* _memcached_client;

  // Tests that use this fixture use a monotonically incrementing numerical key
  // (so that tests are isolated from each other). This variable stores the
  // next key to use.
  static unsigned int _next_key;

  // The table and key to use for the test. The table is the same in all tests.
  const static std::string _table;
  std::string _key;

  //
  // Utility methods that perform memcached commands using libmemcached
  // directly (but provide a slightly easier-to-use API).
  //

  // Calculate the fully qualified key for a record, using the same mechanism
  // as MemcachedStore.
  std::string fqkey()
  {
    return _table + "\\\\" + _key;
  }

  memcached_return_t simple_add(const std::string fqkey,
                                const std::string data,
                                uint64_t expiry = 300)
  {
    return memcached_add(_memcached_client,
                         fqkey.c_str(),
                         fqkey.length(),
                         data.c_str(),
                         data.length(),
                         expiry,
                         0);
  }

  memcached_return_t simple_set(const std::string fqkey,
                                const std::string data,
                                uint64_t expiry = 300)
  {
    return memcached_set(_memcached_client,
                         fqkey.c_str(),
                         fqkey.length(),
                         data.c_str(),
                         data.length(),
                         expiry,
                         0);
  }

  memcached_return_t simple_delete(const std::string fqkey)
  {
    return memcached_delete(_memcached_client, fqkey.c_str(), fqkey.length(), 0);
  }

  memcached_return_t simple_get(const std::string fqkey,
                                std::string& data,
                                uint64_t& cas)
  {
    const char* key = fqkey.c_str();
    const size_t key_len = fqkey.length();

    memcached_return_t rc;
    rc = memcached_mget(_memcached_client, &key, &key_len, 1);

    if (memcached_success(rc))
    {
      memcached_result_st result;
      memcached_result_create(_memcached_client, &result);
      memcached_fetch_result(_memcached_client, &result, &rc);

      data.assign(memcached_result_value(&result),
                  memcached_result_length(&result));
      memcached_result_free(&result);
    }

    return rc;
  }
};

unsigned int MemcachedTest::_next_key;
const std::string MemcachedTest::_table = "test_table";


// An class to get MemcachedStore's config from an in-memory structure
class StaticConfigReader : public MemcachedConfigReader
{
public:
  bool read_config(MemcachedConfig& config)
  {
    config = _cfg;
    return true;
  }

  MemcachedConfig _cfg;
};


// Config reader that configures MemcachedStore to use tombstones.
class TombstoneConfig : public StaticConfigReader
{
  TombstoneConfig() : StaticConfigReader()
  {
    _cfg.servers.push_back("127.0.0.1:" + std::to_string(memcached_port()));
    _cfg.tombstone_lifetime = 300;
  }
};


// Config reader that configures MemcachedStore to NOT use tombstones.
class NoTombstoneConfig : public StaticConfigReader
{
  NoTombstoneConfig() : StaticConfigReader()
  {
    _cfg.servers.push_back("127.0.0.1:" + std::to_string(memcached_port()));
    _cfg.tombstone_lifetime = 0;
  }
};

//
// Basic MemcachedStore tests.
//

// Test fixture that creates a single MemcachedStore that with a templated
// config reader. This allows the basic store tests to be run over several
// different configs using gtest typed tests (for more details see
// https://code.google.com/p/googletest/wiki/AdvancedGuide#Typed_Tests).
template<class T>
class SingleMemcachedStoreTest : public MemcachedTest
{
public:
  MemcachedStore* _store;

  virtual void SetUp()
  {
    MemcachedTest::SetUp();
    _store = new MemcachedStore(false, new T());
  }

  virtual void TearDown()
  {
    delete _store; _store = NULL;
    MemcachedTest::TearDown();
  }

  static void SetUpTestCase()
  {
    MemcachedTest::SetUpTestCase();
  }
};

typedef ::testing::Types<TombstoneConfig, NoTombstoneConfig> StoreConfigs;
TYPED_TEST_CASE(SingleMemcachedStoreTest, StoreConfigs);


TYPED_TEST(SingleMemcachedStoreTest, SetDeleteSequence)
{
  Store::Status status;
  const std::string data_in = "kermit";
  std::string data_out;
  uint64_t cas;

  status = this->_store->set_data(this->_table, this->_key, data_in, 0, 300, DUMMY_TRAIL_ID);
  EXPECT_EQ(status, Store::OK);

  status = this->_store->get_data(this->_table, this->_key, data_out, cas, DUMMY_TRAIL_ID);
  EXPECT_EQ(status, Store::OK);
  EXPECT_EQ(data_out, data_in);

  status = this->_store->delete_data(this->_table, this->_key, DUMMY_TRAIL_ID);
  EXPECT_EQ(status, Store::OK);

  status = this->_store->get_data(this->_table, this->_key, data_out, cas, DUMMY_TRAIL_ID);
  EXPECT_EQ(status, Store::NOT_FOUND);
}


TYPED_TEST(SingleMemcachedStoreTest, UpdateExistingData)
{
  Store::Status status;
  const std::string data_in1 = "kermit";
  const std::string data_in2 = "gonzo";
  std::string data_out;
  uint64_t cas;

  status = this->_store->set_data(this->_table, this->_key, data_in1, 0, 300, DUMMY_TRAIL_ID);
  EXPECT_EQ(status, Store::OK);

  status = this->_store->get_data(this->_table, this->_key, data_out, cas, DUMMY_TRAIL_ID);
  EXPECT_EQ(status, Store::OK);
  EXPECT_EQ(data_out, data_in1);

  status = this->_store->set_data(this->_table, this->_key, data_in2, cas, 300, DUMMY_TRAIL_ID);
  EXPECT_EQ(status, Store::OK);

  status = this->_store->get_data(this->_table, this->_key, data_out, cas, DUMMY_TRAIL_ID);
  EXPECT_EQ(status, Store::OK);
  EXPECT_EQ(data_out, data_in2);

  status = this->_store->delete_data(this->_table, this->_key, DUMMY_TRAIL_ID);
  EXPECT_EQ(status, Store::OK);
}


TYPED_TEST(SingleMemcachedStoreTest, UpdateWrongCas)
{
  Store::Status status;
  const std::string data_in1 = "kermit";
  const std::string data_in2 = "gonzo";
  std::string data_out;
  uint64_t cas;

  status = this->_store->set_data(this->_table, this->_key, data_in1, 0, 300, DUMMY_TRAIL_ID);
  EXPECT_EQ(status, Store::OK);

  status = this->_store->get_data(this->_table, this->_key, data_out, cas, DUMMY_TRAIL_ID);
  EXPECT_EQ(status, Store::OK);
  EXPECT_EQ(data_out, data_in1);

  status = this->_store->set_data(this->_table, this->_key, data_in2, (cas - 1), 300, DUMMY_TRAIL_ID);
  EXPECT_EQ(status, Store::DATA_CONTENTION);

  status = this->_store->get_data(this->_table, this->_key, data_out, cas, DUMMY_TRAIL_ID);
  EXPECT_EQ(status, Store::OK);
  EXPECT_EQ(data_out, data_in1);

  status = this->_store->delete_data(this->_table, this->_key, DUMMY_TRAIL_ID);
  EXPECT_EQ(status, Store::OK);
}


TYPED_TEST(SingleMemcachedStoreTest, SecondAddFails)
{
  Store::Status status;
  const std::string data_in1 = "kermit";
  const std::string data_in2 = "gonzo";
  std::string data_out;
  uint64_t cas;

  status = this->_store->set_data(this->_table, this->_key, data_in1, 0, 300, DUMMY_TRAIL_ID);
  EXPECT_EQ(status, Store::OK);

  status = this->_store->get_data(this->_table, this->_key, data_out, cas, DUMMY_TRAIL_ID);
  EXPECT_EQ(status, Store::OK);
  EXPECT_EQ(data_out, data_in1);

  status = this->_store->set_data(this->_table, this->_key, data_in2, 0, 300, DUMMY_TRAIL_ID);
  EXPECT_EQ(status, Store::DATA_CONTENTION);

  status = this->_store->get_data(this->_table, this->_key, data_out, cas, DUMMY_TRAIL_ID);
  EXPECT_EQ(status, Store::OK);
  EXPECT_EQ(data_out, data_in1);

  status = this->_store->delete_data(this->_table, this->_key, DUMMY_TRAIL_ID);
  EXPECT_EQ(status, Store::OK);
}


TYPED_TEST(SingleMemcachedStoreTest, KeyCanBeDeletedThenAdded)
{
  Store::Status status;
  const std::string data_in1 = "kermit";
  const std::string data_in2 = "gonzo";
  std::string data_out;
  uint64_t cas;

  status = this->_store->set_data(this->_table, this->_key, data_in1, 0, 300, DUMMY_TRAIL_ID);
  EXPECT_EQ(status, Store::OK);

  status = this->_store->delete_data(this->_table, this->_key, DUMMY_TRAIL_ID);
  EXPECT_EQ(status, Store::OK);

  status = this->_store->set_data(this->_table, this->_key, data_in2, 0, 300, DUMMY_TRAIL_ID);
  EXPECT_EQ(status, Store::OK);

  status = this->_store->get_data(this->_table, this->_key, data_out, cas, DUMMY_TRAIL_ID);
  EXPECT_EQ(status, Store::OK);
  EXPECT_EQ(data_out, data_in2);

  status = this->_store->delete_data(this->_table, this->_key, DUMMY_TRAIL_ID);
  EXPECT_EQ(status, Store::OK);
}

//
// MemcachedStore tests that only apply to when the store uses tombstone
// records.
//

class MemcachedStoreTombstoneTest : public MemcachedTest
{
public:
  MemcachedStore* _store;

  virtual void SetUp()
  {
    MemcachedTest::SetUp();
    _store = new MemcachedStore(false, new TombstoneConfig());
  }

  virtual void TearDown()
  {
    delete _store; _store = NULL;
    MemcachedTest::TearDown();
  }

  static void SetUpTestCase()
  {
    MemcachedTest::SetUpTestCase();
  }
};

TEST_F(MemcachedStoreTombstoneTest, TombstonesPreventSimpleAdds)
{
  Store::Status status;
  memcached_return_t rc;
  const std::string data_in1 = "kermit";
  const std::string data_in2 = "gonzo";
  std::string data_out;
  uint64_t cas;

  status = _store->set_data(_table, _key, data_in1, 0, 300, DUMMY_TRAIL_ID);
  EXPECT_EQ(status, Store::OK);

  status = _store->delete_data(_table, _key, DUMMY_TRAIL_ID);
  EXPECT_EQ(status, Store::OK);

  rc = simple_add(fqkey(), data_in2);
  EXPECT_EQ(MEMCACHED_NOTSTORED, rc);

  rc = simple_get(fqkey(), data_out, cas);
  EXPECT_MEMCACHED_SUCCESS(rc, _memcached_client);
  EXPECT_EQ(data_out, "");
}

//
// MemcachedStoreTests that use an uplevel store (on that uses tombstones) and
// a downlevel store (that does not).
//

class MemcachedStoreUpgradeTest : public MemcachedTest
{
public:
  MemcachedStore* _uplevel_store;
  MemcachedStore* _downlevel_store;

  virtual void SetUp()
  {
    MemcachedTest::SetUp();
    _uplevel_store = new MemcachedStore(false, new TombstoneConfig());
    _downlevel_store = new MemcachedStore(false, new NoTombstoneConfig());
  }

  virtual void TearDown()
  {
    delete _uplevel_store; _uplevel_store = NULL;
    delete _downlevel_store; _downlevel_store = NULL;
    MemcachedTest::TearDown();
  }

  static void SetUpTestCase()
  {
    MemcachedTest::SetUpTestCase();
  }
};

TEST_F(MemcachedStoreUpgradeTest, UplevelDeletesData)
{
  Store::Status status;
  const std::string data_in1 = "kermit";
  const std::string data_in2 = "gonzo";
  std::string data_out;
  uint64_t cas;

  status = _uplevel_store->set_data(_table, _key, data_in1, 0, 300, DUMMY_TRAIL_ID);
  EXPECT_EQ(status, Store::OK);

  status = _uplevel_store->delete_data(_table, _key, DUMMY_TRAIL_ID);
  EXPECT_EQ(status, Store::OK);

  status = _downlevel_store->get_data(_table, _key, data_out, cas, DUMMY_TRAIL_ID);
  EXPECT_EQ(status, Store::NOT_FOUND);

  status = _downlevel_store->set_data(_table, _key, data_in2, 0, 300, DUMMY_TRAIL_ID);
  EXPECT_EQ(status, Store::OK);

  status = _downlevel_store->get_data(_table, _key, data_out, cas, DUMMY_TRAIL_ID);
  EXPECT_EQ(status, Store::OK);
  EXPECT_EQ(data_out, data_in2);

  status = _downlevel_store->delete_data(_table, _key, DUMMY_TRAIL_ID);
  EXPECT_EQ(status, Store::OK);
}


TEST_F(MemcachedStoreUpgradeTest, DownlevelDeletesData)
{
  Store::Status status;
  const std::string data_in1 = "kermit";
  const std::string data_in2 = "gonzo";
  std::string data_out;
  uint64_t cas;

  status = _downlevel_store->set_data(_table, _key, data_in1, 0, 300, DUMMY_TRAIL_ID);
  EXPECT_EQ(status, Store::OK);

  status = _downlevel_store->delete_data(_table, _key, DUMMY_TRAIL_ID);
  EXPECT_EQ(status, Store::OK);

  status = _uplevel_store->get_data(_table, _key, data_out, cas, DUMMY_TRAIL_ID);
  EXPECT_EQ(status, Store::NOT_FOUND);

  status = _uplevel_store->set_data(_table, _key, data_in2, 0, 300, DUMMY_TRAIL_ID);
  EXPECT_EQ(status, Store::OK);

  status = _uplevel_store->get_data(_table, _key, data_out, cas, DUMMY_TRAIL_ID);
  EXPECT_EQ(status, Store::OK);
  EXPECT_EQ(data_out, data_in2);

  status = _uplevel_store->delete_data(_table, _key, DUMMY_TRAIL_ID);
  EXPECT_EQ(status, Store::OK);
}
