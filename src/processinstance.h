/**
 * @file processinstance.h - class for controlling real instances of Clearwater
 * processes.
 *
 * Copyright (C) Metaswitch Networks 2015
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */

#include <string>
#include <map>
#include <vector>

class ProcessInstance
{
public:
  ProcessInstance(std::string ip, int port) : _ip(ip), _port(port), _running(false) {};
  ProcessInstance(int port) : ProcessInstance("127.0.0.1", port) {};
  virtual ~ProcessInstance() { kill_instance(); }

  bool start_instance();
  bool kill_instance();
  bool restart_instance();
  bool wait_for_instance();

  std::string ip() const { return _ip; }
  int port() const { return _port; }

private:
  virtual bool execute_process() = 0;

  std::string _ip;
  int _port;
  int _pid;
  bool _running;
};

class MemcachedInstance : public ProcessInstance
{
public:
  MemcachedInstance(const std::string& ip, int port) : ProcessInstance(ip, port) {};
  virtual bool execute_process();
};

class RogersInstance : public ProcessInstance
{
public:
  RogersInstance(const std::string& ip, int port, const std::string& cluster_settings_file) :
    ProcessInstance(ip, port),
    _cluster_settings_file(cluster_settings_file)
  {};
  virtual bool execute_process();

private:
  std::string _cluster_settings_file;
};

class DnsmasqInstance : public ProcessInstance
{
public:
  DnsmasqInstance(std::string ip, int port, std::map<std::string, std::vector<std::string>> a_records) :
    ProcessInstance(ip, port) { write_config(a_records); };
  ~DnsmasqInstance() { std::remove(_cfgfile.c_str()); };

  bool execute_process();
private:
  void write_config(std::map<std::string, std::vector<std::string>> a_records);
  std::string _cfgfile;
};

class ChronosInstance : public ProcessInstance
{
public:
  ChronosInstance(const std::string& ip,
                  int port,
                  const std::string& instance_dir,
                  const std::string& cluster_conf_file,
                  const std::string& shared_conf_file,
                  const std::string& dns_ip,
                  int dns_port);
  virtual ~ChronosInstance();
  bool execute_process();

private:
  std::string _instance_dir;
  std::string _log_dir;
  std::string _conf_dir;
  std::string _local_conf_file;
  std::string _cluster_conf_file;
  std::string _shared_conf_file;
};
