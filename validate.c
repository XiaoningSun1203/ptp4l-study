#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "config.c"
#include "validate.h"

#define MISSING_ENUM_VALUE 0xff
#define MISSING_INT_VALUE  1534545687
#define MISSING_STR_VALUE  "no_string"

#define N_G_8265_1_MASTER_CONFIG_ITEMS       (sizeof(g_8265_1_config_tab_MOOC) / sizeof(g_8265_1_config_tab_MOOC[0]))
#define N_G_8265_1_SLAVE_CONFIG_ITEMS        (sizeof(g_8265_1_config_tab_SOOC) / sizeof(g_8265_1_config_tab_SOOC[0]))
#define N_G_8275_1_CONFIG_ITEMS              (sizeof(g_8275_1_config_tab) / sizeof(g_8275_1_config_tab[0]))
#define N_G_8275_1_SLAVEONLY_CONFIG_ITEMS    (sizeof(g_8275_1_config_tab_SLAVEONLY) / sizeof(g_8275_1_config_tab_SLAVEONLY[0]))
#define N_G_8275_2_CONFIG_ITEMS              (sizeof(g_8275_2_config_tab) / sizeof(g_8275_2_config_tab[0]))
#define N_G_8275_2_SLAVEONLY_CONFIG_ITEMS    (sizeof(g_8275_2_config_tab_SLAVEONLY) / sizeof(g_8275_2_config_tab_SLAVEONLY[0]))

static struct config_enum profile_delay_mech_enu[] = {
	{ "E2E", DM_E2E },
	{ NULL, 0 },
};

static struct config_enum g_8265_1_profile_nw_trans_enu[] = {
	{ "UDPv4", TRANS_UDP_IPV4 },
	{ "UDPv6", TRANS_UDP_IPV6 },
	{ NULL, 0 },
};

static struct config_enum g_8275_1_profile_nw_trans_enu[] = {
	{ "L2",    TRANS_IEEE_802_3 },
	{ NULL, 0 },
};

static struct config_enum g_8275_2_profile_nw_trans_enu[] = {
	{ "UDPv4", TRANS_UDP_IPV4 },
	{ "UDPv6", TRANS_UDP_IPV6 },
	{ NULL, 0 },
};

static struct config_enum g_8265_1_profile_clock_type_enu[] = {
	{ "OC",      CLOCK_TYPE_ORDINARY },
	{ NULL, 0 },
};

static struct config_enum g_8275_1_profile_clock_type_enu[] = {
	{ "OC",      CLOCK_TYPE_ORDINARY },
	{ "BC",      CLOCK_TYPE_BOUNDARY },
	{ "E2E_TC",  CLOCK_TYPE_E2E      },
	{ NULL, 0 },
};

static struct config_enum g_8275_2_profile_clock_type_enu[] = {
	{ "OC",      CLOCK_TYPE_ORDINARY },
	{ "BC",      CLOCK_TYPE_BOUNDARY },
	{ NULL, 0 },
};

static struct config_enum g_8275_x_profile_clock_type_slaveonly_enu[] = {
	{ "OC",      CLOCK_TYPE_ORDINARY },
	{ NULL, 0 },
};

static struct config_enum g_8275_1_GM_profile_clockAccuracy[] = {
	{ "0x20", 0x20 },
	{ "0x21", 0x21 },
	{ "0xfe", 0xfe },
	{ NULL, 0 },
};

static struct config_enum g_8265_1_MOOC_profile_clockClass[] = {
	{ "80", 80 },
	{ "82", 82 },
	{ "84", 84 },
	{ "86", 86 },
	{ "90", 90 },
	{ "96", 96 },
	{ "100", 100 },
	{ "102", 102 },
	{ "104", 104 },
	{ "106", 106 },
	{ "108", 108 },
	{ "110", 110 },
	{ NULL, 0 },
};

static struct config_enum g_8275_x_profile_clockClass[] = {
	{ "6", 6 },
	{ "7", 7 },
	{ "135", 135 },
	{ "140", 140 },
	{ "150", 150 },
	{ "160", 160 },
	{ "165", 165 },
	{ "248", 248 },
	{ NULL, 0 },
};

static struct config_enum g_8275_1_GM_BC_profile_OSLV[] = {
	{ "0x4b32", 0x4b32 },
	{ "0X4e5d", 0x4e5d },
	{ "0xffff", 0xffff },
	{ "65535", 0xffff },
	{ NULL, 0 },
};

/**
 * The profile specific configuration tabs below have a wrong default value.
 * This is to check for comparison with the proper values in the .cfg file.
 * If the proper value is defined through the .cfg, the wrong value is replaced.
 * If the values are absent or improper in the .cfg file, reports a failure.
 */

static struct config_item g_8265_1_config_tab_MOOC[] = {
	PORT_ITEM_INT("announceReceiptTimeout", MISSING_INT_VALUE, 2, 2),
	GLOB_ITEM_ENU("clockClass", MISSING_INT_VALUE, g_8265_1_MOOC_profile_clockClass),
	GLOB_ITEM_ENU("clock_type", MISSING_ENUM_VALUE, g_8265_1_profile_clock_type_enu),
	PORT_ITEM_ENU("delay_mechanism", MISSING_ENUM_VALUE, profile_delay_mech_enu),
	GLOB_ITEM_INT("domainNumber", MISSING_INT_VALUE, 4, 23),
	PORT_ITEM_INT("masterOnly", MISSING_INT_VALUE, 1, 1),
	PORT_ITEM_ENU("network_transport", MISSING_ENUM_VALUE, g_8265_1_profile_nw_trans_enu),
	GLOB_ITEM_INT("slaveOnly", MISSING_INT_VALUE, 0, 0),
	PORT_ITEM_INT("transportSpecific", MISSING_INT_VALUE, 0, 0),
};

