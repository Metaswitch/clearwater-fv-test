/**
 * @file site.cpp Helper class for spinning up all the processes in a
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

#include "log.h"

#include "processinstance.h"
#include "site.h"

static const int MEMCACHED_PORT = 33333;
static const int ROGERS_PORT = 11311;
static const int CHRONOS_PORT = 7253;


Site::Site(int index,
           const std::string& name,
           const std::string& dir,
           std::map<std::string, Topology> deployment_topology,
           int num_memcached,
           int num_rogers,
           int num_chronos) :
  _site_index(index),
  _site_name(name),
  _site_dir(dir),
  _deployment_topology(deployment_topology)
{
  boost::filesystem::create_directory(_site_dir);
  create_memcached_instances(num_memcached);
  create_rogers_instances(num_rogers);
  create_chronos_instances(num_chronos);
}


Site::~Site()
{
  _memcached_instances.clear();
  _rogers_instances.clear();
  _chronos_instances.clear();

  boost::filesystem::remove_all(_site_dir);
}


std::string Site::site_ip(int index)
{
  return "127.0." + std::to_string(_site_index) + "." + std::to_string(index);
}


void Site::create_memcached_instances(int count)
{
  std::ofstream cluster_settings(_site_dir + "/cluster_settings");

  for (int ii = 0; ii < count; ++ii)
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
    cluster_settings << ip << ":" << std::to_string(MEMCACHED_PORT);
  }

  cluster_settings.close();
}


void Site::create_rogers_instances(int count)
{
  for (int ii = 0; ii < count; ++ii)
  {
    _rogers_instances.emplace_back(new RogersInstance(site_ip(ii + 1),
                                                      ROGERS_PORT,
                                                      _site_dir + "/cluster_settings"));
  }
}


void Site::create_chronos_instances(int count)
{
  // Create directory to hold chronos config and logs.
  std::string chronos_dir = _site_dir + "/chronos";
  boost::filesystem::create_directory(chronos_dir);

  // If we have a deployment topology, create a shared config file containing
  // information about all the sites.
  if (!_deployment_topology.empty())
  {
    std::string shared_conf_file = chronos_dir + "/chronos_shared.conf";
    std::ofstream conf;
    conf.open(shared_conf_file);
    conf << "[sites]\n";

    for (const std::pair<std::string, Topology>& item : _deployment_topology)
    {
      std::string name = item.first;
      const Topology& tplg = item.second;

      if (name == _site_name)
      {
        conf << "local_site = " << name << "\n";
      }
      else
      {
        conf << "remote_site = " << name << "=" << tplg.chronos_domain << "\n";
      }
    }

    conf.close();
  }

  // Create the chronos config that is common across all nodes in the cluster.
  std::string cluster_conf_file = chronos_dir + "/chronos_cluster.conf";
  std::ofstream conf;
  conf.open(cluster_conf_file);
  conf << "[cluster]\n";

  for (int ii = 0; ii < count; ++ii)
  {
    std::string ip = site_ip(ii + 1);
    conf << "node = " << ip << ":" << std::to_string(CHRONOS_PORT) << "\n";

    std::string dir = chronos_dir + "/instance" + std::to_string(ii + 1);
    _chronos_instances.emplace_back(new ChronosInstance(ip,
                                                        CHRONOS_PORT,
                                                        dir,
                                                        cluster_conf_file));
  }

  conf.close();
}


void Site::for_each_instance(std::function<void(std::shared_ptr<ProcessInstance>)> fn)
{
  for (const std::shared_ptr<MemcachedInstance>& inst : _memcached_instances)
  {
    fn(inst);
  }

  for (const std::shared_ptr<RogersInstance>& inst : _rogers_instances)
  {
    fn(inst);
  }

  for (const std::shared_ptr<ChronosInstance>& inst : _chronos_instances)
  {
    fn(inst);
  }
}


void Site::start()
{
  for_each_instance([](std::shared_ptr<ProcessInstance> inst)
  {
    inst->start_instance();
  });
}


void Site::restart()
{
  for_each_instance([](std::shared_ptr<ProcessInstance> inst)
  {
    inst->restart_instance();
  });
}


void Site::kill()
{
  for_each_instance([](std::shared_ptr<ProcessInstance> inst)
  {
    inst->kill_instance();
  });
}


bool Site::wait_for_instances()
{
  bool ok = true;

  for_each_instance([&ok](std::shared_ptr<ProcessInstance> inst)
  {
    if (!inst->wait_for_instance())
    {
      ok = false;
    }
  });

  return ok;
}


std::vector<std::string> Site::get_chronos_ips()
{
  std::vector<std::string> ips;
  for (const std::shared_ptr<ChronosInstance> instance : _chronos_instances)
  {
    ips.push_back(instance->ip());
  }

  return ips;
}


std::vector<std::string> Site::get_rogers_ips()
{
  std::vector<std::string> ips;
  for (const std::shared_ptr<RogersInstance> instance : _rogers_instances)
  {
    ips.push_back(instance->ip());
  }

  return ips;
}


std::shared_ptr<ChronosInstance> Site::get_first_chronos()
{
  return _chronos_instances.back();
}


std::shared_ptr<RogersInstance> Site::get_first_rogers()
{
  return _rogers_instances.back();
}


std::shared_ptr<MemcachedInstance> Site::get_first_memcached()
{
  return _memcached_instances.back();
}


Site::Topology& Site::Topology::with_chronos(const std::string& domain)
{
  chronos_domain = domain;
  return *this;
}


Site::Topology& Site::Topology::with_rogers(const std::string& domain)
{
  rogers_domain = domain;
  return *this;
}
