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

static const SAS::TrailId DUMMY_TRAIL_ID = 0x12345678;

class MemcachedFixture : public ::testing::Test
{
  void SetUp()
  {
    _store = new MemcachedStore(false, "./cluster_settings");

    std::string options("--CONNECT-TIMEOUT=10 --SUPPORT-CAS");
    _memcached_client = memcached(options.c_str(), options.length());
    memcached_behavior_set(_memcached_client,
                           MEMCACHED_BEHAVIOR_CONNECT_TIMEOUT,
                           50);
    memcached_server_add(_memcached_client, "127.0.0.1", 55555);
  }

  void TearDown()
  {
    delete _store; _store = NULL;
    memcached_free(_memcached_client); _memcached_client = NULL;
  }

  MemcachedStore* _store;
  memcached_st* _memcached_client;
};

TEST_F(MemcachedFixture, ExampleTest)
{
}