static struct config_item g_8265_1_config_tab_SOOC[] = {
	GLOB_ITEM_INT("clockClass", MISSING_INT_VALUE, 255, 255),
	GLOB_ITEM_ENU("clock_type", MISSING_ENUM_VALUE, g_8265_1_profile_clock_type_enu),
	PORT_ITEM_ENU("delay_mechanism", MISSING_ENUM_VALUE, profile_delay_mech_enu),
	GLOB_ITEM_INT("domainNumber", MISSING_INT_VALUE, 4, 23),
	PORT_ITEM_INT("masterOnly", MISSING_INT_VALUE, 0, 0),
	PORT_ITEM_ENU("network_transport", MISSING_ENUM_VALUE, g_8265_1_profile_nw_trans_enu),
	GLOB_ITEM_INT("slaveOnly", MISSING_INT_VALUE, 1, 1),
	PORT_ITEM_INT("transportSpecific", MISSING_INT_VALUE, 0, 0),
	PORT_ITEM_INT("unicast_req_duration", MISSING_INT_VALUE, 60, 1000),
};

static struct config_item g_8275_1_config_tab[] = {
	PORT_ITEM_INT("announceReceiptTimeout", MISSING_INT_VALUE, 3, UINT8_MAX),
	GLOB_ITEM_ENU("clockAccuracy", MISSING_ENUM_VALUE, g_8275_1_GM_profile_clockAccuracy),
	GLOB_ITEM_ENU("clockClass", MISSING_ENUM_VALUE, g_8275_x_profile_clockClass),
	GLOB_ITEM_ENU("clock_type", MISSING_ENUM_VALUE, g_8275_1_profile_clock_type_enu),
	PORT_ITEM_ENU("delay_mechanism", MISSING_ENUM_VALUE, profile_delay_mech_enu),
	GLOB_ITEM_INT("domainNumber", MISSING_INT_VALUE, 24, 43),
	GLOB_ITEM_INT("G.8275.defaultDS.localPriority", MISSING_INT_VALUE, 1, UINT8_MAX),
	PORT_ITEM_INT("G.8275.portDS.localPriority", MISSING_INT_VALUE, 1, UINT8_MAX),
	PORT_ITEM_INT("logAnnounceInterval", MISSING_INT_VALUE, -3, -3),
	PORT_ITEM_INT("logMinDelayReqInterval", MISSING_INT_VALUE, -4, -4),
	PORT_ITEM_INT("logSyncInterval", MISSING_INT_VALUE, -4, -4),
	PORT_ITEM_INT("masterOnly", MISSING_INT_VALUE, 0, 1),
	GLOB_ITEM_INT("maxStepsRemoved", MISSING_INT_VALUE, 1, UINT8_MAX),
	PORT_ITEM_ENU("network_transport", MISSING_ENUM_VALUE, g_8275_1_profile_nw_trans_enu),
	GLOB_ITEM_ENU("offsetScaledLogVariance", MISSING_ENUM_VALUE, g_8275_1_GM_BC_profile_OSLV),
	GLOB_ITEM_INT("priority1", MISSING_INT_VALUE, 128, 128),
	GLOB_ITEM_INT("priority2", MISSING_INT_VALUE, 0, UINT8_MAX),
	PORT_ITEM_STR("ptp_dst_mac", MISSING_STR_VALUE),
	GLOB_ITEM_INT("slaveOnly", MISSING_INT_VALUE, 0, 0),
	GLOB_ITEM_INT("timeSource", MISSING_INT_VALUE, 0, UINT8_MAX),
};

static struct config_item g_8275_1_config_tab_SLAVEONLY[] = {
	PORT_ITEM_INT("announceReceiptTimeout", MISSING_INT_VALUE, 3, UINT8_MAX),
	GLOB_ITEM_INT("clockAccuracy", MISSING_INT_VALUE, 0xfe, 0xfe),
	GLOB_ITEM_INT("clockClass", MISSING_INT_VALUE, 255, 255),
	GLOB_ITEM_ENU("clock_type", MISSING_ENUM_VALUE, g_8275_x_profile_clock_type_slaveonly_enu),
	PORT_ITEM_ENU("delay_mechanism", MISSING_ENUM_VALUE, profile_delay_mech_enu),
	GLOB_ITEM_INT("domainNumber", MISSING_INT_VALUE, 24, 43),
	GLOB_ITEM_INT("G.8275.defaultDS.localPriority", MISSING_INT_VALUE, 1, UINT8_MAX),
	PORT_ITEM_INT("G.8275.portDS.localPriority", MISSING_INT_VALUE, 1, UINT8_MAX),
	PORT_ITEM_INT("logAnnounceInterval", MISSING_INT_VALUE, -3, -3),
	PORT_ITEM_INT("logMinDelayReqInterval", MISSING_INT_VALUE, -4, -4),
	PORT_ITEM_INT("logSyncInterval", MISSING_INT_VALUE, -4, -4),
	PORT_ITEM_INT("masterOnly", MISSING_INT_VALUE, 0, 0),
	GLOB_ITEM_INT("maxStepsRemoved", MISSING_INT_VALUE, 1, UINT8_MAX),
	PORT_ITEM_ENU("network_transport", MISSING_ENUM_VALUE, g_8275_1_profile_nw_trans_enu),
	GLOB_ITEM_INT("offsetScaledLogVariance", MISSING_INT_VALUE, 0xffff, 0xffff),
	GLOB_ITEM_INT("priority1", MISSING_INT_VALUE, 128, 128),
	GLOB_ITEM_INT("priority2", MISSING_INT_VALUE, 255, 255),
	PORT_ITEM_STR("ptp_dst_mac", MISSING_STR_VALUE),
	GLOB_ITEM_INT("slaveOnly", MISSING_INT_VALUE, 1, 1),
	GLOB_ITEM_INT("timeSource", MISSING_INT_VALUE, 0, UINT8_MAX),
};

