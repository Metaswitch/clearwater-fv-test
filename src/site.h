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

class Site
{
public:
  /// A struct describing the externally visibly topology of the site - i.e.
  /// what services are present and what domain names they are accessible at.
  struct Topology
  {
    std::string chronos_domain;
    std::string rogers_domain;

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
       std::map<std::string, Topology> deployment_topology = {});
  virtual ~Site();

  /// Creates and starts up the specified number of memcached instances.
  ///
  /// @param count [in] The number of processes to start.
  void create_and_start_memcached_instances(int memcached_instances);

  /// Creates and starts up the specified number of rogers instances.
  ///
  /// @param count [in] The number of processes to start.
  void create_and_start_rogers_instances(int rogers_instances);

  /// Creates and starts up the specified number of chronos instances.
  ///
  /// @param count [in] The number of processes to start.
  void create_and_start_chronos_instances(int chronos_instances);

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

  /// Wait for all the processes in the site to successfully start up.
  ///
  /// @return Whether the processes have all started successfully.
  bool wait_for_instances();

private:
  /// Utility function to get at one of the site IPs.
  std::string site_ip(int index);

  /// Index of this site. Each site must have a different index.
  int _site_index;

  /// The name of this site.
  std::string _site_name;

  /// Directory this site may use for storing config files, log files, etc.
  std::string _site_dir;

  /// The topology of all the sites in the deployment.
  std::map<std::string, Topology> _deployment_topology;

  /// Use shared pointers for managing the instances so that the memory gets
  /// freed when the vector is cleared.
  std::vector<std::shared_ptr<MemcachedInstance>> _memcached_instances;
  std::vector<std::shared_ptr<RogersInstance>> _rogers_instances;
  std::vector<std::shared_ptr<ChronosInstance>> _chronos_instances;
};

#endif


