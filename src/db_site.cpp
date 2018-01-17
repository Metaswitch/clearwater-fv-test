/**
 * @file db_site.cpp Helper class for spinning up all the processes in a
 * site.
 *
 * Copyright (C) Metaswitch Networks 2018
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */

#include <fstream>
#include <boost/filesystem.hpp>

#include "processinstance.h"
#include "db_site.h"

static const int MEMCACHED_PORT = 33333;
static const int ROGERS_PORT = 11311;
static const int CHRONOS_PORT = 7253;


DbSite::DbSite(int index, const std::string& dir) :
  _site_index(index), _site_dir(dir)
{
  boost::filesystem::create_directory(_site_dir);
}


DbSite::~DbSite()
{
  _memcached_instances.clear();
  _rogers_instances.clear();
  _chronos_instances.clear();

  if (remove("cluster_settings") != 0)
  {
    perror("remove cluster_settings");
  }

  boost::filesystem::remove_all(_site_dir);
}


std::string DbSite::site_ip(int index)
{
  return "127.0." + std::to_string(_site_index) + "." + std::to_string(index);
}


void DbSite::create_and_start_memcached_instances(int memcached_instances)
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

    // Each instance should listen on a new IP address.
    std::string ip = site_ip(ii + 1);
    _memcached_instances.emplace_back(new MemcachedInstance(ip, MEMCACHED_PORT));
    _memcached_instances.back()->start_instance();

    cluster_settings << ip << ":" << std::to_string(MEMCACHED_PORT);
  }

  cluster_settings.close();
}


void DbSite::create_and_start_rogers_instances(int rogers_instances)
{
  for (int ii = 0; ii < rogers_instances; ++ii)
  {
    _rogers_instances.emplace_back(new RogersInstance(site_ip(ii + 1), ROGERS_PORT));
    _rogers_instances.back()->start_instance();
  }
}


void DbSite::create_and_start_chronos_instances(int chronos_instances)
{
  // Create directory to hold chronos config and logs.
  std::string chronos_dir = _site_dir + "/chronos";
  boost::filesystem::create_directory(chronos_dir);

  // Create the chronos config that is common across all nodes in the cluster.
  std::string cluster_conf_file = chronos_dir + "/chronos_cluster.conf";
  std::ofstream conf;
  conf.open(cluster_conf_file);
  conf << "[cluster]\n";

  for (int ii = 0; ii < chronos_instances; ++ii)
  {
    std::string ip = site_ip(ii + 1);
    conf << "node = " << ip << ":" << std::to_string(CHRONOS_PORT) << "\n";

    std::string dir = chronos_dir + "/instance" + std::to_string(ii + 1);
    _chronos_instances.emplace_back(new ChronosInstance(ip,
                                                        CHRONOS_PORT,
                                                        dir,
                                                        cluster_conf_file));
    _chronos_instances.back()->start_instance();
  }

  conf.close();
}


bool DbSite::wait_for_instances()
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

  return success;
}


std::vector<std::string> DbSite::get_chronos_ips()
{
  std::vector<std::string> ips;
  for (const std::shared_ptr<ChronosInstance> instance : _chronos_instances)
  {
    ips.push_back(instance->ip());
  }

  return ips;
}


std::vector<std::string> DbSite::get_rogers_ips()
{
  std::vector<std::string> ips;
  for (const std::shared_ptr<RogersInstance> instance : _rogers_instances)
  {
    ips.push_back(instance->ip());
  }

  return ips;
}
