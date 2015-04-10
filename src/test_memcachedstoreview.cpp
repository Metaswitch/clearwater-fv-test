/**
 * @file test_memcachedstoreview.cpp Tests for the Memcached Store View class.
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
#include "gmock/gmock.h"

#include "memcachedstoreview.h"

using ::testing::AnyOf;
using ::testing::Contains;

// Function that expects a valid replica list.
//
// @param - The list to check.
// @param - The expected length of the list.
// @param - A matcher that should match every element of the list.  This will
//          typically be a std::string (if the caller expects a list
//          containing a single server) or an AnyOf matcher (listing the
//          possible replicas).
template<class M>
void expect_replica_list(MemcachedStoreView::ReplicaList replicas,
                         size_t count,
                         M matcher)
{
  // Check the count.
  EXPECT_EQ(replicas.size(), count);

  // Check each entry is unique. Do this by sorting the list, unique-ing it and
  // checking the length has not changed.
  std::sort(replicas.begin(), replicas.end());
  std::unique(replicas.begin(), replicas.end());
  EXPECT_EQ(replicas.size(), count);

  // Check each entry of the list is allowed.
  for (size_t i = 0; i < count; ++i)
  {
    EXPECT_THAT(replicas[i], matcher);
  }
}

#define EXPECT_REPLICA_LIST(LIST, COUNT, MATCHER)                              \
{                                                                              \
  SCOPED_TRACE("Checking replica list");                                       \
  expect_replica_list((LIST), (COUNT), (MATCHER));                             \
}

//
// Base fixture for all our tests.
//
const int NUM_VBUCKETS = 128;
const int NUM_REPLICAS = 2;

class MemcachedStoreViewTest : public ::testing::Test
{
  MemcachedStoreViewTest() : _view(NUM_VBUCKETS, NUM_REPLICAS) {}
  MemcachedStoreView _view;
};

//
// Fixture that sets up a single server.
//
class SingleServerTest : public MemcachedStoreViewTest
{
  void SetUp()
  {
    MemcachedConfig cfg;
    cfg.servers.push_back("localhost:40001");
    _view.update(cfg);
  }
};

TEST_F(SingleServerTest, OneCurrentReplica)
{
  std::map<int, MemcachedStoreView::ReplicaList> replicas = _view.current_replicas();
  for (int i = 0; i < NUM_VBUCKETS; ++i)
  {
    EXPECT_REPLICA_LIST(replicas[i], 1u, "localhost:40001");
  }
}

TEST_F(SingleServerTest, NoNewReplicas)
{
  EXPECT_TRUE(_view.new_replicas().empty());
}

TEST_F(SingleServerTest, MovesEmpty)
{
  EXPECT_TRUE(_view.calculate_vbucket_moves().empty());
}

//
// Fixture that sets up several current servers, but no new ones.
//
class MultiServerTest : public MemcachedStoreViewTest
{
  void SetUp()
  {
    MemcachedConfig cfg;
    cfg.servers.push_back("localhost:40001");
    cfg.servers.push_back("localhost:40002");
    cfg.servers.push_back("localhost:40003");
    _view.update(cfg);
  }
};

TEST_F(MultiServerTest, TwoCurrentReplicas)
{
  std::map<int, MemcachedStoreView::ReplicaList> replicas = _view.current_replicas();
  for (int i = 0; i < NUM_VBUCKETS; ++i)
  {
    EXPECT_REPLICA_LIST(replicas[i], 2u, AnyOf("localhost:40001",
                                               "localhost:40002",
                                               "localhost:40003"));
  }
}

TEST_F(MultiServerTest, NoNewReplicas)
{
  EXPECT_TRUE(_view.new_replicas().empty());
}

TEST_F(MultiServerTest, MovesEmpty)
{
  EXPECT_TRUE(_view.calculate_vbucket_moves().empty());
}


//
// Fixture that sets up several current servers and several new ones.
//
class ScaleUpTest : public MemcachedStoreViewTest
{
  void SetUp()
  {
    MemcachedConfig cfg;
    cfg.servers.push_back("localhost:40001");
    cfg.servers.push_back("localhost:40002");
    cfg.new_servers.push_back("localhost:40001");
    cfg.new_servers.push_back("localhost:40002");
    cfg.new_servers.push_back("localhost:40003");
    _view.update(cfg);
  }
};

TEST_F(ScaleUpTest, CurrentReplicasDontHaveNewServer)
{
  std::map<int, MemcachedStoreView::ReplicaList> replicas = _view.current_replicas();
  for (int i = 0; i < NUM_VBUCKETS; ++i)
  {
    EXPECT_REPLICA_LIST(replicas[i], 2u, AnyOf("localhost:40001", "localhost:40002"));
  }
}

TEST_F(ScaleUpTest, NewReplicasFilledIn)
{
  std::map<int, MemcachedStoreView::ReplicaList> replicas = _view.new_replicas();
  for (int i = 0; i < NUM_VBUCKETS; ++i)
  {
    EXPECT_REPLICA_LIST(replicas[i], 2u, AnyOf("localhost:40001",
                                               "localhost:40002",
                                               "localhost:40003"));
  }
}

TEST_F(ScaleUpTest, MovesNotEmpty)
{
  EXPECT_FALSE(_view.calculate_vbucket_moves().empty());
}

TEST_F(ScaleUpTest, MovesAgreeWithReplicaLists)
{
  std::map<int, MemcachedStoreView::ReplicaList> curr_replicas = _view.current_replicas();
  std::map<int, MemcachedStoreView::ReplicaList> new_replicas = _view.new_replicas();
  std::map<int, MemcachedStoreView::ReplicaChange> moves = _view.calculate_vbucket_moves();

  for (int i = 0; i < NUM_VBUCKETS; ++i)
  {
    if (moves.find(i) != moves.end())
    {
      EXPECT_EQ(moves[i].first, curr_replicas[i]);
      EXPECT_EQ(moves[i].second, new_replicas[i]);
    }
    else
    {
      MemcachedStoreView::ReplicaList old_sorted = curr_replicas[i];
      MemcachedStoreView::ReplicaList new_sorted = new_replicas[i];
      std::sort(old_sorted.begin(), old_sorted.end());
      std::sort(new_sorted.begin(), new_sorted.end());
      EXPECT_EQ(old_sorted, new_sorted);
    }
  }
}

TEST_F(ScaleUpTest, WriteListContainsAllCurrents)
{
  std::map<int, MemcachedStoreView::ReplicaList> curr_replicas = _view.current_replicas();

  for (int i = 0; i < NUM_VBUCKETS; ++i)
  {
    std::vector<std::string> write_list = _view.write_replicas(i);
    EXPECT_THAT(write_list, Contains(curr_replicas[i][0]));
    EXPECT_THAT(write_list, Contains(curr_replicas[i][1]));
  }
}