static struct config_item g_8275_2_config_tab[] = {
	PORT_ITEM_INT("announceReceiptTimeout", MISSING_INT_VALUE, 2, 2),
	GLOB_ITEM_INT("clockAccuracy", MISSING_INT_VALUE, 0, UINT8_MAX),
	GLOB_ITEM_ENU("clockClass", MISSING_ENUM_VALUE, g_8275_x_profile_clockClass),
	GLOB_ITEM_ENU("clock_type", MISSING_ENUM_VALUE, g_8275_2_profile_clock_type_enu),
	PORT_ITEM_ENU("delay_mechanism", MISSING_ENUM_VALUE, profile_delay_mech_enu),
	GLOB_ITEM_INT("domainNumber", MISSING_INT_VALUE, 44, 63),
	GLOB_ITEM_INT("G.8275.defaultDS.localPriority", MISSING_INT_VALUE, 1, UINT8_MAX),
	PORT_ITEM_INT("G.8275.portDS.localPriority", MISSING_INT_VALUE, 1, UINT8_MAX),
	PORT_ITEM_INT("masterOnly", MISSING_INT_VALUE, 0, 1),
	GLOB_ITEM_INT("maxStepsRemoved", MISSING_INT_VALUE, 1, UINT8_MAX),
	PORT_ITEM_ENU("network_transport", MISSING_ENUM_VALUE, g_8275_2_profile_nw_trans_enu),
	GLOB_ITEM_INT("offsetScaledLogVariance", MISSING_INT_VALUE, 0xffff, 0xffff),
	GLOB_ITEM_INT("priority1", MISSING_INT_VALUE, 128, 128),
	GLOB_ITEM_INT("priority2", MISSING_INT_VALUE, 0, UINT8_MAX),
	GLOB_ITEM_INT("slaveOnly", MISSING_INT_VALUE, 0, 0),
	GLOB_ITEM_INT("timeSource", MISSING_INT_VALUE, 0, UINT8_MAX),
	PORT_ITEM_INT("transportSpecific", MISSING_INT_VALUE, 0, 0),
};

static struct config_item g_8275_2_config_tab_SLAVEONLY[] = {
	GLOB_ITEM_INT("clockAccuracy", MISSING_INT_VALUE, 0xfe, 0xfe),
	GLOB_ITEM_INT("clockClass", MISSING_INT_VALUE, 255, 255),
	GLOB_ITEM_ENU("clock_type", MISSING_ENUM_VALUE, g_8275_x_profile_clock_type_slaveonly_enu),
	PORT_ITEM_ENU("delay_mechanism", MISSING_ENUM_VALUE, profile_delay_mech_enu),
	GLOB_ITEM_INT("domainNumber", MISSING_INT_VALUE, 44, 63),
	GLOB_ITEM_INT("G.8275.defaultDS.localPriority", MISSING_INT_VALUE, 1, UINT8_MAX),
	PORT_ITEM_INT("G.8275.portDS.localPriority", MISSING_INT_VALUE, 1, UINT8_MAX),
	PORT_ITEM_INT("masterOnly", MISSING_INT_VALUE, 0, 0),
	GLOB_ITEM_INT("maxStepsRemoved", MISSING_INT_VALUE, 1, UINT8_MAX),
	PORT_ITEM_ENU("network_transport", MISSING_ENUM_VALUE, g_8275_2_profile_nw_trans_enu),
	GLOB_ITEM_INT("offsetScaledLogVariance", MISSING_INT_VALUE, 0xffff, 0xffff),
	GLOB_ITEM_INT("priority1", MISSING_INT_VALUE, 128, 128),
	GLOB_ITEM_INT("priority2", MISSING_INT_VALUE, 255, 255),
	GLOB_ITEM_INT("slaveOnly", MISSING_INT_VALUE, 1, 1),
	GLOB_ITEM_INT("timeSource", MISSING_INT_VALUE, 0, UINT8_MAX),
	PORT_ITEM_INT("transportSpecific", MISSING_INT_VALUE, 0, 0),
	PORT_ITEM_INT("unicast_req_duration", MISSING_INT_VALUE, 60, 1000),
};

static void profile_config_item_free(void *ptr)
{
	struct config_item *ci = ptr;

	if (ci->type == CFG_TYPE_STRING && ci->flags & CFG_ITEM_DYNSTR) {
		free(ci->val.s);
		ci->val.s = NULL;
		ci->flags &= ~CFG_ITEM_DYNSTR;
	}
	if (ci->flags & CFG_ITEM_STATIC)
		return;
	free(ci);
}

void profile_config_destroy(struct config *cfg)
{
	struct unicast_master_address *address;
	struct unicast_master_table *table;
	struct interface *iface;

	while ((iface = STAILQ_FIRST(&cfg->interfaces))) {
		STAILQ_REMOVE_HEAD(&cfg->interfaces, list);
		interface_destroy(iface);
	}
	while ((table = STAILQ_FIRST(&cfg->unicast_master_tables))) {
		while ((address = STAILQ_FIRST(&table->addrs))) {
			STAILQ_REMOVE_HEAD(&table->addrs, list);
			free(address);
		}
		if (table->peer_name) {
			free(table->peer_name);
		}
		STAILQ_REMOVE_HEAD(&cfg->unicast_master_tables, list);
		free(table);
	}
	hash_destroy(cfg->htab, profile_config_item_free);
	free(cfg->opts);
	free(cfg);
	cfg = NULL;
}


