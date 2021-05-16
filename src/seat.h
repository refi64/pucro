/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#pragma once

#include "utils.h"

#include <systemd/sd-event.h>
#include <uthash.h>

typedef struct SeatMonitorUser SeatMonitorUser;
typedef struct SeatMonitorSeat SeatMonitorSeat;
typedef struct SeatMonitor SeatMonitor;

typedef void (*SeatMonitor_OnSeatAdded)(SeatMonitor *monitor, SeatMonitorSeat *seat,
                                        void *userdata);
typedef void (*SeatMonitor_OnSeatRemoved)(SeatMonitor *monitor, SeatMonitorSeat *seat,
                                          void *userdata);
typedef void (*SeatMonitor_UserDataDestroy)(void *userdata);

struct SeatMonitorSeat {
  char *id;
  char *object;

  UT_hash_handle hh;
};

SeatMonitor *SeatMonitor_New(sd_event *event, int priority);

bool SeatMonitor_Start(SeatMonitor *monitor);

void SeatMonitor_SetSeatAddedCallback(SeatMonitor *monitor,
                                      SeatMonitor_OnSeatAdded on_seat_added);
void SeatMonitor_SetSeatRemovedCallback(SeatMonitor *monitor,
                                        SeatMonitor_OnSeatRemoved on_seat_removed);
void SeatMonitor_SetUserData(SeatMonitor *monitor, void *userdata,
                             SeatMonitor_UserDataDestroy userdata_destroy);

const SeatMonitorSeat *SeatMonitor_GetSeats(SeatMonitor *monitor);
const SeatMonitorSeat *SeatMonitor_FindSeat(SeatMonitor *monitor, const char *seat_id);

const char *SeatMonitor_GetUser(SeatMonitor *monitor, const SeatMonitorSeat *seat);

void SeatMonitor_Free(SeatMonitor *monitor);

CLEANUP_AUTOPTR_DEFINE(SeatMonitor, SeatMonitor_Free)
