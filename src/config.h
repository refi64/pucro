/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#pragma once

#include "utils.h"

typedef struct ConfigRule ConfigRule;
typedef struct Config Config;

struct ConfigRule {
  char **buttons;
  char **users;
  char *action;

  ConfigRule *next;
};

struct Config {
  ConfigRule *rules;
};

Config *Config_GetInstance();

void Config_Clear(Config *config);
bool Config_Load(Config *config);

ConfigRule *Config_FindMatchingRule(Config *config, const char *user, const char *button);