int profile_config_set_string(struct config *cfg,
			      const char *option,
			      const char *val)
{
	struct config_item *ci;
	ci = config_find_item(cfg, NULL, option);

	if (!ci || ci->type != CFG_TYPE_STRING) {
		pr_err("bug: config option %s missing or invalid!", option);
		return -1;
	}
	if (ci->flags & CFG_ITEM_DYNSTR) {
		free(ci->val.s);
	}

	ci->val.s = strdup(val);
	if (!ci->val.s) {
		pr_err("low memory");
		return -1;
	}
	ci->flags |= CFG_ITEM_DYNSTR;
	pr_debug("locked item global.%s as '%s'", option, ci->val.s);
	return 0;
}

int profile_config_set_section_int(struct config *cfg,
				   const char *section,
				   const char *option,
				   int val)
{
	struct config_item *cgi, *dst;

	cgi = config_find_item(cfg, NULL, option);
	if (!cgi) {
		pr_err("bug: config option %s missing!", option);
		return -1;
	}
	switch (cgi->type) {
	case CFG_TYPE_DOUBLE:
	case CFG_TYPE_STRING:
		pr_err("bug: config option %s type mismatch!", option);
		return -1;
	case CFG_TYPE_INT:
	case CFG_TYPE_ENUM:
		break;
	}
	if (!section) {
		cgi->val.i = val;
		return 0;
	}
	/* Create or update this port specific item. */
	dst = config_section_item(cfg, section, option);
	if (!dst) {
		dst = config_item_alloc(cfg, section, option, cgi->type);
		if (!dst)
			return -1;
	}
	dst->val.i = val;
	pr_debug("section item %s.%s now %d\n", section, option, dst->val.i);
	return 0;
}

int config_set_ranged_int(struct config *cfg,
			  const char *section,
			  const char *option,
			  int min,
			  int max)
{
	struct config_item *cgi, *dst;

	cgi = config_find_item(cfg, NULL, option);
	if (!cgi) {
		pr_err("bug: config option %s missing!", option);
		return -1;
	}
	switch (cgi->type) {
	case CFG_TYPE_DOUBLE:
	case CFG_TYPE_STRING:
		pr_err("bug: config option %s type mismatch!", option);
		return -1;
	case CFG_TYPE_INT:
	case CFG_TYPE_ENUM:
		break;
	}
	if (!section) {
		cgi->min.i = min;
		cgi->max.i = max;
		return 0;
	}
	/* Create or update this port specific item. */
	dst = config_section_item(cfg, section, option);
	if (!dst) {
		dst = config_item_alloc(cfg, section, option, cgi->type);
		if (!dst)
			return -1;
	}
	dst->min.i = min;
	dst->max.i = max;
	pr_debug("Min. value %s.%s is %d\n", section, option, dst->min.i);
	pr_debug("Max. value %s.%s is %d\n", section, option, dst->max.i);
	return 0;
}

void set_profile_specific_range(struct config *cfg, enum profile_mode mode)
{
	struct config_item *oslv, *clkClass, *clkAccuracy;

	if (mode == G_8265_1_MOOC) {
		clkClass = config_find_item(cfg, "global", "clockClass");
		clkClass->type = CFG_TYPE_ENUM;
		clkClass->tab = g_8265_1_MOOC_profile_clockClass;
	}
	if (mode == G_8275_1) {
		oslv = config_find_item(cfg, "global", "offsetScaledLogVariance");
		clkClass = config_find_item(cfg, "global", "clockClass");
		clkAccuracy = config_find_item(cfg, "global", "clockAccuracy");
		oslv->type = CFG_TYPE_ENUM;
		oslv->tab = g_8275_1_GM_BC_profile_OSLV;
		clkClass->type = CFG_TYPE_ENUM;
		clkClass->tab = g_8275_x_profile_clockClass;
		clkAccuracy->type = CFG_TYPE_ENUM;
		clkAccuracy->tab = g_8275_1_GM_profile_clockAccuracy;
	}
	if (mode == G_8275_2) {
		clkClass = config_find_item(cfg, "global", "clockClass");
		clkClass->type = CFG_TYPE_ENUM;
		clkClass->tab = g_8275_x_profile_clockClass;
	}
}

void restore_profile_specific_range(struct config *cfg, enum profile_mode mode)
{
	struct config_item *oslv, *clkClass, *clkAccuracy;

	if (mode == G_8265_1_MOOC) {
		clkClass = config_find_item(cfg, "global", "clockClass");
		clkClass->type = CFG_TYPE_INT;
		clkClass->tab = NULL;
	}
	if (mode == G_8275_1) {
		oslv = config_find_item(cfg, "global", "offsetScaledLogVariance");
		clkClass = config_find_item(cfg, "global", "clockClass");
		clkAccuracy = config_find_item(cfg, "global", "clockAccuracy");
		oslv->type = CFG_TYPE_INT;
		oslv->tab = NULL;
		clkClass->type = CFG_TYPE_INT;
		clkClass->tab = NULL;
		clkAccuracy->type = CFG_TYPE_INT;
		clkAccuracy->tab = NULL;
	}
	if (mode == G_8275_2) {
		clkClass = config_find_item(cfg, "global", "clockClass");
		clkClass->type = CFG_TYPE_INT;
		clkClass->tab = NULL;
	}
}

