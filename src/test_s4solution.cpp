/**
 * @file test_s4solution.cpp FV tests for Clearwater's S4 component.
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
#include "site.h"

#include "httpstack.h"
#include "httpstack_utils.h"
#include "memcachedstore.h"
#include "astaire_aor_store.h"
#include "s4.h"
#include "s4_chronoshandlers.h"
#include "mock_timer_pop_consumer.h"

#include <vector>
#include <iostream>
#include <fstream>
#include <stdio.h>
#include <thread>
#include <boost/filesystem.hpp>
#include <stddef.h>
#include <signal.h>

SAS::TrailId FAKE_SAS_TRAIL_ID = 0x12345678;

using ::testing::_;
using ::testing::StrictMock;

/// Class containing everything needed for an "S4-site".
///
/// As S4 is currently a class rather than a microservice, each client actually
/// needs multiple S4 instances:
/// -  One S4 instance for each remote site, that only talks to databases in
///    that site.
/// -  One S4 instance for the local site that talks to databases in the local
///    site, and "remote S4s" referred to in the previous bullet.
///
/// This class encapsulates all of this logic so that the test code can create
/// all the necessary instances easily.
class S4Site
{
public:
  /// The local S4 instance. This is the one and only S4 that the tests should
  /// access, so it is a public member (while the other S4 instances are
  /// private).
  S4* s4;

  /// A mock object that receives timer pops from S4.
  StrictMock<MockTimerPopConsumer> timer_sink;

  /// Constructor
  ///
  /// @param [in] site_name           - The name of the local database site that
  ///                                   this S4 site talks to.
  /// @param [in] deployment_topology - The topology of the entire deployment.
  S4Site(const std::string& site_name, std::map<std::string, Site::Topology> deployment_topology)
  {
    // Create a DNS server, http_resolver and astaire_resolver.
    _dns_client = new DnsCachedResolver("127.0.0.1",
                                        DnsCachedResolver::DEFAULT_TIMEOUT,
                                        DnsCachedResolver::NO_DNS_FILE,
                                        5353);
    _http_resolver = new HttpResolver(_dns_client,
                                      AF_INET,
                                      HttpResolver::DEFAULT_BLACKLIST_DURATION);
    _astaire_resolver = new AstaireResolver(_dns_client, AF_INET);

    // Work out what our local IP address should be.
    Site::Topology& this_site = deployment_topology.at(site_name);
    std::string ip_addr = this_site.ip_addr_prefix + "1";

    // Create all the remote S4s with their associated stores.
    for (const std::pair<std::string, Site::Topology>& item: deployment_topology)
    {
      if (item.first != site_name)
      {
        _remote_stores.push_back(
          new TopologyNeutralMemcachedStore(item.second.rogers_domain,
                                            _astaire_resolver,
                                            true,
                                            nullptr,
                                            ip_addr));
        _remote_aor_stores.push_back(new AstaireAoRStore(_remote_stores.back()));
        _remote_s4s.push_back(new S4(site_name + "-remote-s4-to-" + item.first,
                                     _remote_aor_stores.back()));
      }
    }

    // Now create the local S4 and associated stores.
    _store = new TopologyNeutralMemcachedStore(this_site.rogers_domain,
                                               _astaire_resolver,
                                               false,
                                               nullptr,
                                               ip_addr);
    _aor_store = new AstaireAoRStore(_store);
    _chronos_connection = new ChronosConnection(this_site.chronos_domain + ":7253",
                                                ip_addr + ":8080",
                                                _http_resolver,
                                                nullptr,
                                                ip_addr);
    s4 = new S4(site_name + "-local-s4",
                _chronos_connection,
                "/timers",
                _aor_store,
                _remote_s4s);

    try
    {
      // Create an HTTP stack with no exception handler. This means the tests
      // will crash if they ever hit a signal, but this is probably what we want
      // anyway.
      _http_stack = new HttpStack(1, nullptr);
      _http_stack->initialize();

      // Register a handler to bind S4 to the HTTP stack.
      _s4_handler_config = new ChronosAoRTimeoutTask::Config(s4);
      _s4_handler = new HttpStackUtils::SpawningHandler<
        ChronosAoRTimeoutTask, ChronosAoRTimeoutTask::Config>(_s4_handler_config);
      _http_stack->register_handler("^/timers$", _s4_handler);

      _http_stack->bind_tcp_socket(ip_addr, 8080);
      _http_stack->start(nullptr);
      _http_stack->initialize();
    }
    catch (HttpStack::Exception& e)
    {
      EXPECT_TRUE(false);
      exit(1);
    }

    // Register S4 with the timer sink.
    s4->register_timer_pop_consumer(&timer_sink);
  }

  /// Destructor.
  virtual ~S4Site()
  {
    // Free everything off.
    for (TopologyNeutralMemcachedStore* s : _remote_stores) { delete s; }
    for (AoRStore* s : _remote_aor_stores) { delete s; }
    for (S4* s : _remote_s4s) { delete s; }
    delete s4; s4 = nullptr;
    delete _chronos_connection; _chronos_connection = nullptr;
    delete _aor_store; _aor_store = nullptr;
    delete _store; _store = nullptr;
    delete _astaire_resolver; _astaire_resolver = nullptr;
    delete _http_resolver; _http_resolver = nullptr;
    delete _dns_client; _dns_client = nullptr;

    try
    {
      _http_stack->stop();
      _http_stack->wait_stopped();
      delete _http_stack; _http_stack = nullptr;
    }
    catch (HttpStack::Exception& e)
    {
      EXPECT_TRUE(false);
      exit(2);
    }

    delete _s4_handler; _s4_handler = nullptr;
    delete _s4_handler_config; _s4_handler_config = nullptr;
  }

private:
  HttpResolver* _http_resolver;
  AstaireResolver* _astaire_resolver;
  DnsCachedResolver* _dns_client;

  TopologyNeutralMemcachedStore* _store;
  AoRStore* _aor_store;
  ChronosConnection* _chronos_connection;
  HttpStack* _http_stack;
  HttpStackUtils::SpawningHandler<
    ChronosAoRTimeoutTask, ChronosAoRTimeoutTask::Config>* _s4_handler;
  ChronosAoRTimeoutTask::Config* _s4_handler_config;

  std::vector<TopologyNeutralMemcachedStore*> _remote_stores;
  std::vector<AoRStore*> _remote_aor_stores;
  std::vector<S4*> _remote_s4s;
};

/// Fixture for all S4 solution tests.
class BaseS4SolutionTest : public ::testing::Test
{
public:
  static void signal_handler(int signal);

  static void SetUpTestCase()
  {
    signal(SIGSEGV, signal_handler);
    signal(SIGINT, signal_handler);

    // Create a directory to store the various config files that we are going to
    // need.
    boost::filesystem::create_directory("tmp");

    _deployment_topology.emplace("site1",
                                 Site::Topology("127.0.1.")
                                  .with_chronos("chronos.site1")
                                  .with_rogers("rogers.site1"));
    _deployment_topology.emplace("site2",
                                 Site::Topology("127.0.2.")
                                  .with_chronos("chronos.site2")
                                  .with_rogers("rogers.site2"));
  }

  static void TearDownTestCase()
  {
    _dnsmasq_instance.reset();
    _site1.reset();
    _site2.reset();

    boost::filesystem::remove_all("tmp");

    signal(SIGSEGV, SIG_DFL);
    signal(SIGINT, SIG_DFL);
  }

  virtual void SetUp()
  {
    _s4_site1 = new S4Site("site1", _deployment_topology);
    _s4_site2 = new S4Site("site2", _deployment_topology);

    // Ensure all our instances are running.
    EXPECT_TRUE(wait_for_instances());
  }

  virtual void TearDown()
  {
    delete _s4_site1; _s4_site1 = nullptr;
    delete _s4_site2; _s4_site2 = nullptr;
  }

  static void create_and_start_sites()
  {
    _site1 = std::shared_ptr<Site>(new Site(1,
                                            "site1",
                                            "tmp/site1",
                                            _deployment_topology,
                                            2,
                                            2,
                                            2));
    _site1->start();
    TRC_DEBUG("Started site1");

    _site2 = std::shared_ptr<Site>(new Site(2,
                                            "site2",
                                            "tmp/site2",
                                            _deployment_topology,
                                            2,
                                            2,
                                            2));
    _site2->start();
    TRC_DEBUG("Started site2");
  }

  /// Creates and starts up a dnsmasq instance to allow S4 to find remote
  /// processes.
  static void create_and_start_dns(const std::map<std::string, std::vector<std::string>>& a_records)
  {
    _dnsmasq_instance = std::shared_ptr<DnsmasqInstance>(
      new DnsmasqInstance("127.0.0.1", 5353, a_records));
    _dnsmasq_instance->start_instance();
  }

  /// Wait for all existing memcached and Rogers instances to come up by
  /// checking they're listening on the correct ports. Returns false if any of
  /// the instances fail to come up.
  static bool wait_for_instances()
  {
    if (!_site1->wait_for_instances())
    {
      return false;
    }

    if (!_site2->wait_for_instances())
    {
      return false;
    }

    if (_dnsmasq_instance && !_dnsmasq_instance->wait_for_instance())
    {
      return false;
    }

    return true;
  }

  S4Site* _s4_site1;
  S4Site* _s4_site2;

  /// Use shared pointers for managing the instances so that the memory gets
  /// freed when the vector is cleared.
  static std::shared_ptr<DnsmasqInstance> _dnsmasq_instance;
  static std::map<std::string, Site::Topology> _deployment_topology;
  static std::shared_ptr<Site> _site1;
  static std::shared_ptr<Site> _site2;
};

std::shared_ptr<DnsmasqInstance> BaseS4SolutionTest::_dnsmasq_instance;
std::map<std::string, Site::Topology> BaseS4SolutionTest::_deployment_topology;
std::shared_ptr<Site> BaseS4SolutionTest::_site1;
std::shared_ptr<Site> BaseS4SolutionTest::_site2;

/// Clear all the memcached and Rogers instances. This calls their
/// destructors which will kill the underlying processes. Also remove the
/// cluster_settings file.
void BaseS4SolutionTest::signal_handler(int sig)
{
  // Clean up the testcase.
  TearDownTestCase();

  // Re-raise to signal to cause the script to exit.
  raise(sig);
}

////////////////////////////////////////////////////////////////////////////////
///
/// SimpleS4SolutionTest testcases start here.
///
////////////////////////////////////////////////////////////////////////////////

/// Test fixture that sets up 2 Rogers and 2 memcacheds.
class SimpleS4SolutionTest : public BaseS4SolutionTest
{
public:
  static void SetUpTestCase()
  {
    BaseS4SolutionTest::SetUpTestCase();

    create_and_start_sites();

    // Generate the DNS records for rogers and chronos, and start dnsmasq to
    // serve these.
    std::map<std::string, std::vector<std::string>> a_records;
    a_records[_deployment_topology.at("site1").rogers_domain] = _site1->get_rogers_ips();
    a_records[_deployment_topology.at("site1").chronos_domain] = _site1->get_chronos_ips();
    a_records[_deployment_topology.at("site2").rogers_domain] = _site2->get_rogers_ips();
    a_records[_deployment_topology.at("site2").chronos_domain] = _site2->get_chronos_ips();

    create_and_start_dns(a_records);
  }

  static void TearDownTestCase()
  {
    BaseS4SolutionTest::TearDownTestCase();
  }
};

/// Add a key let it time out.
TEST_F(SimpleS4SolutionTest, TimerTracerBullet)
{
  const std::string impu = "sip:kermit@muppets.com";

  // PUT a binding to site 1.
  AoR* aor = new AoR(impu);
  Binding* b = new Binding(impu);
  b->_expires = time(nullptr) + 3;
  aor->_bindings[impu] = b;

  _s4_site1->s4->handle_put(impu, *aor, FAKE_SAS_TRAIL_ID);
  delete aor; aor = nullptr;

  EXPECT_CALL(_s4_site1->timer_sink, handle_timer_pop(impu, _));
  sleep(5);

  printf("Test complete\n");
  sleep(3600);
}

/// Add a key and retrieve it.
TEST_F(SimpleS4SolutionTest, TracerBullet)
{
  const std::string impu = "sip:kermit@muppets.com";

  // PUT a binding to site 1.
  AoR* aor = new AoR(impu);
  Binding* b = new Binding(impu);
  b->_expires = time(nullptr) + 3600;
  aor->_bindings[impu] = b;

  _s4_site1->s4->handle_put(impu, *aor, FAKE_SAS_TRAIL_ID);
  delete aor; aor = nullptr;

  // Kill site 1.
  _site1->kill();
  delete _s4_site1; _s4_site1 = nullptr;

  // Get from site 2. This should work as S4 has replicated the PUT to the
  // remote site.
  uint64_t cas;
  HTTPCode status = _s4_site2->s4->handle_get("sip:kermit@muppets.com",
                                              &aor,
                                              cas,
                                              FAKE_SAS_TRAIL_ID);
  EXPECT_EQ(status, HTTP_OK);
  EXPECT_NE(aor, nullptr);
  delete aor; aor = nullptr;
}

