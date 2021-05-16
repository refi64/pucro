/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#pragma once

#include "utils.h"

#include <libinput.h>
#include <systemd/sd-event.h>

typedef struct InputMonitor InputMonitor;

typedef void (*InputMonitor_OnInputEvent)(InputMonitor *monitor, const char *seat_id,
                                          struct libinput_event *event, void *userdata);
typedef void (*InputMonitor_UserDataDestroy)(void *userdata);

InputMonitor *InputMonitor_New(sd_event *event);

void InputMonitor_Free(InputMonitor *monitor);

void InputMonitor_SetInputEventCallback(InputMonitor *monitor,
                                        InputMonitor_OnInputEvent on_input_event);
void InputMonitor_SetUserData(InputMonitor *monitor, void *userdata,
                              InputMonitor_UserDataDestroy userdata_destroy);

bool InputMonitor_Add(InputMonitor *monitor, const char *seat_id);
bool InputMonitor_Remove(InputMonitor *monitor, const char *seat_id);

CLEANUP_AUTOPTR_DEFINE(InputMonitor, InputMonitor_Free)