static void usage(char *progname)
{
	fprintf(stderr,
		"\nusage: %s [options]\n\n"
		" -f [file]         read configuration from 'file'\n"
		" -p [profile name] Profile to be validated\n"
		"                   Options:\n"
		"                   G.8265.1/g.8265.1\n"
		"                   G.8275.1/g.8275.1\n"
		"                   G.8275.2/g.8275.2\n"
		" -l [num]          set the logging level to 'num'\n"
		" -m                print messages to stdout\n"
		" -h                prints this message and exits\n"
		"\n",
		progname);
}

void display_profile_defaults(enum profile_mode mode)
{
	switch (mode) {
	case G_8265_1_MOOC:
		printf("\nDefault values for G.8265.1 master profile are:\n\n"
		       " announceReceiptTimeout  2        Allowed values {2}\n"
		       " clockClass              110      Allowed range  {80, 82, 84, 86, 90, 96, 100, 102, 104, 106, 108, 110}\n"
		       " clock_type              OC       Allowed values {OC}\n"
		       " delay_mechanism         E2E      Allowed values {E2E}\n"
		       " domainNumber            4        Allowed range  [4, 23]\n"
		       " masterOnly              1        Allowed values {1}\n"
		       " network_transport       UDPv4    Allowed values {UDPv4, UDPv6}\n"
		       " slaveOnly               0        Allowed values {0}\n"
		       " transportSpecific       0        Allowed values {0}\n"
		       "\n");
		break;
	case G_8265_1_SOOC:
		printf("\nDefault values for G.8265.1 slave profile are:\n\n"
		       " clockClass              255      Allowed values {255}\n"
		       " clock_type              OC       Allowed values {OC}\n"
		       " delay_mechanism         E2E      Allowed values {E2E}\n"
		       " domainNumber            4        Allowed range  [4, 23]\n"
		       " masterOnly              0        Allowed values {0}\n"
		       " network_transport       UDPv4    Allowed values {UDPv4, UDPv6}\n"
		       " slaveOnly               1        Allowed values {1}\n"
		       " transportSpecific       0        Allowed values {0}\n"
		       " unicast_req_duration    300      Allowed range  [60, 1000]\n"
		       "\n");
		break;
	case G_8275_1:
		printf("\nDefault values for G.8275.1 profile are:\n\n"
		       " announceReceiptTimeout         3                     Allowed values {3}\n"
		       " clockAccuracy                  0xfe                  Allowed values {0x20, 0x21, 0xfe}\n"
		       " clockClass                     248                   Allowed values {6, 7, 135, 140, 150, 160, 165, 248}\n"
		       " clock_type                     OC                    Allowed values {OC, BC}\n"
		       " delay_mechanism                E2E                   Allowed values {E2E}\n"
		       " domainNumber                   24                    Allowed range  [24, 43]\n"
		       " G.8275.defaultDS.localPriority 128                   Allowed range  [1, UINT8_MAX]\n"
		       " G.8275.portDS.localPriority    128                   Allowed range  [1, UINT8_MAX]\n"
		       " logAnnounceInterval            -3                    Allowed values {-3}\n"
		       " logMinDelayReqInterval         -4                    Allowed values {-4}\n"
		       " logSyncInterval                -4                    Allowed values {-4}\n"
		       " masterOnly                     1                     Allowed values {0, 1}\n"
		       " maxStepsRemoved                255                   Allowed range  [1, UINT8_MAX]\n"
		       " network_transport              L2                    Allowed values {L2}\n"
		       " offsetScaledLogVariance        0xffff                Allowed values {0x4b32, 0x4e5d, 0xffff}\n"
		       " priority1                      128                   Allowed range  [0, UINT8_MAX]\n"
		       " priority2                      128                   Allowed range  [0, UINT8_MAX]\n"
		       " ptp_dst_mac                    01:1B:19:00:00:00     Allowed values {01:1B:19:00:00:00, 01:80:C2:00:00:0E}\n"
		       " slaveOnly                      0                     Allowed values {0}\n"
		       " timeSource                     0xa0                  Allowed values {0x10, 0x20, 0x30, 0x39, 0x40, 0x50, 0x60, 0x90, 0xa0}\n"
		       "\n");
		break;
	case G_8275_1_SLAVEONLY:
		printf("\nDefault values for G.8275.1 SLAVEONLY profile are:\n\n"
		       " announceReceiptTimeout         3                    Allowed values {3}\n"
		       " clockAccuracy                  0xfe                 Allowed values {0xfe}\n"
		       " clockClass                     255                  Allowed values {255}\n"
		       " clock_type                     OC                   Allowed values {OC}\n"
		       " delay_mechanism                E2E                  Allowed values {E2E}\n"
		       " domainNumber                   24                   Allowed range  [24, 43]\n"
		       " G.8275.defaultDS.localPriority 128                  Allowed range  [1, UINT8_MAX]\n"
		       " G.8275.portDS.localPriority    128                  Allowed range  [1, UINT8_MAX]\n"
		       " logAnnounceInterval            -3                   Allowed values {-3}\n"
		       " logMinDelayReqInterval         -4                   Allowed values {-4}\n"
		       " logSyncInterval                -4                   Allowed values {-4}\n"
		       " masterOnly                     0                    Allowed values {0}\n"
		       " maxStepsRemoved                255                  Allowed range  [1, UINT8_MAX]\n"
		       " network_transport              L2                   Allowed values {L2}\n"
		       " offsetScaledLogVariance        0xffff               Allowed values {0xffff}\n"
		       " priority1                      128                  Allowed values {128}\n"
		       " priority2                      255                  Allowed values {255}\n"
		       " ptp_dst_mac                    01:1B:19:00:00:00    Allowed values {01:1B:19:00:00:00, 01:80:C2:00:00:0E]\n"
		       " slaveOnly                      1                    Allowed values {1}\n"
		       " timeSource                     0xa0                 Allowed values {0x10, 0x20, 0x30, 0x39, 0x40, 0x50, 0x60, 0x90, 0xa0}\n"
		       "\n");
		break;
	case G_8275_2:
		printf("\nDefault values for G.8275.2 profile are:\n\n"
		       " announceReceiptTimeout         2         Allowed values {2}\n"
		       " clockAccuracy                  0xfe      Allowed values {0x20, 0x21, 0xfe}\n"
		       " clockClass                     248       Allowed values {6, 7, 135, 140, 150, 160, 165, 248}\n"
		       " clock_type                     OC        Allowed values {OC, BC}\n"
		       " delay_mechanism                E2E       Allowed values {E2E}\n"
		       " domainNumber                   44        Allowed range  [44, 63]\n"
		       " G.8275.defaultDS.localPriority 128       Allowed range  [1, UINT8_MAX]\n"
		       " G.8275.portDS.localPriority    128       Allowed range  [1, UINT8_MAX]\n"
		       " masterOnly                     1         Allowed values {0, 1}\n"
		       " maxStepsRemoved                255       Allowed range  [1, UINT8_MAX]\n"
		       " network_transport              UDPv4     Allowed range  [UDPv4, UDPv6]\n"
		       " offsetScaledLogVariance        0xffff    Allowed range  [0xffff]\n"
		       " priority1                      128       Allowed range  [128]\n"
		       " priority2                      128       Allowed range  [0, UINT8_MAX]\n"
		       " slaveOnly                      0         Allowed values {0}\n"
		       " timeSource                     0xa0      Allowed values {0x10, 0x20, 0x30, 0x39, 0x40, 0x50, 0x60, 0x90, 0xa0}\n"
		       " transportSpecific              0         Allowed values {0}\n"
		       "\n");
		break;
	case G_8275_2_SLAVEONLY:
		printf("\nDefault values for G.8275.2 SLAVEONLY profile are:\n\n"
		       " clockAccuracy                  0xfe      Allowed values {0xfe}\n"
		       " clockClass                     255       Allowed values {255}\n"
		       " clock_type                     OC        Allowed values {OC}\n"
		       " delay_mechanism                E2E       Allowed values {E2E}\n"
		       " domainNumber                   44        Allowed range  [44, 63]\n"
		       " G.8275.defaultDS.localPriority 128       Allowed range  [1, UINT8_MAX]\n"
		       " G.8275.portDS.localPriority    128       Allowed range  [1, UINT8_MAX]\n"
		       " masterOnly                     0         Allowed values {0}\n"
		       " maxStepsRemoved                255       Allowed range  [1, UINT8_MAX]\n"
		       " network_transport              UDPv4     Allowed range  [UDPv4, UDPv6]\n"
		       " offsetScaledLogVariance        0xffff    Allowed values {0xffff}\n"
		       " priority1                      128       Allowed values {128}\n"
		       " priority2                      255       Allowed values {255}\n"
		       " slaveOnly                      1         Allowed values {1}\n"
		       " timeSource                     0xa0      Allowed values {0x10, 0x20, 0x30, 0x39, 0x40, 0x50, 0x60, 0x90, 0xa0}\n"
		       " transportSpecific              0         Allowed values {0}\n"
		       " unicast_req_duration           300       Allowed range  [60, 1000]\n"
		       "\n");
		break;
	case E_INVALID:
	case E_OUT_OF_BOUNDS:
		printf("\nDefault values of clockClass for all profiles are:\n\n"
		       " G_8265_1_MOOC           110    Allowed range  {80, 82, 84, 86, 90, 96, 100, 102, 104, 106, 108, 110}\n"
		       " G_8265_1_SOOC           255    Allowed values {255}\n"
		       " G_8275_1                248    Allowed values {6, 7, 135, 140, 150, 160, 165, 248}\n"
		       " G_8275_1_SLAVEONLY      255    Allowed values {255}\n"
		       " G_8275_2                248    Allowed values {6, 7, 135, 140, 150, 160, 165, 248}\n"
		       " G_8275_2_SLAVEONLY      255    Allowed values {255}\n"
		       "\n");
		break;
	default:
		printf("Improper mode chosen!!\n");
		break;
	}
}

