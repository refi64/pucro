/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#pragma once

#include "utils.h"

#include <systemd/sd-event.h>

typedef struct Dispatcher Dispatcher;

Dispatcher *Dispatcher_New(sd_event *event);

void Dispatcher_Free(Dispatcher *dispatcher);

bool Dispatcher_RunAsUser(Dispatcher *dispatcher, const char *command, const char *user);

CLEANUP_AUTOPTR_DEFINE(Dispatcher, Dispatcher_Free)
