// heavily based off of aquabsd-core/sbin/kldstat/kldstat.c

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <libutil.h>

#include <sys/param.h> // TODO sys/linker.h not including all the stuff it needs

#include <sys/module.h>
#include <sys/linker.h>

static void __dead2 usage(void) {
	fprintf(stderr,
		"usage: %1$s [-hv] [-l filename]\n"
		"       %1$s [-hv] [-u modname]\n",
	getprogname());

	exit(1);
}

typedef struct {
	int verbose;
	int humanize;
	char* filename;
} opts_t;

#define PTR_WIDTH ((int) (sizeof(void*) * 2 + 2))

static void print_mod(opts_t* opts, int id) {
	struct module_stat stat = {
		.version = sizeof stat,
	};

	if (modstat(id, &stat) < 0) {
		warn("can't stat module id %d", id);
		return;
	}

	// TODO analog to showdata option

	printf("\t%3d %s\n", stat.id, stat.name);
}

static void print_file(opts_t* opts, int id) {
	struct kld_file_stat stat = {
		.version = sizeof stat,
	};

	if (kldstat(id, &stat) < 0) {
		err(1, "can't stat file id %d", id);
	}

	if (opts->humanize) {
		char buf[5];
		humanize_number(buf, sizeof buf, stat.size, "", HN_AUTOSCALE, HN_DECIMAL | HN_NOSPACE);

		printf("%2d %4d %*p %5s %s", stat.id, stat.refs, PTR_WIDTH, stat.address, buf, stat.name);
	}

	else {
		printf("%2d %4d %*p %8zx %s", stat.id, stat.refs, PTR_WIDTH, stat.address, stat.size, stat.name);
	}

	if (opts->verbose) {
		// print out what that module contains

		printf(" (%s)\n", stat.pathname);
		printf("\t id name\n");

		for (int mod = kldfirstmod(id); mod; mod = modfnext(mod)) {
			print_mod(opts, mod);
		}
	}

	else {
		printf("\n");
	}
}

static int do_list(opts_t* opts) {
	char* fmt = opts->humanize ?
		"id refs addr%*c %8s name\n" :
		"id refs addr%*c %11s name\n";

	printf(fmt, PTR_WIDTH - 7, ' ', "size");

	for (int id = kldnext(0); id; id = kldnext(id)) {
		print_file(opts, id);
	}

	return 0;
}

typedef enum {
	ACTION_LIST,
	ACTION_STAT,
	ACTION_LOAD,
	ACTION_UNLOAD
} action_t;

int main(int argc, char* argv[]) {
	// options

	action_t action = ACTION_LIST;
	
	opts_t opts = {
		.verbose = 0,
		.humanize = 0,
		.filename = NULL,
	};

	// get options

	int c;

	while ((c = getopt(argc, argv, "hf:l:su:v")) != -1) {
		if (c == 'h') {
			opts.humanize = 1;
		}

		else if (c == 'l') {
			action = ACTION_LOAD;
			opts.filename = optarg;
		}

		else if (c == 's') {
			action = ACTION_STAT;
		}

		else if (c == 'u') {
			action = ACTION_UNLOAD;
			opts.filename = optarg;
		}

		else if (c == 'v') {
			opts.verbose = 1;
		}

		else {
			usage();
		}
	}

	argc -= optind;
	argv += optind;

	// take action

	if (action == ACTION_LIST) {
		return do_list(&opts);
	}

	// if (action == ACTION_STAT) {
	// 	return do_stat();
	// }

	// if (action == ACTION_LOAD) {
	// 	return do_load();
	// }

	// if (action == ACTION_UNLOAD) {
	// 	return do_unload();
	// }

	return 1;
}