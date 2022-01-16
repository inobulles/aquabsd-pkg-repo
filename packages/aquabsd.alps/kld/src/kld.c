// heavily based off of:
//  - aquabsd-core/sbin/kldstat/kldstat.c
//  - aquabsd-core/sbin/kldstat/kldload.c

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <libutil.h>

#include <sys/param.h> // TODO sys/linker.h not including all the stuff it needs
#include <sys/stat.h>

#include <sys/linker.h>
#include <sys/module.h>
#include <sys/sysctl.h>

#define PATHCTL "kern.module_path"

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

	int id;
	char* file;
	char* mod;
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

	printf("%3d %s\n", stat.id, stat.name);
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
		printf(" (%s)\n", stat.pathname);
	}

	else {
		printf("\n");
	}
}

static inline int __do_list_mods(opts_t* opts) {
	int file_id = -1;

	if (opts->id > -1) {
		file_id = opts->id;
	}

	if (file_id < 0) {
		return -1;
	}

	printf(" id name\n");

	for (int mod = kldfirstmod(file_id); mod; mod = modfnext(mod)) {
		print_mod(opts, mod);
	}

	return 0;
}

static inline int __do_list_files(opts_t* opts) {
	char* fmt = opts->humanize ?
		"id refs addr%*c %8s name\n" :
		"id refs addr%*c %11s name\n";

	printf(fmt, PTR_WIDTH - 7, ' ', "size");

	for (int id = kldnext(0); id; id = kldnext(id)) {
		print_file(opts, id);
	}

	return 0;
}

static int do_stat(opts_t* opts) {
	if (opts->file) {
		opts->id = kldfind(opts->file);

		if (opts->id < 0) {
			warnx("can't find file %s", opts->file);
			return -1;
		}
	}

	if (opts->id > -1) {
		return __do_list_mods(opts);
	}

	if (opts->mod) {
		usage();
	}

	return __do_list_files(opts);
}

static int do_load(opts_t* opts) {
	char* name = opts->file;

	// check path

	if (strchr(name, '/')) {
		goto path_validated;
	}

	if (!strstr(name, ".ko")) {
		goto path_validated;
	}

	struct stat stat_;

	if (stat(name, &stat_) < 0) {
		goto path_validated;
	}

	// for later

	dev_t dev = stat_.st_dev;
	ino_t ino = stat_.st_ino;

	// process $PATHCTL sysctl

	int mib[5];
	size_t mib_len = nitems(mib);

	if (sysctlnametomib(PATHCTL, mib, &mib_len) < 0) {
		err(1, "sysctlnametomib(%s)", PATHCTL);
	}

	size_t path_len;

	if (sysctl(mib, mib_len, NULL, &path_len, NULL, 0) < 0) {
		err(1, "getting path: sysctl(%s) - size only", PATHCTL);
	}

	char* path = malloc(path_len + 1);

	if (sysctl(mib, mib_len, path, &path_len, NULL, 0) < 0) {
		err(1, "getting path: sysctl(%s)", PATHCTL);
	}

	char* tmp = path;
	int found = 0;

	char* elem;

	while ((elem = strsep(&tmp, ";"))) {
		char kld_path[strlen(elem) + 1];
		strlcpy(kld_path, elem, sizeof kld_path);

		// add slash if there isn't one already

		if (kld_path[strlen(kld_path) - 1] != '/') {
			strlcat(kld_path, "/", sizeof kld_path);
		}

		strlcat(kld_path, name, sizeof kld_path);

		if (stat(kld_path, &stat_) < 0) {
			continue;
		}

		found = 1;

		if (stat_.st_dev != dev || stat_.st_ino != ino) {
			warnx("%s will be loaded from %s, not the current directory", name, elem);
		}

		break;
	}

	free(path);

	if (!found) {
		warnx("%s is not in the module path", name);
		return -1;
	}

path_validated: {} // I still don't understand why labels can't have a declaration after them 😄

	// all good, actually load the file

	int id = kldload(name);

	if (id > -1) {
		if (opts->verbose) {
			printf("Loaded %s, id=%d\n", name, id);
		}

		return 0;
	}

	// something went wrong

	if (errno == EEXIST) {
		warnx("can't load %s: module already loaded or in kernel\n", name);
	}

	else if (errno == ENOEXEC) {
		warnx("an error occurred while loading %s; please check dmesg(8) for more details", name);
	}

	else {
		warn("can't load %s", name);
	}

	return -1;
}

typedef enum {
	ACTION_STAT,
	ACTION_LOAD,
	ACTION_UNLOAD
} action_t;

int main(int argc, char* argv[]) {
	// options

	action_t action = ACTION_STAT;
	
	opts_t opts = {
		.verbose = 0,
		.humanize = 0,

		.id = -1,
		.file = NULL,
		.mod = NULL,
	};

	// get options

	int c;

	while ((c = getopt(argc, argv, "hi:f:lm:uv")) != -1) {
		// general options
		
		if (c == 'h') {
			opts.humanize = 1;
		}

		else if (c == 'v') {
			opts.verbose = 1;
		}

		// action options

		else if (c == 'l') {
			action = ACTION_LOAD;
		}

		else if (c == 'u') {
			action = ACTION_UNLOAD;
		}

		// id/filename/modulename-passing options

		else if (c == 'i') {
			char* inval;
			opts.id = strtoul(optarg, &inval, 10);

			if (*inval) {
				usage();
			}
		}

		else if (c == 'f') {
			opts.file = optarg;
		}

		else if (c == 'm') {
			opts.mod = optarg;
		}

		else {
			usage();
		}
	}

	argc -= optind;
	argv += optind;

	// take action

	int rv = 0; // success

	if (action == ACTION_STAT) {
		rv = do_stat(&opts);
	}

	else if (action == ACTION_LOAD) {
		rv = do_load(&opts);
	}

	// else if (action == ACTION_UNLOAD) {
	// 	rv = do_unload();
	// }

	return rv < 0 ? 1 : 0;
}