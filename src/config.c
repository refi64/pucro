/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "config.h"

#include "src/utils.h"

#include <confuse.h>
#include <strings.h>

CLEANUP_AUTOPTR_DEFINE(cfg_t, cfg_free)

#define CONFIG_FILE SYSCONFDIR "/pucro.conf"

static void StrvFree(char **values) {
  for (char **p = values; p != NULL && *p != NULL; p++) {
    free(*p);
  }

  free(values);
}

Config *Config_GetInstance() {
  static Config config = {NULL};
  return &config;
}

void Config_Clear(Config *config) {
  for (ConfigRule *rule = STEAL_POINTER(&config->rules); rule != NULL;) {
    StrvFree(rule->buttons);
    StrvFree(rule->users);
    free(rule->action);

    ConfigRule *next = rule->next;
    free(rule);
    rule = next;
  }
}

static void LibConfuseErrorHandler(cfg_t *cfg, const char *fmt, va_list args) {
  CLEANUP_AUTOFREE char *message = NULL;
  if (vasprintf(&message, fmt, args) == -1) {
    abort();
  }

  LogError("Failed to parse %s:%d: %s", cfg->filename, cfg->line, message);
}

static char **CfgStringListToStrv(cfg_t *cfg, const char *key) {
  size_t count = cfg_size(cfg, key);
  char **strv = Alloc(sizeof(char *) * (count + 1));

  for (size_t i = 0; i < count; i++) {
    strv[i] = StrDup(cfg_getnstr(cfg, key, i));
  }

  return strv;
}

bool Config_Load(Config *config) {
  CLEANUP(Config_Clear) Config new_config = {NULL};

  cfg_opt_t rule_opts[] = {
      CFG_STR_LIST("buttons", "{}", CFGF_NODEFAULT),
      CFG_STR_LIST("users", "{}", CFGF_NONE),
      CFG_STR("action", NULL, CFGF_NODEFAULT),
      CFG_END(),
  };

  cfg_opt_t opts[] = {
      CFG_SEC("rule", rule_opts, CFGF_MULTI),
      CFG_END(),
  };

  CLEANUP_AUTOPTR(cfg_t) cfg = cfg_init(opts, CFGF_NONE);
  cfg_set_error_function(cfg, LibConfuseErrorHandler);

  int ret = cfg_parse(cfg, CONFIG_FILE);
  if (ret != CFG_SUCCESS) {
    LogError("Failed to parse config file");
    return false;
  }

  for (size_t i = 0; i < cfg_size(cfg, "rule"); i++) {
    cfg_t *rule_cfg = cfg_getnsec(cfg, "rule", i);

    ConfigRule *rule = Alloc(sizeof(ConfigRule));
    rule->buttons = CfgStringListToStrv(rule_cfg, "buttons");
    rule->users = CfgStringListToStrv(rule_cfg, "users");
    rule->action = StrDup(cfg_getstr(rule_cfg, "action"));
    rule->next = new_config.rules;
    new_config.rules = rule;
  }

  Config_Clear(config);
  config->rules = STEAL_POINTER(&new_config.rules);
  return true;
}

static bool StrvContainsIgnoreCase(char **strv, const char *item) {
  for (; *strv != NULL; strv++) {
    if (strcasecmp(*strv, item) == 0) {
      return true;
    }
  }

  return false;
}

ConfigRule *Config_FindMatchingRule(Config *config, const char *user,
                                    const char *button) {
  for (ConfigRule *rule = config->rules; rule != NULL; rule = rule->next) {
    if (StrvContainsIgnoreCase(rule->users, user) &&
        StrvContainsIgnoreCase(rule->buttons, button)) {
      return rule;
    }
  }

  return NULL;
}
