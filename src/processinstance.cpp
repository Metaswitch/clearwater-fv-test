/**
 * @file processinstance.cpp - class for controlling real instances of
 * Clearwater processes.
 *
 * Copyright (C) Metaswitch Networks 2016
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */

#include "processinstance.h"

#include <unistd.h>
#include <signal.h>
#include <cstdio>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <netdb.h>
#include <cstring>
#include <fstream>
#include <boost/filesystem.hpp>

/// Start this instance.
bool ProcessInstance::start_instance()
{
  bool success;

  // Fork the current process so that we can start an instance of the process.
  int pid = fork();

  if (pid == -1)
  {
    // Failed to fork.
    perror("fork");
    success = false;
  }
  else if (pid == 0)
  {
    // This is the new process, so execute the process.
    success = execute_process();
  }
  else
  {
    // This is the original process, so save off the new PID and return true.
    _pid = pid;
    success = true;
  }
  return success;
}

/// Kill this instance.
bool ProcessInstance::kill_instance()
{
  int status;

  if (kill(_pid, SIGTERM) == 0)
  {
    waitpid(_pid, &status, 0);
    return (WIFSIGNALED(status) || WIFEXITED(status));
  }
  else
  {
    // Failed to kill the instance.
    perror("kill");
    return false;
  }
}

/// Restart this instance.
bool ProcessInstance::restart_instance()
{
  // Kill and start the instance.
  return kill_instance() && start_instance();
}

/// Wait for the instance to come up by trying to connect to the port the
/// instance listens on.
bool ProcessInstance::wait_for_instance()
{
  struct addrinfo hints, *res;
  int sockfd;
  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  getaddrinfo(_ip.c_str(), std::to_string(_port).c_str(), &hints, &res);
  sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);

  if (sockfd == -1)
  {
    perror("socket");
    return false;
  }

  bool connected = false;
  int attempts = 0;

  // If the process hasn't come up after 5 seconds, call the whole thing off.
  //
  // Sleep a little bit to begin with to allow the instance to come up,
  // otherwise we are almost guaranteed to wait for at least 1s.
  usleep(10000);

  while ((!connected) && (attempts < 5))
  {
    if (connect(sockfd, res->ai_addr, res->ai_addrlen) == 0)
    {
      connected = true;
    }
    else
    {
      sleep(1);
      attempts++;
    }
  }

  close(sockfd);
  freeaddrinfo(res);

  return connected;
}

bool MemcachedInstance::execute_process()
{
  // Start memcached. execlp only returns if an error has occurred, in which
  // case return false.
  execlp("/usr/bin/memcached",
         "memcached",
         "-l",
         _ip.c_str(),
         "-p",
         std::to_string(_port).c_str(),
         "-e",
         "ignore_vbucket=true",
         (char*)NULL);
  perror("execlp");
  return false;
}

std::string get_log_level()
{
  // Work out the log level of the tests by parsing the NOISY=t:?
  // environment variable.
  int log_level = 0;
  char* val = getenv("NOISY");
  if ((val != NULL) && (strchr("TtYy", val[0]) != NULL))
  {
    if (val != NULL)
    {
      val = strchr(val, ':');

      if (val != NULL)
      {
        log_level = strtol(val + 1, NULL, 10);
      }
    }
  }

  return std::to_string(log_level);
}

bool RogersInstance::execute_process()
{
  // Start Rogers. execlp only returns if an error has occurred, in which case
  // return false. This assumes that cluster_settings has been setup.
  execlp("../modules/astaire/build/bin/rogers",
         "rogers",
         "--bind-addr",
         _ip.c_str(),
         "--cluster-settings-file",
         _cluster_settings_file.c_str(),
         "--log-level",
         get_log_level().c_str(),
         (char*)NULL);
  perror("execlp");
  return false;
}

void DnsmasqInstance::write_config(std::map<std::string, std::vector<std::string>> a_records)
{
  _cfgfile = _ip + "_" + std::to_string(_port) + "_" + "_dnsmasq.cfg";

  std::ofstream ofs(_cfgfile, std::ios::trunc);
  ofs << "listen-address=" << _ip << "\n";
  ofs << "port=" << _port << "\n";

  for (auto i: a_records)
  {
    for (auto ip: i.second)
    {
      ofs << "host-record=" << i.first << "," << ip << "\n";
    }
  }

  ofs.close();
}

bool DnsmasqInstance::execute_process()
{
  // Start dnsmasq. execlp only returns if an error has occurred, in which
  // case return false.
  execlp("/usr/sbin/dnsmasq",
         "dnsmasq",
         "-z", // don't bind to all interfaces
         "-k", // keep in foreground
         "-C",
         _cfgfile.c_str(),
         (char*)NULL);
  perror("execlp");
  return false;
}

ChronosInstance::ChronosInstance(const std::string& ip,
                                 int port,
                                 const std::string& instance_dir,
                                 const std::string& cluster_conf_file) :
  ProcessInstance(ip, port),
  _instance_dir(instance_dir),
  _log_dir(_instance_dir + "/log"),
  _conf_dir(_instance_dir + "/conf"),
  _local_conf_file(_conf_dir + "/chronos.conf"),
  _cluster_conf_file(cluster_conf_file)
{
  boost::filesystem::create_directory(_instance_dir);
  boost::filesystem::create_directory(_log_dir);
  boost::filesystem::create_directory(_conf_dir);

  std::ofstream config;
  config.open(_local_conf_file);
  config << "[logging]\n"
         << "level = " << get_log_level() << "\n"
         << "folder = " << _log_dir << "\n"
         << "\n"
         << "[http]\n"
         << "bind-address = " << _ip << "\n"
         << "bind-port = " << _port << "\n"
         << "\n"
         << "[throttling]\n"
         << "max_tokens = 1000\n"
         << "\n"
         << "[cluster]\n"
         << "localhost = " << _ip << ":" << _port << "\n";

  config.close();
}

ChronosInstance::~ChronosInstance()
{
  boost::filesystem::remove_all(_instance_dir);
}

bool ChronosInstance::execute_process()
{
  // Redirect stdout and stderr to a log file to avoid cluttering up the test
  // output (and also so we can tell what chronos instance it came from).
  std::string out_file = _log_dir + "/chronos_stdout.txt";
  std::string err_file = _log_dir + "/chronos_stderr.txt";
  freopen(out_file.c_str(), "a", stdout);
  freopen(err_file.c_str(), "a", stderr);

  // Start Chronos. execlp only returns if an error has occurred, in which case
  // return false.
  execlp("../modules/chronos/build/bin/chronos",
         "chronos",
         "--local-config-file", _local_conf_file.c_str(),
         "--cluster-config-file", _cluster_conf_file.c_str(),
         (char*)NULL);
  perror("execlp");
  return false;
}
