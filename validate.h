#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "clock.h"
#include "config.h"
#include "ntpshm.h"
#include "pi.h"
#include "print.h"
#include "raw.h"
#include "sk.h"
#include "transport.h"
#include "udp6.h"
#include "uds.h"
#include "util.h"
#include "version.h"

struct config_item;

enum profile_mode {
	G_8265_1_MOOC,
	G_8265_1_SOOC,
	G_8275_1,
	G_8275_1_SLAVEONLY,
	G_8275_2,
	G_8275_2_SLAVEONLY,
	E_INVALID = 100,
	E_OUT_OF_BOUNDS = 101,
};

void profile_config_destroy(struct config *cfg);

enum profile_mode get_profile_mode(const char *config,
				   struct config *cfg,
				   char *profile_type);

int get_profile_tab(enum profile_mode mode,
		    struct config_item **profile_tab_ptr,
		    int *config_tab_size_ptr);

void set_profile_specific_range(struct config *cfg, enum profile_mode mode);

void restore_profile_specific_range(struct config *cfg, enum profile_mode mode);

int profile_config_set_string(struct config *cfg,
			      const char *option,
			      const char *val);

int config_set_ranged_int(struct config *cfg,
			  const char *section,
			  const char *option,
			  int min,
			  int max);

int profile_config_set_section_int(struct config *cfg,
				   const char *section,
				   const char *option,
				   int val);

void profile_validate(struct config_item *cgi,
		      struct config_item *dst,
		      bool *missing,
		      bool *range_out);

int profile_range_fail_check(struct config *cfg,
			     struct config_item *profile_tab,
			     int config_tab_size,
			     enum profile_mode mode);

void display_profile_defaults(enum profile_mode mode);