enum profile_mode get_profile_mode(const char *config,
				   struct config *cfg,
				   char *profile_type)
{
	int slave_only, clock_class, master_only;
	enum profile_mode mode = E_OUT_OF_BOUNDS;

	master_only = config_get_int(cfg, NULL, "masterOnly");
	clock_class = config_get_int(cfg, NULL, "clockClass");
	slave_only = config_get_int(cfg, NULL, "slaveOnly");

	if ((strcmp(profile_type, "g.8265.1") && strcmp(profile_type, "G.8265.1")) == 0) {
		if (slave_only == 1)
			mode = G_8265_1_SOOC;
		else
			mode = G_8265_1_MOOC;
	} else if ((strcmp(profile_type, "g.8275.1") && strcmp(profile_type, "G.8275.1")) == 0) {
		if (slave_only == 1) {
			mode = G_8275_1_SLAVEONLY;
		} else {
			mode = G_8275_1;
			if (clock_class != 248) {
				if ((master_only != 1) || (clock_class != 6))
					printf("Warning: wrong clockClass %d chosen for startup!\n", clock_class);
			}
		}
	} else if ((strcmp(profile_type, "g.8275.2") && strcmp(profile_type, "G.8275.2")) == 0) {
		if (slave_only == 1) {
			mode = G_8275_2_SLAVEONLY;
		} else {
			mode = G_8275_2;
			if (clock_class != 248) {
				if ((master_only != 1) || (clock_class != 6))
					printf("Warning: wrong clockClass %d chosen for startup!\n", clock_class);
			}
		}
	} else {
		mode = E_INVALID;
	}
	return mode;
}

