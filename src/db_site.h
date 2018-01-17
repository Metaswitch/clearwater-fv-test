/**
 * @file db_site.h Helper class for spinning up all the processes in a site.
 *
 * Copyright (C) Metaswitch Networks 2018
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */

#ifndef DB_SITE_H__
#define DB_SITE_H__

#include <string>
#include <memory>
#include <vector>

class DbSite
{
public:
  /// Constructor
  ///
  /// @param index [in] The (typically 1-based) index of the site. This must be
  ///                   unique across all sites.
  /// @param dir [in]   A directory that the site may create and use to store
  ///                   any temporary files it requires.
  DbSite(int index, const std::string& dir);
  virtual ~DbSite();

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
  std::vector<std::string> get_rogers_ips();;

  /// Wait for all the processes in the site to successfully start up.
  ///
  /// @return Whether the processes have all started successfully.
  bool wait_for_instances();

private:
  /// Utility function to get at one of the site IPs.
  std::string site_ip(int index);

  /// Index of this site. Each site must have a different index.
  int _site_index;

  /// Directory this site may use for storing config files, log files, etc.
  std::string _site_dir;

  /// Use shared pointers for managing the instances so that the memory gets
  /// freed when the vector is cleared.
  std::vector<std::shared_ptr<MemcachedInstance>> _memcached_instances;
  std::vector<std::shared_ptr<RogersInstance>> _rogers_instances;
  std::vector<std::shared_ptr<ChronosInstance>> _chronos_instances;
};

#endif


