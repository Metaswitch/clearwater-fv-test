/**
 * @file test_memcached_config.cpp Test reading a memcached config file.
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

#include <fstream>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "memcached_config.h"

class MemcachedConfigTest : public ::testing::Test
{
public:
  char _filename[64];
  int _fd;

  MemcachedConfigReader* _reader;

  virtual void SetUp()
  {
    // Create a temporary file to write the config to.
    strcpy(_filename, "./cluster_settings.XXXXXX");
    _fd = mkstemp(_filename);
    ASSERT_GT(_fd, 0)
      << "Got error " << _fd << " (" << strerror(errno) << ")";

    _reader = new MemcachedConfigFileReader(std::string(_filename));
  }

  virtual void TearDown()
  {
    delete _reader; _reader = NULL;
    close(_fd);
    unlink(_filename);
  }

  void write_config(const std::string cfg)
  {
    size_t num_bytes = ::write(_fd, cfg.c_str(), cfg.length());
    ASSERT_EQ(num_bytes, cfg.length())
      << "Got error " << num_bytes << " (" << strerror(errno) << ")";
  }
};

TEST_F(MemcachedConfigTest, AllSettings)
{
  write_config("servers=192.168.0.1:11211,192.168.0.2:11211\n"
               "new_servers=10.0.0.1:11211\n");

  MemcachedConfig config;
  EXPECT_TRUE(_reader->read_config(config));
  EXPECT_EQ(config.servers.size(), 2u);
  EXPECT_EQ(config.servers[0], "192.168.0.1:11211");
  EXPECT_EQ(config.servers[1], "192.168.0.2:11211");
  EXPECT_EQ(config.new_servers.size(), 1u);
  EXPECT_EQ(config.new_servers[0], "10.0.0.1:11211");
  EXPECT_EQ(config.tombstone_lifetime, 1800);
}


TEST_F(MemcachedConfigTest, OptionalFieldsEmpty)
{
  write_config("servers=192.168.0.1:11211,192.168.0.2:11211");

  MemcachedConfig config;
  EXPECT_TRUE(_reader->read_config(config));
  EXPECT_EQ(config.servers.size(), 2u);
  EXPECT_EQ(config.servers[0], "192.168.0.1:11211");
  EXPECT_EQ(config.servers[1], "192.168.0.2:11211");
  // new_servers defaults to an empty vector.
  EXPECT_EQ(config.new_servers.size(), 0u);
  EXPECT_EQ(config.tombstone_lifetime, 1800);
}


TEST_F(MemcachedConfigTest, TombstoneLifetime)
{
  write_config("servers=192.168.0.1:11211,192.168.0.2:11211\n"
               "new_servers=10.0.0.1:11211\n"
               "tombstone_lifetime=200");

  MemcachedConfig config;
  // We no longer support tombstone_lifetime as a valid setting in
  // cluster_settings, so this file should be rejected.
  EXPECT_FALSE(_reader->read_config(config));
}


TEST_F(MemcachedConfigTest, EmptyConfig)
{
  write_config("");

  MemcachedConfig config;
  EXPECT_FALSE(_reader->read_config(config));
}


TEST_F(MemcachedConfigTest, CorruptConfig)
{
  write_config("qwerty");

  MemcachedConfig config;
  EXPECT_FALSE(_reader->read_config(config));
}


TEST_F(MemcachedConfigTest, ServerListEmpty)
{
  // A blank server list should be valid and parseable - this is so that an empty
  // remote_cluster_settings file can be put in place, then updated without a
  // restart when GR config is learnt.
  write_config("servers=");

  MemcachedConfig config;
  EXPECT_TRUE(_reader->read_config(config));
}


TEST_F(MemcachedConfigTest, NoServerLine)
{
  write_config("new_servers=a:11211");

  MemcachedConfig config;
  EXPECT_FALSE(_reader->read_config(config));
}

TEST_F(MemcachedConfigTest, OnlyNewServers)
{
  write_config("servers=\nnew_servers=a:11211");

  MemcachedConfig config;
  EXPECT_TRUE(_reader->read_config(config));
  EXPECT_EQ(config.servers.size(), 0u);
  EXPECT_EQ(config.new_servers.size(), 1u);
}

TEST_F(MemcachedConfigTest, BothServerListsEmpty)
{
  // A blank server list should be valid and parseable - this is so that an empty
  // remote_cluster_settings file can be put in place, then updated without a
  // restart when GR config is learnt.
  write_config("servers=\nnew_servers=");

  MemcachedConfig config;
  EXPECT_TRUE(_reader->read_config(config));
}

TEST_F(MemcachedConfigTest, Comments)
{
  write_config("servers=192.168.0.1:11211\n"
               "#comment"); 

  MemcachedConfig config;
  EXPECT_TRUE(_reader->read_config(config));
}

TEST_F(MemcachedConfigTest, Whitespace)
{
  write_config(" # Comment\n"
               "servers=10.0.0.1:11211, 10.0.0.2:11211");

  MemcachedConfig config;
  EXPECT_TRUE(_reader->read_config(config));
  EXPECT_EQ(config.servers.size(), 2u);
  EXPECT_EQ(config.servers[0], "10.0.0.1:11211");
  EXPECT_EQ(config.servers[1], "10.0.0.2:11211");
}