int get_profile_tab(enum profile_mode mode,
		    struct config_item **profile_tab_ptr,
		    int *config_tab_size_ptr)
{
	int err = 0;

	switch (mode) {
	case G_8265_1_MOOC:
		*config_tab_size_ptr = N_G_8265_1_MASTER_CONFIG_ITEMS;
		*profile_tab_ptr = g_8265_1_config_tab_MOOC;
		break;
	case G_8265_1_SOOC:
		*config_tab_size_ptr = N_G_8265_1_SLAVE_CONFIG_ITEMS;
		*profile_tab_ptr = g_8265_1_config_tab_SOOC;
		break;
	case G_8275_1:
		*config_tab_size_ptr = N_G_8275_1_CONFIG_ITEMS;
		*profile_tab_ptr = g_8275_1_config_tab;
		break;
	case G_8275_1_SLAVEONLY:
		*config_tab_size_ptr = N_G_8275_1_SLAVEONLY_CONFIG_ITEMS;
		*profile_tab_ptr = g_8275_1_config_tab_SLAVEONLY;
		break;
	case G_8275_2:
		*config_tab_size_ptr = N_G_8275_2_CONFIG_ITEMS;
		*profile_tab_ptr = g_8275_2_config_tab;
		break;
	case G_8275_2_SLAVEONLY:
		*config_tab_size_ptr = N_G_8275_2_SLAVEONLY_CONFIG_ITEMS;
		*profile_tab_ptr = g_8275_2_config_tab_SLAVEONLY;
		break;
	case E_INVALID:
		printf("Invalid profile type and mode chosen!\n");
		printf("Check clockClass value for the profile!\n");
		err = -1;
		break;
	case E_OUT_OF_BOUNDS:
		printf("clockClass value for the profile out of bounds!\n");
		display_profile_defaults(mode);
		err = -1;
		break;
	default:
		printf("Proper mode not chosen!\n");
		err = -1;
		break;
	}

	return err;
}

void profile_validate(struct config_item *cgi,
		      struct config_item *dst,
		      bool *missing,
		      bool *range_out)
{
	struct config_enum *enum_check = NULL;
	*missing = true;
	*range_out = true;

	switch (cgi->type) {
	case CFG_TYPE_DOUBLE:
		if (dst->val.d != cgi->val.d) {
			*missing = false;
			if ((dst->val.d >= cgi->min.d) && (dst->val.d <= cgi->max.d))
				*range_out = false;
		}
		break;
	case CFG_TYPE_STRING:
		if (strcmp(dst->label, "ptp_dst_mac") == 0) {
			if (dst->val.s != NULL) {
				if ((strcmp(dst->val.s, "01:80:C2:00:00:0E") == 0) || (strcmp(dst->val.s, "01:1B:19:00:00:00") == 0)) {
					*missing = false;
					*range_out = false;
				} else if (strcmp(dst->val.s, MISSING_STR_VALUE) != 0) {
					*missing = false;
				}
			}
		}
		break;
	case CFG_TYPE_INT:
		if (dst->val.i != cgi->val.i) {
			*missing = false;
			if ((dst->val.i >= cgi->min.i) && (dst->val.i <= cgi->max.i))
				*range_out = false;
		}
		break;
	case CFG_TYPE_ENUM:
		enum_check = cgi->tab;
		while (enum_check->label != NULL) {
			if (dst->val.i == enum_check->value) {
				/* valid value is found */
				*missing = false;
				*range_out = false;
				return;
			}
			enum_check++;
		}
		if (dst->val.i != MISSING_ENUM_VALUE) {
			/* value out of range */
			*missing = false;
		}
		break;
	default:
		pr_err("Invalid type!!\n");
		break;
	}
}


static int get_global_item_status(bool missing, bool range_out, char const *label)
{
	int ret = 0;

	if (missing == true) {
		ret = -1;
		printf("Global item %s missing!\n\n", label);
	} else {
		if (range_out == true) {
			ret = -1;
			printf("Global item %s out of range!\n\n", label);
		} else {
			pr_debug("Global item %s is valid\n\n", label);
		}
	}
	return ret;
}

static int get_port_item_status_global_section(bool missing, bool range_out, char const *label)
{
	int ret = 0;

	if (missing == true) {
		ret = -1;
		printf("Port item %s missing!\n\n", label);
	} else {
		if (range_out == true) {
			ret = -1;
			printf("Port item %s out of range in the global section!\n\n", label);
		} else {
			pr_debug("Port item %s is valid in the global section\n\n", label);
		}
	}
	return ret;
}

static int get_port_item_status_port_section(bool range_out, char const *label, char const *ifname)
{
	int ret = 0;

	if (range_out) {
		ret = -1;
		printf("Port item %s out of range in the port section %s!\n\n", label, ifname);
	} else {
		pr_debug("Port item %s is valid in the port section %s\n\n", label, ifname);
	}
	return ret;
}

static int get_port_item_status_both_sections(bool missing, bool range_out, char const *label, char const *ifname)
{
	int ret = 0;

	if (missing) {
		ret = -1;
		printf("Port item %s missing from both global and port section %s!\n\n", label, ifname);
	} else if (range_out) {
		ret = -1;
		printf("Port item %s missing from port section %s and out of range in global section!\n\n", label, ifname);
	} else {
		pr_debug("Port item %s is valid in the global section\n\n", label);
	}
	return ret;
}

