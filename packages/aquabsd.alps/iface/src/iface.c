// TODO
//  - usage information
//  - manual page
//  - write proper tests
//  - integrate with dhclient

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <iface.h>

#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void __dead2 usage(void) {
	fprintf(stderr,
		"usage: %1$s [-hv]\n"
		"       %1$s [-dhv] -i id\n"
		"       %1$s [-dhv] -n filename\n"
		"       %1$s [-hv] -l [-i id] [-n filename] [-m modname]\n"
		"       %1$s [-hv] -u [-i id] [-n filename] [-m modname]\n",
	getprogname());

	exit(EXIT_FAILURE);
}

typedef struct {
	iface_t* ifaces;
	size_t   iface_len;

	iface_t* iface;
	iface_flag_t flag;
} opts_t;

static inline int __stat_iface(opts_t* opts, iface_t* iface) {
	// get flags

	iface_flag_t flag;

	if (iface_get_flags(iface, &flag) < 0) {
		errx(EXIT_FAILURE, "iface_get_flags: %s", iface_error_str());
	}

	printf("%s\t%s\t%s\n", iface->name, iface_get_inet(iface), flag & IFACE_UP ? "up" : "down");
	return 0;
}

static int do_stat(opts_t* opts) {
	if (opts->iface) {
		return __stat_iface(opts, opts->iface);
	}

	for (int i = 0; i < opts->iface_len; i++) {
		iface_t* iface = &opts->ifaces[i];
		__stat_iface(opts, iface);
	}

	return 0;
}

static int do_set_flag(opts_t* opts) {
	if (!opts->iface) {
		err(EXIT_FAILURE, "You must provide an interface with the -i option to set its flags");
	}

	if (iface_set_flags(opts->iface, opts->flag) < 0) {
		errx(EXIT_FAILURE, "iface_set_flags: %s", iface_error_str());
	}

	return 0;
}

typedef int (*action_t) (opts_t* opts);

static inline void __find_iface(opts_t* opts, char* name) {
	for (int i = 0; i < opts->iface_len; i++) {
		iface_t* iface = &opts->ifaces[i];

		if (strcmp(iface->name, name)) {
			continue;
		}

		opts->iface = iface;
		return;
	}

	errx(EXIT_FAILURE, "Couldn't find interface %s", name);
}

static inline void __set_flag(opts_t* opts, char sign, char* flag) {
	#define FLAG(cmp, _flag) else if (cmp) opts->flag |= sign * (_flag);

	// TODO all the ones with a negative sign here don't seem to work

	if (0);

	FLAG(*flag == 'u', +IFACE_UP)
	FLAG(*flag == 'd', -IFACE_UP)

	FLAG(*flag == 'a', -IFACE_NOARP)
	FLAG(*flag == 'g', +IFACE_DEBUG)
	FLAG(*flag == '0', +IFACE_LINK0)
	FLAG(*flag == '1', +IFACE_LINK1)
	FLAG(*flag == '2', +IFACE_LINK2)

	else {
		errx(EXIT_FAILURE, "Couldn't find flag %s\n", flag);
	}

	#undef FLAG
}

int main(int argc, char* argv[]) {
	// options

	action_t action = do_stat;
	opts_t opts = { 0 };

	// get options

	char* name = NULL;

	int c;

	while ((c = getopt(argc, argv, "a:r:i:")) >= 0) {
		// general options

		if (c == 'i') {
			name = optarg;
		}

		else if (c == 'a') {
			action = do_set_flag;
			__set_flag(&opts, +1, optarg);
		}

		else if (c == 'r') {
			action = do_set_flag;
			__set_flag(&opts, -1, optarg);
		}

		else {
			usage();
		}
	}

	argc -= optind;
	argv += optind;

	// list interfaces, and search for the one with the passed name if 'name != NULL'

	if (iface_list(&opts.ifaces, &opts.iface_len) < 0) {
		errx(EXIT_FAILURE, "iface_list: %s", iface_error_str());
	}

	if (name) {
		__find_iface(&opts, name);
	}

	// take action

	int rv = action(&opts);

	iface_free(opts.ifaces, opts.iface_len);

	return rv < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}