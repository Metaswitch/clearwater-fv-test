/**
 * @file processinstance.cpp - class for controlling real instances of
 * Clearwater processes.
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
    return WIFSIGNALED(status);
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