int profile_range_fail_check(struct config *cfg,
			     struct config_item *profile_tab,
			     int config_tab_size,
			     enum profile_mode mode)
{
	int err = 0, j;
	bool global_missing, port_missing;
	bool global_range_out, port_range_out;
	struct config_item *cgi, *dst;
	struct interface *iface;
	const char *ifname;

	for (j = 0; j < config_tab_size; j++) {
		cgi = &profile_tab[j];

		dst = config_find_item(cfg, NULL, cgi->label);
		profile_validate(cgi, dst, &global_missing, &global_range_out);

		if ((cgi->flags & CFG_ITEM_PORT) == 0) {
			/* not a port item */
			err = get_global_item_status(global_missing, global_range_out, dst->label);
			if (err)
				goto end;
		}

		if (((cgi->flags & CFG_ITEM_PORT) == CFG_ITEM_PORT) && (cfg->n_interfaces == 0)) {
			/* no interfaces defined in the .cfg file */
			err = get_port_item_status_global_section(global_missing, global_range_out, dst->label);
			if (err)
				goto end;
		}

		if (((cgi->flags & CFG_ITEM_PORT) == CFG_ITEM_PORT) && (cfg->n_interfaces != 0)) {
			/* call profile_validate for each interface */
			STAILQ_FOREACH(iface, &cfg->interfaces, list) {
				ifname = interface_name(iface);
				dst = config_find_item(cfg, ifname, cgi->label);
				profile_validate(cgi, dst, &port_missing, &port_range_out);

				if (!global_missing) {
					err = get_port_item_status_global_section(global_missing, global_range_out, dst->label);
					if (err)
						goto end;
				}

				if (!port_missing) {
					/* item found in port section */
					err = get_port_item_status_port_section(port_range_out, dst->label, ifname);
				} else {
					/* item missing in port section */
					err = get_port_item_status_both_sections(global_missing, global_range_out, dst->label, ifname);
				}
				if (err)
					goto end;
			}
		}
	}

end:
	if (err) {
		display_profile_defaults(mode);
		return err;
	}
	return err;
}

int main(int argc, char *argv[])
{
	char *config = NULL, *profile_type = NULL, *progname;
	int c, j, err = -1, print_level, config_tab_size;
	struct config_item *ci, *profile_tab;
	struct config *cfg;
	enum profile_mode mode;

	if (handle_term_signals())
		return -1;

	cfg = config_create();
	if (!cfg)
		return -1;

	/* Process the command line arguments. */
	progname = strrchr(argv[0], '/');
	progname = progname ? 1+progname : argv[0];

	while ((c = getopt(argc, argv, "f:p:l:hm")) != -1) {
		switch (c) {
		case 'f':
			config = optarg;
			break;
		case 'p':
			profile_type = optarg;
			break;
		case 'l':
			if (get_arg_val_i(c, optarg, &print_level,
					  PRINT_LEVEL_MIN, PRINT_LEVEL_MAX))
				goto out;
			config_set_int(cfg, "logging_level", print_level);
			break;
		case 'm':
			config_set_int(cfg, "verbose", 1);
			break;
		case 'h':
			usage(progname);
			err = 0;
			goto out;
		case '?':
			usage(progname);
			goto out;
		default:
			usage(progname);
			goto out;
		}
	}

	if (argc == 1) {
		fprintf(stderr, "no options specified\n");
		usage(progname);
		goto out;
	}

	if (config == NULL) {
		fprintf(stderr, "Config file not provided!! Exiting..\n");
		goto out;
	}

	if (profile_type == NULL) {
		fprintf(stderr, "Profile type not provided!! Exiting..\n");
		goto out;
	}

	c = config_read(config, cfg);
	if (config && (c != 0)) {
		err = c;
		goto out;
	}

	print_set_progname(progname);
	print_set_level(config_get_int(cfg, NULL, "logging_level"));
	print_set_verbose(config_get_int(cfg, NULL, "verbose"));

	mode = get_profile_mode(config, cfg, profile_type);

	err = get_profile_tab(mode, &profile_tab, &config_tab_size);
	if (err)
		goto out;

	/**
	 * Modify config_tab[] with profile specific range values and
	 * seed with bad values for detecting missing parameter from .cfg file
	 */
	for (j = 0; j < config_tab_size; j++) {
		ci = &profile_tab[j];

		if (ci->type == CFG_TYPE_STRING)
			pr_debug("global.%s = %s\n", ci->label, ci->val.s);
		else
			pr_debug("global.%s = %d\n", ci->label, ci->val.i);

		if (ci->type == CFG_TYPE_STRING) {
			err = profile_config_set_string(cfg, ci->label, ci->val.s);
			if (err)
				goto out;
		} else {
			err = profile_config_set_section_int(cfg, NULL, ci->label, ci->val.i);
			if (err)
				goto out;

			err = config_set_ranged_int(cfg, NULL, ci->label, ci->min.i, ci->max.i);
			if (err)
				goto out;
		}
	}

	/* Destroy previous configuration after getting clockClass */
	profile_config_destroy(cfg);

	cfg = config_create();
	if (!cfg)
		return -1;

	set_profile_specific_range(cfg, mode);

	/* config_read to make sure that the new values are read properly */
	err = config_read(config, cfg);
	if (err) {
		display_profile_defaults(mode);
		goto out;
	}

	err = profile_range_fail_check(cfg, profile_tab, config_tab_size, mode);

	restore_profile_specific_range(cfg, mode);

	if (err)
		goto out;

	printf("%s Checked successfully for %s %d profile\n\n", config, profile_type, mode);
	pr_debug("filename = %s, profile = %s\n", config, profile_type);

out:
	profile_config_destroy(cfg);
	return err;
}
