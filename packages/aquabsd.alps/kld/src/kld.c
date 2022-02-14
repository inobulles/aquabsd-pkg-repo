// for an alternative to the kldconfig command (i.e. to configure the kernel module search path), use the kern.module_path sysctl(8):
// % sysctl kern.module_path

// heavily based off of:
//  - aquabsd-core/sbin/kldstat/kldstat.c
//  - aquabsd-core/sbin/kldload/kldload.c
//  - aquabsd-core/sbin/kldunload/kldunload.c

// TODO
//  - analog to kldstat's showdata option
//  - proper usage information
//  - manual page
//  - write proper tests (create mock modules?)

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
	int humanize;
	int force;
	int verbose;

	int id;
	char* file;
	char* mod;

	// path stuff

	size_t path_len;
	char* path;

	size_t mib_len;
	int mib[5];
} opts_t;

static inline void __get_mib(opts_t* opts) {
	if (opts->mib_len) {
		return;
	}

	opts->mib_len = nitems(opts->mib);

	if (sysctlnametomib(PATHCTL, opts->mib, &opts->mib_len) < 0) {
		err(EXIT_FAILURE, "sysctlnametomib(%s)", PATHCTL);
	}
}

static inline void __get_path(opts_t* opts) {
	if (opts->path) {
		free(opts->path);
	}

	__get_mib(opts);

	if (sysctl(opts->mib, opts->mib_len, NULL, &opts->path_len, NULL, 0) < 0) {
		err(EXIT_FAILURE, "getting path: sysctl(%s) - size only", PATHCTL);
	}

	opts->path = malloc(opts->path_len + 1);

	if (sysctl(opts->mib, opts->mib_len, opts->path, &opts->path_len, NULL, 0) < 0) {
		err(EXIT_FAILURE, "getting path: sysctl(%s)", PATHCTL);
	}
}

static int compute_id(opts_t* opts) {
	if (!opts->file) {
		return 0;
	}

	opts->id = kldfind(opts->file);

	if (opts->id < 0) {
		warnx("can't find file %s", opts->file);
		return -1;
	}

	return 0;
}

#define PTR_WIDTH ((int) (sizeof(void*) * 2 + 2))

static void print_mod(opts_t* opts, int id) {
	struct module_stat stat = {
		.version = sizeof stat,
	};

	if (modstat(id, &stat) < 0) {
		warn("can't stat module id %d", id);
		return;
	}

	printf("%3d %s\n", stat.id, stat.name);
}

static void print_file(opts_t* opts, int id) {
	struct kld_file_stat stat = {
		.version = sizeof stat,
	};

	if (kldstat(id, &stat) < 0) {
		err(EXIT_FAILURE, "can't stat file id %d", id);
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
	if (compute_id(opts) < 0) {
		return -1;
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

	if (!name) {
		usage();
	}

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

	__get_path(opts);

	char* tmp = opts->path;
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

	if (!found) {
		warnx("%s is not in module search path", name);
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

static int do_unload(opts_t* opts) {
	if (compute_id(opts) < 0) {
		return -1;
	}

	if (opts->id < 0) {
		usage();
	}

	if (opts->verbose) {
		struct kld_file_stat stat = {
			.version = sizeof stat,
		};

		if (kldstat(opts->id, &stat) < 0) {
			warnx("can't stat file id %d", opts->id);
			return -1;
		}

		printf("Unloading %s, id=%d\n", stat.name, opts->id);
	}

	int force = opts->force ?
		LINKER_UNLOAD_FORCE :
		LINKER_UNLOAD_NORMAL;

	if (kldunloadf(opts->id, force) < 0) {
		warnx("can't unload file id %d", opts->id);
		return -1;
	}

	return 0;
}

typedef int (*action_t) (opts_t* opts);

int main(int argc, char* argv[]) {
	// options

	action_t action = do_stat;
	
	opts_t opts = { 0 };
	opts.id = -1;

	// get options

	int c;

	while ((c = getopt(argc, argv, "a:d:fhi:lm:n:ruv")) != -1) {
		// general options

		if (c == 'f') {
			opts.force = 1;
		}

		else if (c == 'h') {
			opts.humanize = 1;
		}

		else if (c == 'v') {
			opts.verbose = 1;
		}

		// action options

		else if (c == 'l') {
			action = do_load;
		}

		else if (c == 'u') {
			action = do_unload;
		}

		// id/filename/modulename-passing options

		else if (c == 'i') {
			char* inval;
			opts.id = strtoul(optarg, &inval, 10);

			if (*inval) {
				usage();
			}
		}

		else if (c == 'n') {
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

	return action(&opts) < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}