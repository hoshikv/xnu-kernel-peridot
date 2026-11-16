/*
 * The board's addresses (sys/hakios.board.h): board_<block>=0x... on the command line, else the phone's
 * own built-in defaults (POCO F6 / Redmi Turbo 3 "peridot" SM8635 when /arm-io/device_type is "peridot",
 * otherwise POCO X5 Pro "redwood" SM7325). Read where a driver maps its block, so that one kernel serves
 * every phone whose SoC has the same blocks without overflowing XNU's 607-character command line.
 *
 * The names, and the drivers that ask for them (redwood SM7325 / peridot SM8635):
 *   epss       cpufreq: the CPU frequency domains (qcom,cpufreq-epss "freq-domain0")      0x18591000 / 0x17d91000
 *   cmd_db     cpufreq: RPMh's command database                                         0x80860000 / 0x81c60000
 *   rsc        cpufreq: the apps RSC (RPMh votes)                                       0x18220000 / 0x17a20000
 *   tsens0/1/2 thermal: the TM regions of the TSENS blocks                              0x0c263000 / 0x0c222000 ...
 *   gcc        display: the global clock controller                                     0x00100000
 *   dispcc     display: the display clock controller                                    0x0af00000
 *   intf       display: the DSI panel's timing engine (mdp_intf_base)                   0x0ae36000
 *   ufshc      ufs: the UFS host controller ("ufs_mem")                                 0x01d84000
 *   spmi_core, spmi_cnfg, spmi_obsrvr   buttons: the PMIC arbiter                       0x0c440000 ... / 0x0c400000 ...
 */
#include <libkern/libkern.h>
#include <pexpert/device_tree.h>
#include <pexpert/pexpert.h>
#include <sys/hakios.board.h>
#include <sys/systm.h>

static bool
board_is_peridot(void)
{
	static int cached = -1;
	DTEntry entry;
	void const *prop;
	unsigned int size;

	if (cached >= 0) {
		return cached != 0;
	}
	cached = (SecureDTLookupEntry(NULL, "/arm-io", &entry) == kSuccess &&
	    SecureDTGetProperty(entry, "device_type", &prop, &size) == kSuccess &&
	    size >= 7 && memcmp(prop, "peridot", 7) == 0) ? 1 : 0;
	return cached != 0;
}

static bool
peridot_addr_default(const char *name, vm_offset_t *out)
{
	static const struct { const char *name; vm_offset_t value; } table[] = {
		{ "epss",        0x17d91000UL },
		{ "cmd_db",      0x81c60000UL },
		{ "rsc",         0x17a20000UL },
		{ "tsens0",      0x0c222000UL },
		{ "tsens1",      0x0c223000UL },
		{ "tsens2",      0x0c224000UL },
		{ "smem",        0x81d00000UL },
		{ "aoss",        0x0c300000UL },
		{ "ipcc",        0x00406000UL },
		{ "tlmm",        0x0f100000UL },
		{ "wpss_mem",    0x9bb00000UL },
		{ "wifi_iova",   0xb0000000UL },
		{ "wifi_msi",    0x17110040UL },
		{ "gcc",         0x00100000UL },
		{ "dispcc",      0x0af00000UL },
		{ "intf",        0x0ae36000UL },
		{ "ufshc",       0x01d84000UL },
		{ "spmi_core",   0x0c400000UL },
		{ "spmi_cnfg",   0x0c42d000UL },
		{ "spmi_obsrvr", 0x0c440000UL },
		{ "bt_uart",     0x00888000UL },
	};

	for (unsigned i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
		if (strcmp(name, table[i].name) == 0) {
			*out = table[i].value;
			return true;
		}
	}
	return false;
}

