/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "config.h"
#include "dispatch.h"
#include "input.h"
#include "seat.h"
#include "utils.h"

#include <errno.h>
#include <libevdev/libevdev.h>
#include <libinput.h>
#include <systemd/sd-daemon.h>
#include <systemd/sd-event.h>

typedef struct EventHandlerData EventHandlerData;

struct EventHandlerData {
  InputMonitor *input_monitor;
  SeatMonitor *seat_monitor;
  Dispatcher *dispatcher;
};

const char kButtonNamePrefix[] = "BTN_";

CLEANUP_AUTOPTR_ALIAS(sd_event, sd_event_unrefp)

static int ReloadConfigOnSigHup(sd_event_source *source,
                                const struct signalfd_siginfo *info, void *userdata) {
  sd_notify(0, "RELOADING=1");

  if (!Config_Load(Config_GetInstance())) {
    LogError("Failed to reload config on request");
  }

  sd_notify(0, "READY=1");
  return 0;
}

static bool SetupSignalHandlers(sd_event *event) {
  sigset_t mask;
  sigemptyset(&mask);
  sigaddset(&mask, SIGINT);
  sigaddset(&mask, SIGTERM);
  sigaddset(&mask, SIGHUP);
  sigaddset(&mask, SIGCHLD);

  if (sigprocmask(SIG_BLOCK, &mask, NULL) == -1) {
    LogErrno(errno, "Falied to block signals");
    return false;
  }

  int rc = 0;
  if ((rc = sd_event_add_signal(event, NULL, SIGINT, NULL, NULL)) < 0 ||
      (rc = sd_event_add_signal(event, NULL, SIGTERM, NULL, NULL)) < 0) {
    LogErrno(-rc, "Failed to add termination signal handlers");
    return false;
  }

  if ((rc = sd_event_add_signal(event, NULL, SIGHUP, ReloadConfigOnSigHup, NULL)) < 0) {
    LogErrno(-rc, "Failed to add config reload signal handler");
    return false;
  }

  return true;
}

static void LookupRuleAndDispatch(EventHandlerData *handler_data, const char *seat_id,
                                  const char *button_name) {
  if (strncmp(button_name, kButtonNamePrefix, strlen(kButtonNamePrefix)) != 0) {
    LogError("Unexpected button name %s", button_name);
    return;
  }

  const char *button = button_name + strlen(kButtonNamePrefix);

  const SeatMonitorSeat *seat = SeatMonitor_FindSeat(handler_data->seat_monitor, seat_id);
  if (seat == NULL) {
    LogError("Failed to find seat with id %s", seat_id);
    return;
  }

  CLEANUP_AUTOFREE char *user = SeatMonitor_GetUser(handler_data->seat_monitor, seat);
  if (user == NULL) {
    LogError("Failed to get user for seat %s", seat_id);
    return;
  }

  LogDebug("Find rule for %s pressing %s", user, button);

  Config *config = Config_GetInstance();
  ConfigRule *rule = Config_FindMatchingRule(config, user, button);
  if (rule != NULL) {
    LogInfo("Dispatch '%s' as '%s'", rule->action, user);

    if (!Dispatcher_RunAsUser(handler_data->dispatcher, rule->action, user)) {
      LogError("Failed to dispatch '%s' as '%s'", rule->action, user);
    }
  }
}

static void OnInputEvent(InputMonitor *input_monitor, const char *seat_id,
                         struct libinput_event *event, void *userdata) {
  EventHandlerData *handler_data = userdata;

  if (libinput_event_get_type(event) != LIBINPUT_EVENT_POINTER_BUTTON) {
    return;
  }

  struct libinput_event_pointer *pointer_event = libinput_event_get_pointer_event(event);

  uint32_t button = libinput_event_pointer_get_button(pointer_event);
  const char *button_name = libevdev_event_code_get_name(EV_KEY, button);
  enum libinput_button_state state =
      libinput_event_pointer_get_button_state(pointer_event);
  bool pressed = state == LIBINPUT_BUTTON_STATE_PRESSED;

  LogDebug("Pointer button %s in state %s", button_name,
           pressed ? "pressed" : "released");

  if (pressed) {
    LookupRuleAndDispatch(handler_data, seat_id, button_name);
  }
}

static void OnAddedSeat(SeatMonitor *seat_monitor, SeatMonitorSeat *seat,
                        void *userdata) {
  EventHandlerData *handler_data = userdata;
  if (!InputMonitor_Add(handler_data->input_monitor, seat->id)) {
    LogError("Failed to monitor input to newly added seat %s", seat->id);
  }
}

static void OnRemovedSeat(SeatMonitor *seat_monitor, SeatMonitorSeat *seat,
                          void *userdata) {
  EventHandlerData *handler_data = userdata;
  if (!InputMonitor_Remove(handler_data->input_monitor, seat->id)) {
    LogError("Failed to stop monitoring removed seat %s", seat->id);
  }
}

static bool Run() {
  SetupLogLevels();

  if (!Config_Load(Config_GetInstance())) {
    LogError("Failed to load config file to initialize");
    return false;
  }

  int rc = 0;

  CLEANUP_AUTOPTR(sd_event) event = NULL;
  if ((rc = sd_event_default(&event)) < 0) {
    LogError("Failed to create sd-event");
    return false;
  }

  if ((rc = sd_event_set_watchdog(event, true)) < 0) {
    LogError("Failed to set sd-event watchdog");
    return false;
  }

  if (!SetupSignalHandlers(event)) {
    LogError("Failed to set up signal handlers");
    return false;
  }

  CLEANUP_AUTOPTR(InputMonitor) input_monitor = InputMonitor_New(event);
  if (input_monitor == NULL) {
    LogError("Failed to create input monitor");
    return false;
  }

  CLEANUP_AUTOPTR(SeatMonitor)
  seat_monitor = SeatMonitor_New(event, SD_EVENT_PRIORITY_NORMAL);
  if (seat_monitor == NULL) {
    LogError("Failed to create seat monitor");
    return false;
  }

  CLEANUP_AUTOPTR(Dispatcher) dispatcher = Dispatcher_New(event);
  if (dispatcher == NULL) {
    LogError("Failed to create dispatcher");
    return false;
  }

  EventHandlerData handler_data = {
      .input_monitor = input_monitor,
      .seat_monitor = seat_monitor,
      .dispatcher = dispatcher,
  };

  SeatMonitor_SetSeatAddedCallback(seat_monitor, OnAddedSeat);
  SeatMonitor_SetSeatRemovedCallback(seat_monitor, OnRemovedSeat);
  SeatMonitor_SetUserData(seat_monitor, &handler_data, NULL);

  InputMonitor_SetInputEventCallback(input_monitor, OnInputEvent);
  InputMonitor_SetUserData(input_monitor, &handler_data, NULL);

  if (!SeatMonitor_Start(seat_monitor)) {
    LogError("Failed to start seat monitor");
    return false;
  }

  sd_notify(0, "READY=1");

  if ((rc = sd_event_loop(event) < 0)) {
    LogErrno(-rc, "Failed to run event loop");
    return false;
  }

  Config_Clear(Config_GetInstance());
  return true;
}

int main() {
  if (!Run()) {
    return 1;
  }

  return 0;
}
