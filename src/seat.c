/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "seat.h"

#include "src/utils.h"

#include <bsd/sys/tree.h>
#include <errno.h>
#include <systemd/sd-bus.h>
#include <uthash.h>

const char kLogindService[] = "org.freedesktop.login1";
const char kLogindObject[] = "/org/freedesktop/login1";
const char kLogindManagerInterface[] = "org.freedesktop.login1.Manager";
const char kLogindManagerSeatNew[] = "SeatNew";
const char kLogindManagerSeatRemoved[] = "SeatRemoved";
const char kLogindManagerListSeats[] = "ListSeats";
const char kLogindSeatInterface[] = "org.freedesktop.login1.Seat";
const char kLogindSeatActiveSession[] = "ActiveSession";
const char kLogindSessionInterface[] = "org.freedesktop.login1.Session";
const char kLogindSessionName[] = "Name";

struct SeatMonitor {
  sd_bus *bus;
  SeatMonitorSeat *seats;

  SeatMonitor_OnSeatAdded on_seat_added;
  SeatMonitor_OnSeatRemoved on_seat_removed;

  void *userdata;
  SeatMonitor_UserDataDestroy userdata_destroy;
};

static void SeatMonitorSeat_Free(SeatMonitorSeat *seat) {
  free(STEAL_POINTER(&seat->object));
  free(STEAL_POINTER(&seat->id));
  free(seat);
}

SeatMonitor *SeatMonitor_New(sd_event *event, int priority) {
  CLEANUP(sd_bus_unrefp) sd_bus *bus = NULL;
  int rc = 0;

  if ((rc = sd_bus_default_system(&bus)) < 0) {
    LogErrno(-rc, "Failed to get default bus");
    return NULL;
  }

  if ((rc = sd_bus_set_close_on_exit(bus, true)) < 0) {
    LogErrno(-rc, "Failed to set bus to close on exit");
    return NULL;
  }

  if ((rc = sd_bus_attach_event(bus, event, priority)) < 0) {
    LogErrno(-rc, "Failed to attach bus to event");
    return NULL;
  }

  SeatMonitor *monitor = Alloc(sizeof(SeatMonitor));
  monitor->bus = STEAL_POINTER(&bus);
  return monitor;
}

static void AddSeat(SeatMonitor *monitor, const char *seat_id, const char *seat_object) {
  LogDebug("SeatMonitor: add seat %s", seat_id);

  SeatMonitorSeat *match = NULL;

  HASH_FIND_STR(monitor->seats, seat_id, match);
  if (match != NULL) {
    LogInfo("Ignoring addition of duplicate seat: %s", match->id);
    return;
  }

  SeatMonitorSeat *seat = Alloc(sizeof(SeatMonitorSeat));
  seat->id = StrDup(seat_id);
  seat->object = StrDup(seat_object);
  HASH_ADD_STR(monitor->seats, id, seat);

  if (monitor->on_seat_added) {
    monitor->on_seat_added(monitor, seat, monitor->userdata);
  }
}

static void RemoveSeat(SeatMonitor *monitor, const char *seat_id) {
  LogDebug("SeatMonitor: remove seat %s", seat_id);

  SeatMonitorSeat *match = NULL;
  HASH_FIND_STR(monitor->seats, seat_id, match);
  if (match == NULL) {
    LogInfo("Ignoring removal of unknown seat: %s", seat_id);
    return;
  }

  HASH_DEL(monitor->seats, match);

  if (monitor->on_seat_removed) {
    monitor->on_seat_removed(monitor, match, monitor->userdata);
  }

  SeatMonitorSeat_Free(match);
}

static int OnNewOrRemovedSeat(sd_bus_message *message, void *userdata,
                              sd_bus_error *error) {
  SeatMonitor *monitor = userdata;

  bool is_new = strcmp(sd_bus_message_get_member(message), kLogindManagerSeatNew) == 0;

  const char *seat_id = NULL, *seat_object = NULL;

  int rc = 0;
  if ((rc = sd_bus_message_read(message, "so", &seat_id, &seat_object)) < 0) {
    LogErrno(-rc, "Failed to parse message from signal %s",
             sd_bus_message_get_member(message));
    return rc;
  }

  if (is_new) {
    AddSeat(monitor, seat_id, seat_object);
  } else {
    RemoveSeat(monitor, seat_id);
  }

  return 1;
}

