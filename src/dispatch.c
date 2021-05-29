/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "dispatch.h"

#include "utils.h"

#include <assert.h>
#include <errno.h>
#include <pwd.h>
#include <systemd/sd-bus.h>
#include <systemd/sd-event.h>
#include <unistd.h>

static const int kDispatchTimeoutSec = 5;
static const int kUsecPerSec = 1000000;

const char kSystemdService[] = "org.freedesktop.systemd1";
const char kSystemdObject[] = "/org/freedesktop/systemd1";
const char kSystemdManagerInterface[] = "org.freedesktop.systemd1.Manager";
const char kSystemdManagerStartTransientUnit[] = "StartTransientUnit";

typedef struct DispatcherProcess DispatcherProcess;

struct DispatcherProcess {
  pid_t pid;

  sd_event_source *timer_event;
  sd_event_source *death_event;

  Dispatcher *dispatcher;

  DispatcherProcess *prev;
  DispatcherProcess *next;
};

struct Dispatcher {
  sd_event *event;

  DispatcherProcess *processes;
};

static void AddProcess(Dispatcher *dispatcher, DispatcherProcess *process) {
  process->dispatcher = dispatcher;
  process->next = dispatcher->processes;
  if (process->next != NULL) {
    process->next->prev = process;
  }

  dispatcher->processes = process;
}

static void RemoveProcess(DispatcherProcess *process) {
  if (process->next != NULL) {
    process->next->prev = NULL;
  }

  if (process->prev != NULL) {
    process->prev->next = NULL;
  } else {
    // Should be head of list.
    assert(process == process->dispatcher->processes);
    process->dispatcher->processes = process->next;
  }

  process->dispatcher = NULL;
  process->prev = process->next = NULL;
}

static void DispatcherProcess_Free(DispatcherProcess *process) {
  sd_event_source_disable_unref(process->death_event);
  sd_event_source_disable_unref(process->timer_event);

  free(process);
}

Dispatcher *Dispatcher_New(sd_event *event) {
  Dispatcher *dispatcher = Alloc(sizeof(Dispatcher));
  dispatcher->event = sd_event_ref(event);
  return dispatcher;
}

void Dispatcher_Free(Dispatcher *dispatcher) {
  for (DispatcherProcess *process = dispatcher->processes; process != NULL;) {
    DispatcherProcess *to_free = process;
    process = process->next;
    DispatcherProcess_Free(to_free);
  }

  sd_event_unref(dispatcher->event);
  free(dispatcher);
}

static int OnTimerExpiration(sd_event_source *source, uint64_t usec, void *userdata) {
  DispatcherProcess *process = userdata;

  LogError("Process %d took too long to spawn subprocess, killing now", process->pid);
  if (kill(process->pid, SIGKILL) == -1) {
    LogErrno(errno, "Failed to kill %d", process->pid);
    return 0;
  }

  RemoveProcess(process);
  DispatcherProcess_Free(process);
  return 0;
}

static int OnProcessDeath(sd_event_source *source, const siginfo_t *si, void *userdata) {
  DispatcherProcess *process = userdata;

  LogDebug("Process %d died", process->pid);

  if (si->si_code != CLD_EXITED) {
    LogError("Process %d failed with signal %d", process->pid, si->si_signo);
  }

  if (si->si_code == CLD_EXITED && si->si_status != 0) {
    LogError("Process %d failed with exit status %d", process->pid, si->si_status);
  }

  RemoveProcess(process);
  DispatcherProcess_Free(process);
  return 0;
}

static sd_bus *ConnectToUserBus(const char *user) {
  CLEANUP(sd_bus_unrefp) sd_bus *bus = NULL;
  int rc = 0;

  CLEANUP_AUTOFREE char *machine = NULL;
  if (asprintf(&machine, "%s@", user) == -1) {
    abort();
  }

  if ((rc = sd_bus_open_user_machine(&bus, machine)) < 0) {
    LogErrno(-rc, "Failed to connect to %s bus", user);
    return NULL;
  }

  return STEAL_POINTER(&bus);
}

static char *GetLoginShell(const char *user) {
  struct passwd *pwd = getpwnam(user);
  if (pwd == NULL) {
    LogErrno(errno, "Failed to look up user %s", user);
    return NULL;
  }

  return StrDup(pwd->pw_shell);
}

