/**
 * @file astaireinstance.cpp - class for controlling a real astaire instance.
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

#include "astaireinstance.h"

#include <unistd.h>
#include <signal.h>
#include <cstdio>
#include <sys/types.h>
#include <sys/wait.h>

/// Start this instance of Astaire.
bool AstaireInstance::start_instance()
{
  bool success;

  // Fork the current process so that we can start a Astaire instance.
  int pid = fork();

  if (pid == -1)
  {
    // Failed to fork.
    perror("fork");
    success = false;
  }
  else if (pid == 0)
  {
    // This is the new process, so start Astaire. execlp only returns if an
    // error has occurred, in which case return false.
    execlp("../modules/astaire/build/bin/astaire",
           "astaire",
           "--local-name",
           "127.0.0.1",
           "--cluster-settings-file",
           "./cluster_settings",
           (char*)NULL);
    perror("execlp");
    success = false;
  }
  else
  {
    // This is the original process, so save off the new PID and return true.
    _pid = pid;
    success = true;
  }
  return success;
}

/// Kill this instance of Astaire.
bool AstaireInstance::kill_instance()
{
  int status;

  if (kill(_pid, SIGTERM) == 0)
  {
    waitpid(_pid, &status, 0);
    return WIFEXITED(status);
  }
  else
  {
    // Failed to kill Astaire.
    perror("kill");
    return false;
  }
}

/// Restart this instance of Astaire.
bool AstaireInstance::restart_instance()
{
  // Kill and start the instance.
  bool success = kill_instance();
  
  if (success)
  {
    success = start_instance();
  }

  return success;
}