bool SeatMonitor_Start(SeatMonitor *monitor) {
  int rc = 0;
  if ((rc = sd_bus_match_signal(monitor->bus, NULL, kLogindService, kLogindObject,
                                kLogindManagerInterface, kLogindManagerSeatNew,
                                OnNewOrRemovedSeat, monitor)) < 0 ||
      (rc = sd_bus_match_signal(monitor->bus, NULL, kLogindService, kLogindObject,
                                kLogindManagerInterface, kLogindManagerSeatRemoved,
                                OnNewOrRemovedSeat, monitor)) < 0) {
    LogErrno(-rc, "Failed to watch logind signals");
    return false;
  }

  CLEANUP(sd_bus_error_free) sd_bus_error error = SD_BUS_ERROR_NULL;
  CLEANUP(sd_bus_message_unrefp) sd_bus_message *reply = NULL;
  if (sd_bus_call_method(monitor->bus, kLogindService, kLogindObject,
                         kLogindManagerInterface, kLogindManagerListSeats, &error, &reply,
                         "") < 0) {
    LogError("Failed to list current seats: %s: %s", error.name, error.message);
    return false;
  }

  if ((rc = sd_bus_message_enter_container(reply, 'a', "(so)")) < 0) {
    LogErrno(-rc, "Failed to enter seats array");
    return false;
  }

  const char *seat_id = NULL, *seat_object = NULL;
  while ((rc = sd_bus_message_read(reply, "(so)", &seat_id, &seat_object)) > 0) {
    AddSeat(monitor, seat_id, seat_object);
  }

  if (rc < 0) {
    LogErrno(-rc, "Failed to read seats");
    return false;
  }

  if ((rc = sd_bus_message_exit_container(reply)) < 0) {
    LogErrno(-rc, "Failed to exit seats array");
    return false;
  }

  return true;
}

void SeatMonitor_SetSeatAddedCallback(SeatMonitor *monitor,
                                      SeatMonitor_OnSeatAdded on_seat_added) {
  monitor->on_seat_added = on_seat_added;
}

void SeatMonitor_SetSeatRemovedCallback(SeatMonitor *monitor,
                                        SeatMonitor_OnSeatRemoved on_seat_removed) {
  monitor->on_seat_removed = on_seat_removed;
}

void SeatMonitor_SetUserData(SeatMonitor *monitor, void *userdata,
                             SeatMonitor_UserDataDestroy userdata_destroy) {
  monitor->userdata = userdata;
  monitor->userdata_destroy = userdata_destroy;
}

const SeatMonitorSeat *SeatMonitor_GetSeats(SeatMonitor *monitor) {
  return monitor->seats;
}

const SeatMonitorSeat *SeatMonitor_FindSeat(SeatMonitor *monitor, const char *seat_id) {
  SeatMonitorSeat *match = NULL;
  HASH_FIND_STR(monitor->seats, seat_id, match);
  return match;
}

const char *SeatMonitor_GetUser(SeatMonitor *monitor, const SeatMonitorSeat *seat) {
  int rc = 0;
  CLEANUP(sd_bus_error_free) sd_bus_error error = SD_BUS_ERROR_NULL;

  CLEANUP(sd_bus_message_unrefp) sd_bus_message *seat_session_value = NULL;
  if (sd_bus_get_property(monitor->bus, kLogindService, seat->object,
                          kLogindSeatInterface, kLogindSeatActiveSession, &error,
                          &seat_session_value, "(so)") < 0) {
    LogError("Failed to get session for seat %s: %s: %s", seat->id, error.name,
             error.message);
    return NULL;
  }

  const char *session_id, *session_object = NULL;
  if ((rc = sd_bus_message_read(seat_session_value, "(so)", &session_id,
                                &session_object)) < 0) {
    LogErrno(-rc, "Failed to parse session for seat %s", seat->id);
    return NULL;
  }

  CLEANUP(sd_bus_message_unrefp) sd_bus_message *session_name_value = NULL;
  if (sd_bus_get_property(monitor->bus, kLogindService, session_object,
                          kLogindSessionInterface, kLogindSessionName, &error,
                          &session_name_value, "s") < 0) {
    LogError("Failed to get user for session %s: %s: %s", session_id, error.name,
             error.message);
    return NULL;
  }

  const char *session_name = NULL;
  if ((rc = sd_bus_message_read(session_name_value, "s", &session_name)) < 0) {
    LogErrno(-rc, "Failed to parse user name for session %s", session_id);
    return NULL;
  }

  return StrDup(session_name);
}

void SeatMonitor_Free(SeatMonitor *monitor) {
  SeatMonitorSeat *seat = NULL, *tmp = NULL;
  HASH_ITER(hh, monitor->seats, seat, tmp) {
    HASH_DEL(monitor->seats, seat);
    SeatMonitorSeat_Free(seat);
  }

  if (monitor->userdata_destroy) {
    monitor->userdata_destroy(monitor->userdata);
  }

  sd_bus_unref(monitor->bus);
  free(monitor);
}