static char *GetUnitName(sd_bus *bus) {
  int rc = 0;

  const char *name = NULL;
  if ((rc = sd_bus_get_unique_name(bus, &name)) < 0) {
    LogErrno(-rc, "Failed to get bus name");
    return NULL;
  }

  const char *name_suffix = strchr(name, '.');
  if (name_suffix == NULL || name_suffix[1] == '\0') {
    LogError("Invalid bus name: %s", name_suffix);
    return NULL;
  }

  CLEANUP_AUTOFREE char *unit_name = NULL;
  if (asprintf(&unit_name, "pucro-%s.service", name_suffix + 1) == -1) {
    abort();
  }

  return STEAL_POINTER(&unit_name);
}

static bool RunCommandAsTransientUnit(sd_bus *bus, const char *shell,
                                      const char *command) {
  CLEANUP_AUTOFREE char *unit_name = GetUnitName(bus);
  if (unit_name == NULL) {
    LogError("Failed to find unit name");
    return false;
  }

  CLEANUP(sd_bus_error_free) sd_bus_error error = SD_BUS_ERROR_NULL;
  CLEANUP(sd_bus_message_unrefp) sd_bus_message *reply = NULL;

  if (sd_bus_call_method(bus, kSystemdService, kSystemdObject, kSystemdManagerInterface,
                         kSystemdManagerStartTransientUnit, &error, &reply,
                         "ssa(sv)a(sa(sv))",
                         // Unit name
                         unit_name,
                         // Unit mode
                         "replace",
                         // # of properties
                         1,
                         // ExecStart=...
                         "ExecStart",
                         // List with a single ExecStart value
                         "a(sasb)", 1,
                         // argv0
                         shell,
                         // argv
                         3, shell, "-c", command,
                         // Literally don't remember what this is
                         false,
                         // # of values in aux array, must be empty
                         0) < 0) {
    LogError("Failed to start transient unit: %s: %s", error.name, error.message);
    return false;
  }

  return true;
}

static bool RunAsUser(const char *command, const char *user) {
  CLEANUP(sd_bus_unrefp) sd_bus *bus = ConnectToUserBus(user);
  if (bus == NULL) {
    LogError("Failed to connect to user bus %s", user);
    return false;
  }

  CLEANUP_AUTOFREE char *shell = GetLoginShell(user);
  if (shell == NULL) {
    LogError("Failed to get login shell");
    return false;
  }

  if (!RunCommandAsTransientUnit(bus, shell, command)) {
    LogError("Failed to run transient unit for: %s", command);
    return false;
  }

  return true;
}

bool Dispatcher_RunAsUser(Dispatcher *dispatcher, const char *command, const char *user) {
  pid_t pid = fork();
  if (pid == -1) {
    LogErrno(errno, "fork failed");
    return false;
  } else if (pid == 0) {
    if (!RunAsUser(command, user)) {
      LogError("Failed to complete dispatch of '%s' as '%s'", command, user);
      exit(1);
    }

    exit(0);
  } else {
    int rc = 0;
    CLEANUP(sd_event_source_unrefp) sd_event_source *timer_event = NULL;
    CLEANUP(sd_event_source_unrefp) sd_event_source *death_event = NULL;

    CLEANUP_AUTOFREE DispatcherProcess *process = Alloc(sizeof(DispatcherProcess));
    process->pid = pid;

    if ((rc = sd_event_add_time_relative(dispatcher->event, &timer_event, CLOCK_MONOTONIC,
                                         kDispatchTimeoutSec * kUsecPerSec, 0,
                                         OnTimerExpiration, process)) < 0 ||
        (rc = sd_event_add_child(dispatcher->event, &death_event, pid, WEXITED,
                                 OnProcessDeath, process)) < 0) {
      LogErrno(-rc, "Failed to add timer and death watch events for %d", pid);
      if (kill(pid, SIGKILL) == -1) {
        LogErrno(errno, "Failed to kill process after failure to monitor");
      }

      return false;
    }

    process->timer_event = STEAL_POINTER(&timer_event);
    process->death_event = STEAL_POINTER(&death_event);
    AddProcess(dispatcher, STEAL_POINTER(&process));
    return true;
  }
}
