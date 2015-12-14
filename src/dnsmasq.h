/**
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

#include <fstream>
#include <string>
#include <map>
#include <cstdlib>
#include <cassert>

#ifndef DNSMASQ_H
#define DNSMASQ_H

class DNSMasq
{
public:
  DNSMasq(std::string local_ip, int port, std::map<std::string, std::string> a_records)
  {
    _cfgfile = local_ip + "_" + std::to_string(port) + "_" + "_dnsmasq.cfg";
    _pidfile = "/tmp/" + local_ip + "_" + std::to_string(port) + "_" + "_dnsmasq.pid";
    kill_pidfile();

    std::ofstream ofs(_cfgfile, std::ios::trunc);
    ofs << "listen-address=" << local_ip << "\n";
    ofs << "port=" << port << "\n";

    for (auto i: a_records)
    {
      ofs << "address=/" << i.first << "/" << i.second << "\n";
    }

    ofs.close();

    std::string cmd = "dnsmasq -z -x " + _pidfile + " -C " + _cfgfile;
    int rc = system(cmd.c_str());
    assert(rc == 0);
  }

  ~DNSMasq()
  {
    kill_pidfile();
    std::remove(_cfgfile.c_str());
    std::remove(_pidfile.c_str());
  }

  void kill_pidfile()
  {
    std::ifstream input(_pidfile);
    if (input.good())
    {
      int pid = 0;
      input >> pid;
      kill(pid, SIGTERM);
    }
  }

private:
  std::string _pidfile;
  std::string _cfgfile;
};

#endif
