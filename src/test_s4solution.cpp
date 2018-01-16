/**
 * @file test_s5 FV tests for Clearwater's S4 component.
 *
 * Copyright (C) Metaswitch Networks 2018
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */

#include "gtest/gtest.h"

#include "processinstance.h"

#include <vector>
#include <iostream>
#include <fstream>
#include <stdio.h>
#include <thread>
#include <boost/filesystem.hpp>

/// TODO
///
/// A bunch of this code is copy-pasted from BAseMemcachedsolutionTest. Need to
/// refactor this.

static const int BASE_MEMCACHED_PORT = 33333;
static const int ROGERS_PORT = 11311;
static const int CHRONOS_PORT = 7253;

/// Fixture for all memcached solution tests.
class BaseS4SolutionTest : public ::testing::Test
{
public:
  static void signal_handler(int signal);

  /// TODO
  static void SetUpTestCase()
  {
    signal(SIGSEGV, signal_handler);

    // Create a directory to store the various config files that we are going to
    // need.
    boost::filesystem::create_directory("tmp");
  }

  /// TODO
  static void TearDownTestCase()
  {
    signal(SIGSEGV, SIG_DFL);

    _memcached_instances.clear();
    _rogers_instances.clear();
    _chronos_instances.clear();
    _dnsmasq_instance.reset();

    if (remove("cluster_settings") != 0)
    {
      perror("remove cluster_settings");
    }

    boost::filesystem::remove_all("tmp");
  }

  /// TODO
  virtual void SetUp()
  {
    // Ensure all our instances are running.
    EXPECT_TRUE(wait_for_instances());
  }

  virtual void TearDown()
  {
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

  /// Creates and starts up the specified number of Rogers instances.
  static void create_and_start_rogers_instances(int rogers_instances)
  {
    for (int ii = 0; ii < rogers_instances; ++ii)
    {
      std::string ip = "127.0.0." + std::to_string(ii + 1);
      _rogers_instances.emplace_back(new RogersInstance(ip, ROGERS_PORT));
      _rogers_instances.back()->start_instance();
    }
  }

  /// Creates and starts up the specified number of chronos instances.
  static void create_and_start_chronos_instances(int chronos_instances)
  {
    // Create directory to hold chronos config and logs.
    boost::filesystem::create_directory("tmp/chronos");

    // Create the chronos config that is common across all nodes in the cluster.
    std::string cluster_conf_file = "tmp/chronos/chronos_cluster.conf";
    std::ofstream conf;
    conf.open(cluster_conf_file);
    conf << "[cluster]\n";
    for (int ii = 0; ii < chronos_instances; ++ii)
    {
      conf << "node = 127.0.0." + std::to_string(ii + 1) + ":" + std::to_string(CHRONOS_PORT) + "\n";
    }
    conf.close();

    for (int ii = 0; ii < chronos_instances; ++ii)
    {
      std::string ip = "127.0.0." + std::to_string(ii + 1);
      std::string dir = "tmp/chronos/instance" + std::to_string(ii + 1);
      _chronos_instances.emplace_back(new ChronosInstance(ip,
                                                          CHRONOS_PORT,
                                                          dir,
                                                          cluster_conf_file));
      _chronos_instances.back()->start_instance();
    }
  }

  /// Creates and starts up a dnsmasq instance to allow S4 to find remote
  /// processes.
  static void create_and_start_dns(
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

    for (const std::shared_ptr<ChronosInstance>& inst : _chronos_instances)
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

  /// Use shared pointers for managing the instances so that the memory gets
  /// freed when the vector is cleared.
  static std::vector<std::shared_ptr<MemcachedInstance>> _memcached_instances;
  static std::vector<std::shared_ptr<RogersInstance>> _rogers_instances;
  static std::vector<std::shared_ptr<ChronosInstance>> _chronos_instances;
  static std::shared_ptr<DnsmasqInstance> _dnsmasq_instance;
};

std::vector<std::shared_ptr<MemcachedInstance>> BaseS4SolutionTest::_memcached_instances;
std::vector<std::shared_ptr<RogersInstance>> BaseS4SolutionTest::_rogers_instances;
std::vector<std::shared_ptr<ChronosInstance>> BaseS4SolutionTest::_chronos_instances;
std::shared_ptr<DnsmasqInstance> BaseS4SolutionTest::_dnsmasq_instance;

/// Clear all the memcached and Rogers instances. This calls their
/// destructors which will kill the underlying processes. Also remove the
/// cluster_settings file.
void BaseS4SolutionTest::signal_handler(int sig)
{
  signal(SIGSEGV, SIG_DFL);

  BaseS4SolutionTest::_memcached_instances.clear();
  BaseS4SolutionTest::_rogers_instances.clear();
  BaseS4SolutionTest::_dnsmasq_instance.reset();

  if (remove("cluster_settings") != 0)
  {
    perror("remove cluster_settings");
  }
}

////////////////////////////////////////////////////////////////////////////////
///
/// SimpleS4SolutionTest testcases start here.
///
////////////////////////////////////////////////////////////////////////////////

/// Test fixture that sets up 2 Rogerss and 2 memcacheds.
class SimpleS4SolutionTest : public BaseS4SolutionTest
{
  static void SetUpTestCase()
  {
    BaseS4SolutionTest::SetUpTestCase();

    create_and_start_memcached_instances(2);
    create_and_start_rogers_instances(2);
    create_and_start_chronos_instances(2);
    create_and_start_dns(_rogers_instances);
  }

  static void TearDownTestCase()
  {
    BaseS4SolutionTest::TearDownTestCase();
  }
};

/// Add a key and retrieve it.
TEST_F(SimpleS4SolutionTest, Mainline)
{
  sleep(86400); // 1 day
}