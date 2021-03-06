/**
 * @file site.h Helper class for spinning up all the processes in a site.
 *
 * Copyright (C) Metaswitch Networks 2018
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */

#ifndef SITE_H__
#define SITE_H__

#include <string>
#include <memory>
#include <vector>

/// Class controlling the processes running in a site.
class Site
{
public:
  /// A struct describing the externally visibly topology of the site - i.e.
  /// what services are present and what domain names they are accessible at.
  struct Topology
  {
    std::string ip_addr_prefix;
    std::string chronos_domain;
    std::string rogers_domain;
    std::string dns_ip;
    int dns_port;

    /// Constructor.
    ///
    /// @param [in] ip_addr_prefix - The first three octets of the site's IP
    /// address range, in the form "x.y.z.".
    ///
    /// Each site is given an IPv4 /24 address range to use. All processes
    /// created in the site will listen on addresses in that subnet. This is
    /// useful for two reasons:
    ///   -  It makes IP address management easier, since the IP addresses used
    ///      by different sites cannot clash.
    ///   -  It makes it possible to simulate adverse network condition between
    ///      the sites, by using IP tables rules to modify the traffic between
    ///      IP addresses in different address ranges.
    Topology(const std::string& ip_addr_prefix);

    /// Set the chronos domain name.
    ///
    /// @param [in] domain The chronos domain name/
    ///
    /// @return This topology instance, so that the method can be used with the
    ///         builder pattern.
    Topology& with_chronos(const std::string& domain);

    /// Set the rogers domain name.
    ///
    /// @param [in] domain The rogers domain name/
    ///
    /// @return This topology instance, so that the method can be used with the
    ///         builder pattern.
    Topology& with_rogers(const std::string& domain);
  };

  /// Constructor
  ///
  /// @param [in] index The (typically 1-based) index of the site. This must be
  ///                   unique across all sites.
  /// @param [in] name  The name of this site. Must be unique across all sites.
  /// @param [in] dir   A directory that the site may create and use to store
  ///                   any temporary files it requires.
  /// @param [in] deployment_topology A mapping of site index to the topology of
  ///                   that site. This is used to cluster GR databases
  ///                   together.
  Site(int index,
       const std::string& site_name,
       const std::string& dir,
       std::map<std::string, Topology> deployment_topology = {},
       int num_memcached = 0,
       int num_rogers = 0,
       int num_chronos = 0);
  virtual ~Site();

  /// Get a list of the IP addresses of all the chronos processes.
  std::vector<std::string> get_chronos_ips();

  /// Get a list of the IP addresses of all the rogers processes.
  std::vector<std::string> get_rogers_ips();

  /// Returns a pointer to the first chronos instance in this site.
  std::shared_ptr<ChronosInstance> get_first_chronos();

  /// Returns a pointer to the first rogers instance in this site.
  std::shared_ptr<RogersInstance> get_first_rogers();

  /// Returns a pointer to the first memcached instance in this site.
  std::shared_ptr<MemcachedInstance> get_first_memcached();

  /// Start all processes in the site.
  ///
  /// @warning This does not wait for the instances to come up. This is so that
  /// multiple sites and/or other processes can be started in parallel. Call
  /// wait_for_instances before using the site.
  void start();

  /// Restart all processes in the site.
  ///
  /// @warning This does not wait for the instances to come up. This is so that
  /// multiple sites and/or other processes can be started in parallel. Call
  /// wait_for_instances before using the site.
  void restart();

  /// Stop all processes in the site.
  void kill();

  /// Wait for all instances in the site to be started.
  ///
  /// @return Whether the processes have all started successfully.
  bool wait_for_instances();

private:

  /// Helper function to create the specified number of memcached instances.
  /// @param [in] count - The number of instances to create.
  void create_memcached_instances(int count);

  /// Helper function to create the specified number of rogers instances.
  /// @param [in] count - The number of instances to create.
  void create_rogers_instances(int count);

  /// Helper function to create the specified number of chronos instances.
  /// @param [in] count - The number of instances to create.
  void create_chronos_instances(int count);

  /// Utility function that does the same thing to each process in the site.
  ///
  /// @param [in] fn - A function that will be called on each process instance
  ///                  in the site.
  void for_each_instance(std::function<void(std::shared_ptr<ProcessInstance>)> fn);

  /// Utility function to get at one of the site IPs.
  std::string site_ip(int index);

  /// Index of this site. Each site must have a different index.
  int _site_index;

  /// The name of this site.
  std::string _site_name;

  /// Directory this site may use for storing config files, log files, etc.
  std::string _site_dir;

  /// IP address prefix of this site, in the form "x.y.z.". Note the trailing
  /// dot.
  std::string _ip_addr_prefix;

  /// The topology of all the sites in the deployment.
  std::map<std::string, Topology> _deployment_topology;

  /// Use shared pointers for managing the instances so that the memory gets
  /// freed when the vector is cleared.
  std::vector<std::shared_ptr<MemcachedInstance>> _memcached_instances;
  std::vector<std::shared_ptr<RogersInstance>> _rogers_instances;
  std::vector<std::shared_ptr<ChronosInstance>> _chronos_instances;
};

#endif