static bool
peridot_number_default(const char *name, uint32_t *out)
{
	static const struct { const char *name; uint32_t value; } table[] = {
		{ "small_cores", 0x07U },
		{ "prime_core",  7U },
		{ "wpss_size",   0x01900000U },
		{ "wifi_stream", 0x1400U },
		{ "gpu_chip",    0x43030b00U },
		{ "wlan_reset",  35U },
		{ "bt_sw",       80U },
		{ "bt_reset",    156U },
	};

	for (unsigned i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
		if (strcmp(name, table[i].name) == 0) {
			*out = table[i].value;
			return true;
		}
	}
	return false;
}

static const char *
peridot_text_default(const char *name)
{
	static const struct { const char *name; const char *value; } table[] = {
		{ "wifi_bdf",      "n16t" },
		{ "wifi_rail_cx",  "smpb3" },
		{ "wifi_rail_xo",  "smpb1" },
		{ "wifi_rail_rfa", "smpb2" },
		{ "wifi_pmic1",    "PMXR2230" },
		{ "wifi_pmic2",    "PM8550VS" },
	};

	for (unsigned i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
		if (strcmp(name, table[i].name) == 0) {
			return table[i].value;
		}
	}
	return NULL;
}

vm_offset_t
hakios_board_addr(const char *name, vm_offset_t dflt)
{
	char arg[40] = "board_";
	uint64_t value = 0;
	vm_offset_t peridot_val = 0;

	strlcat(arg, name, sizeof(arg));
	if (PE_parse_boot_argn(arg, &value, sizeof(value)) && value != 0) {
		return (vm_offset_t)value;
	}
	if (board_is_peridot() && peridot_addr_default(name, &peridot_val)) {
		return peridot_val;
	}
	return dflt;
}

uint32_t
hakios_board_number(const char *name, uint32_t dflt)
{
	char arg[40] = "board_";
	uint32_t value = 0;
	uint32_t peridot_val = 0;

	strlcat(arg, name, sizeof(arg));
	if (PE_parse_boot_argn(arg, &value, sizeof(value))) {
		return value;
	}
	if (board_is_peridot() && peridot_number_default(name, &peridot_val)) {
		return peridot_val;
	}
	return dflt;
}

void
hakios_board_text(const char *name, const char *dflt, char *value, uint32_t size)
{
	char arg[40] = "board_";
	const char *base = dflt;

	if (size == 0) {
		return;
	}
	if (board_is_peridot()) {
		const char *p = peridot_text_default(name);
		if (p != NULL) {
			base = p;
		}
	}
	strlcpy(value, base, size);
	strlcat(arg, name, sizeof(arg));
	(void)PE_parse_boot_argn(arg, value, (int)size);
}

void
hakios_board_report(void)
{
	static const char *const names[] = { "epss", "cmd_db", "rsc", "tsens0", "tsens1", "tsens2", "smem", "aoss", "ipcc",
		                             "tlmm", "wpss_mem", "wifi_iova", "wifi_msi", "gcc", "dispcc", "intf", "ufshc", "spmi_core",
		                             "spmi_cnfg", "spmi_obsrvr", "bt_uart" };
	char bdf[20];
	const char *dflt_bdf = board_is_peridot() ? "n16t" : "m20gl";
	int given = 0;

	for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
		char arg[40] = "board_";
		uint64_t a = 0;

		strlcat(arg, names[i], sizeof(arg));
		if (PE_parse_boot_argn(arg, &a, sizeof(a)) && a != 0) {
			printf("board: %s at %#lx (from the command line)\n", names[i], (unsigned long)a);
			given++;
		}
	}
	if (given == 0) {
		if (board_is_peridot()) {
			printf("board: using POCO F6 (peridot, SM8635) defaults for omitted blocks\n");
		} else {
			printf("board: no board_ addresses given: the POCO X5 Pro's (SM7325) for every block\n");
		}
	}
	hakios_board_text("wifi_bdf", dflt_bdf, bdf, sizeof(bdf));
	if (strcmp(bdf, dflt_bdf) != 0) {
		printf("board: Wi-Fi board data variant %s\n", bdf);
	}
}
