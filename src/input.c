/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "input.h"

#include "src/utils.h"

#include <errno.h>
#include <fcntl.h>
#include <libinput.h>
#include <libudev.h>
#include <sys/epoll.h>
#include <systemd/sd-event.h>
#include <unistd.h>
#include <uthash.h>

typedef struct InputMonitorSeat InputMonitorSeat;

struct InputMonitorSeat {
  char *seat_id;

  struct libinput *libinput;
  sd_event_source *source;

  InputMonitor *monitor;

  UT_hash_handle hh;
};

struct InputMonitor {
  sd_event *event;
  struct udev *udev;

  InputMonitorSeat *seats;

  InputMonitor_OnInputEvent on_input_event;

  void *userdata;
  InputMonitor_UserDataDestroy userdata_destroy;
};

static void InputMonitorSeat_Free(InputMonitorSeat *seat) {
  free(STEAL_POINTER(&seat->seat_id));

  if (seat->libinput != NULL) {
    libinput_unref(STEAL_POINTER(&seat->libinput));
  }

  sd_event_source_disable_unref(STEAL_POINTER(&seat->source));

  free(seat);
}

CLEANUP_AUTOPTR_DEFINE(InputMonitorSeat, InputMonitorSeat_Free);
CLEANUP_AUTOPTR_DEFINE(libinput, libinput_unref)
CLEANUP_AUTOPTR_DEFINE(libinput_event, libinput_event_destroy)

InputMonitor *InputMonitor_New(sd_event *event) {
  struct udev *udev = udev_new();
  if (udev == NULL) {
    LogError("Failed to create udev instance");
    return NULL;
  }

  InputMonitor *monitor = Alloc(sizeof(InputMonitor));
  monitor->event = sd_event_ref(event);
  monitor->udev = udev;
  return monitor;
}

void InputMonitor_SetInputEventCallback(InputMonitor *monitor,
                                        InputMonitor_OnInputEvent on_input_event) {
  monitor->on_input_event = on_input_event;
}

void InputMonitor_SetUserData(InputMonitor *monitor, void *userdata,
                              InputMonitor_UserDataDestroy userdata_destroy) {
  monitor->userdata = userdata;
  monitor->userdata_destroy = userdata_destroy;
}

static int LibInputRestrictedOpen(const char *path, int flags, void *userdata) {
  int fd = open(path, flags);
  return fd != -1 ? fd : -errno;
}

static void LibInputRestrictedClose(int fd, void *userdata) { close(fd); }

static int OnInputEvents(sd_event_source *source, int fd, uint32_t revents,
                         void *userdata) {
  InputMonitorSeat *seat = userdata;
  InputMonitor *monitor = seat->monitor;

  if (revents & (EPOLLHUP | EPOLLERR)) {
    LogError("Hangup / error while monitoring %s, disabling", seat->seat_id);
    return -EINTR;
  }

  for (;;) {
    int rc = 0;
    if ((rc = libinput_dispatch(seat->libinput)) < 0) {
      LogErrno(-rc, "Failed to dispatch events for %s", seat->seat_id);
      return rc;
    }

    CLEANUP_AUTOPTR(libinput_event) event = libinput_get_event(seat->libinput);
    if (event == NULL) {
      break;
    }

    if (monitor->on_input_event) {
      monitor->on_input_event(monitor, seat->seat_id, event, monitor->userdata);
    }
  }

  return 0;
}

bool InputMonitor_Add(InputMonitor *monitor, const char *seat_id) {
  LogDebug("InputMonitor: add seat %s", seat_id);

  static struct libinput_interface libinput_interface = {
      .open_restricted = LibInputRestrictedOpen,
      .close_restricted = LibInputRestrictedClose,
  };

  InputMonitorSeat *match = NULL;
  HASH_FIND_STR(monitor->seats, seat_id, match);
  if (match != NULL) {
    LogInfo("Ignoring duplicate input seat: %s", seat_id);
    return false;
  }

  CLEANUP_AUTOPTR(libinput)
  libinput = libinput_udev_create_context(&libinput_interface, NULL, monitor->udev);
  if (libinput == NULL) {
    LogError("Failed to create libinput context");
    return NULL;
  }

  if (libinput_udev_assign_seat(libinput, seat_id) == -1) {
    LogError("Failed to assign libinput seat %s", seat_id);
    return NULL;
  }

  CLEANUP_AUTOPTR(InputMonitorSeat) seat = Alloc(sizeof(InputMonitorSeat));

  seat->seat_id = StrDup(seat_id);
  seat->libinput = STEAL_POINTER(&libinput);
  seat->monitor = monitor;

  int rc = 0;
  CLEANUP(sd_event_source_unrefp) sd_event_source *source = NULL;
  if ((rc = sd_event_add_io(monitor->event, &source, libinput_get_fd(seat->libinput),
                            EPOLLIN, OnInputEvents, seat)) < 0) {
    LogErrno(-rc, "Failed to monitor libinput seat %s", seat_id);
    return NULL;
  }

  seat->source = STEAL_POINTER(&source);

  // Can't use STEAL_POINTER, because HASH_ADD_STR may evaluate the value
  // argument multiple times.
  HASH_ADD_STR(monitor->seats, seat_id, seat);
  seat = NULL;

  return true;
}

bool InputMonitor_Remove(InputMonitor *monitor, const char *seat_id) {
  LogDebug("InputMonitor: remove seat %s", seat_id);

  InputMonitorSeat *match = NULL;
  HASH_FIND_STR(monitor->seats, seat_id, match);
  if (match == NULL) {
    LogInfo("Ignoring removal of missing input seat: %s", seat_id);
    return false;
  }

  HASH_DEL(monitor->seats, match);
  InputMonitorSeat_Free(match);
  return true;
}

void InputMonitor_Free(InputMonitor *monitor) {
  InputMonitorSeat *seat = NULL, *tmp = NULL;
  HASH_ITER(hh, monitor->seats, seat, tmp) {
    HASH_DEL(monitor->seats, seat);
    InputMonitorSeat_Free(seat);
  }

  if (monitor->userdata_destroy) {
    monitor->userdata_destroy(monitor->userdata);
  }

  sd_event_unref(monitor->event);
  udev_unref(monitor->udev);
  free(monitor);
}
