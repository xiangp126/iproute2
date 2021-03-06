/*
 * iplink_macvlan.c	macvlan/macvtap device support
 *
 *              This program is free software; you can redistribute it and/or
 *              modify it under the terms of the GNU General Public License
 *              as published by the Free Software Foundation; either version
 *              2 of the License, or (at your option) any later version.
 *
 * Authors:     Patrick McHardy <kaber@trash.net>
 *		Arnd Bergmann <arnd@arndb.de>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <linux/if_link.h>
#include <linux/if_ether.h>

#include "rt_names.h"
#include "utils.h"
#include "ip_common.h"

#define pfx_err(lu, ...) {               \
	fprintf(stderr, "%s: ", lu->id); \
	fprintf(stderr, __VA_ARGS__);    \
	fprintf(stderr, "\n");           \
}

static void print_explain(struct link_util *lu, FILE *f)
{
	fprintf(f,
		"Usage: ... %s mode MODE [flag MODE_FLAG] MODE_OPTS\n"
		"MODE: private | vepa | bridge | passthru | source\n"
		"MODE_FLAG: null | nopromisc\n"
		"MODE_OPTS: for mode \"source\":\n"
		"\tmacaddr { { add | del } <macaddr> | set [ <macaddr> [ <macaddr>  ... ] ] | flush }\n",
		lu->id
	);
}

static void explain(struct link_util *lu)
{
	print_explain(lu, stderr);
}


static int mode_arg(const char *arg)
{
	fprintf(stderr,
		"Error: argument of \"mode\" must be \"private\", \"vepa\", \"bridge\", \"passthru\" or \"source\", not \"%s\"\n",
		arg);
	return -1;
}

static int flag_arg(const char *arg)
{
	fprintf(stderr,
		"Error: argument of \"flag\" must be \"nopromisc\" or \"null\", not \"%s\"\n",
		arg);
	return -1;
}

static int macvlan_parse_opt(struct link_util *lu, int argc, char **argv,
			  struct nlmsghdr *n)
{
	__u32 mode = 0;
	__u16 flags = 0;
	__u32 mac_mode = 0;
	int has_flags = 0;
	char mac[ETH_ALEN];
	struct rtattr *nmac;

	while (argc > 0) {
		if (matches(*argv, "mode") == 0) {
			NEXT_ARG();

			if (strcmp(*argv, "private") == 0)
				mode = MACVLAN_MODE_PRIVATE;
			else if (strcmp(*argv, "vepa") == 0)
				mode = MACVLAN_MODE_VEPA;
			else if (strcmp(*argv, "bridge") == 0)
				mode = MACVLAN_MODE_BRIDGE;
			else if (strcmp(*argv, "passthru") == 0)
				mode = MACVLAN_MODE_PASSTHRU;
			else if (strcmp(*argv, "source") == 0)
				mode = MACVLAN_MODE_SOURCE;
			else
				return mode_arg(*argv);
		} else if (matches(*argv, "flag") == 0) {
			NEXT_ARG();

			if (strcmp(*argv, "nopromisc") == 0)
				flags |= MACVLAN_FLAG_NOPROMISC;
			else if (strcmp(*argv, "null") == 0)
				flags |= 0;
			else
				return flag_arg(*argv);

			has_flags = 1;

		} else if (matches(*argv, "macaddr") == 0) {
			NEXT_ARG();

			if (strcmp(*argv, "add") == 0) {
				mac_mode = MACVLAN_MACADDR_ADD;
			} else if (strcmp(*argv, "del") == 0) {
				mac_mode = MACVLAN_MACADDR_DEL;
			} else if (strcmp(*argv, "set") == 0) {
				mac_mode = MACVLAN_MACADDR_SET;
			} else if (strcmp(*argv, "flush") == 0) {
				mac_mode = MACVLAN_MACADDR_FLUSH;
			} else {
				explain(lu);
				return -1;
			}

			addattr32(n, 1024, IFLA_MACVLAN_MACADDR_MODE, mac_mode);

			if (mac_mode == MACVLAN_MACADDR_ADD ||
			    mac_mode == MACVLAN_MACADDR_DEL) {
				NEXT_ARG();

				if (ll_addr_a2n(mac, sizeof(mac),
						*argv) != ETH_ALEN)
					return -1;

				addattr_l(n, 1024, IFLA_MACVLAN_MACADDR, &mac,
					  ETH_ALEN);
			}

			if (mac_mode == MACVLAN_MACADDR_SET) {
				nmac = addattr_nest(n, 1024,
						    IFLA_MACVLAN_MACADDR_DATA);
				while (NEXT_ARG_OK()) {
					NEXT_ARG_FWD();

					if (ll_addr_a2n(mac, sizeof(mac),
							*argv) != ETH_ALEN) {
						PREV_ARG();
						break;
					}

					addattr_l(n, 1024, IFLA_MACVLAN_MACADDR,
						  &mac, ETH_ALEN);
				}
				addattr_nest_end(n, nmac);
			}
		} else if (matches(*argv, "nopromisc") == 0) {
			flags |= MACVLAN_FLAG_NOPROMISC;
			has_flags = 1;
		} else if (matches(*argv, "help") == 0) {
			explain(lu);
			return -1;
		} else {
			pfx_err(lu, "unknown option \"%s\"?", *argv);
			explain(lu);
			return -1;
		}
		argc--, argv++;
	}

	if (mode)
		addattr32(n, 1024, IFLA_MACVLAN_MODE, mode);

	if (has_flags) {
		if (flags & MACVLAN_FLAG_NOPROMISC &&
		    mode != MACVLAN_MODE_PASSTHRU) {
			pfx_err(lu, "nopromisc flag only valid in passthru mode");
			explain(lu);
			return -1;
		}
		addattr16(n, 1024, IFLA_MACVLAN_FLAGS, flags);
	}
	return 0;
}

static void macvlan_print_opt(struct link_util *lu, FILE *f, struct rtattr *tb[])
{
	__u32 mode;
	__u16 flags;
	__u32 count;
	unsigned char *addr;
	int len;
	struct rtattr *rta;

	if (!tb)
		return;

	if (!tb[IFLA_MACVLAN_MODE] ||
	    RTA_PAYLOAD(tb[IFLA_MACVLAN_MODE]) < sizeof(__u32))
		return;

	mode = rta_getattr_u32(tb[IFLA_MACVLAN_MODE]);
	fprintf(f, "mode %s ",
		  mode == MACVLAN_MODE_PRIVATE ? "private"
		: mode == MACVLAN_MODE_VEPA    ? "vepa"
		: mode == MACVLAN_MODE_BRIDGE  ? "bridge"
		: mode == MACVLAN_MODE_PASSTHRU  ? "passthru"
		: mode == MACVLAN_MODE_SOURCE  ? "source"
		:				 "unknown");

	if (!tb[IFLA_MACVLAN_FLAGS] ||
	    RTA_PAYLOAD(tb[IFLA_MACVLAN_FLAGS]) < sizeof(__u16))
		flags = 0;
	else
		flags = rta_getattr_u16(tb[IFLA_MACVLAN_FLAGS]);

	if (flags & MACVLAN_FLAG_NOPROMISC)
		fprintf(f, "nopromisc ");

	/* in source mode, there are more options to print */

	if (mode != MACVLAN_MODE_SOURCE)
		return;

	if (!tb[IFLA_MACVLAN_MACADDR_COUNT] ||
	    RTA_PAYLOAD(tb[IFLA_MACVLAN_MACADDR_COUNT]) < sizeof(__u32))
		return;

	count = rta_getattr_u32(tb[IFLA_MACVLAN_MACADDR_COUNT]);
	fprintf(f, "remotes (%d) ", count);

	if (!tb[IFLA_MACVLAN_MACADDR_DATA])
		return;

	rta = RTA_DATA(tb[IFLA_MACVLAN_MACADDR_DATA]);
	len = RTA_PAYLOAD(tb[IFLA_MACVLAN_MACADDR_DATA]);

	for (; RTA_OK(rta, len); rta = RTA_NEXT(rta, len)) {
		if (rta->rta_type != IFLA_MACVLAN_MACADDR ||
		    RTA_PAYLOAD(rta) < 6)
			continue;
		addr = RTA_DATA(rta);
		fprintf(f, "%.2x:%.2x:%.2x:%.2x:%.2x:%.2x ", addr[0],
			addr[1], addr[2], addr[3], addr[4], addr[5]);
	}
}

static void macvlan_print_help(struct link_util *lu, int argc, char **argv,
	FILE *f)
{
	print_explain(lu, f);
}

struct link_util macvlan_link_util = {
	.id		= "macvlan",
	.maxattr	= IFLA_MACVLAN_MAX,
	.parse_opt	= macvlan_parse_opt,
	.print_opt	= macvlan_print_opt,
	.print_help	= macvlan_print_help,
};

struct link_util macvtap_link_util = {
	.id		= "macvtap",
	.maxattr	= IFLA_MACVLAN_MAX,
	.parse_opt	= macvlan_parse_opt,
	.print_opt	= macvlan_print_opt,
	.print_help	= macvlan_print_help,
};
