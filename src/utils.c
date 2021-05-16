/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "utils.h"

#include <stdio.h>
#include <systemd/sd-daemon.h>

static const char kDebugEnv[] = "PUCRO_DEBUG";

static bool g_debug_enabled = false;

void SetupLogLevels() {
  const char *debug_env = getenv(kDebugEnv);
  if (debug_env != NULL && strcmp(debug_env, "1") == 0) {
    g_debug_enabled = true;
  }
}

void LogV(const char *prefix, const char *fmt, va_list args) {
  fputs(prefix, stderr);
  vfprintf(stderr, fmt, args);
}

void LogDebug(const char *fmt, ...) {
  if (!g_debug_enabled) {
    return;
  }

  va_list args;
  va_start(args, fmt);

  LogV(SD_DEBUG, fmt, args);
  fputc('\n', stderr);

  va_end(args);
}

void LogInfo(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);

  LogV(SD_INFO, fmt, args);
  fputc('\n', stderr);

  va_end(args);
}

void LogError(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);

  LogV(SD_ERR, fmt, args);
  fputc('\n', stderr);

  va_end(args);
}

void LogErrno(int errno_, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);

  LogV(SD_ERR, fmt, args);
  fprintf(stderr, ": %s\n", strerror(errno_));

  va_end(args);

  sd_notifyf(0, "ERRNO=%d", errno_);
}
