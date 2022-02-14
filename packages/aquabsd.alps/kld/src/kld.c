// heavily based off of:
//  - aquabsd-core/sbin/kldstat/kldstat.c
//  - aquabsd-core/sbin/kldload/kldload.c
//  - aquabsd-core/sbin/kldunload/kldunload.c
//  - aquabsd-core/sbin/kldconfig/kldconfig.c

// TODO
//  - analog to kldstat's showdata option
//  - implement kldconfig's functionality 
//  - proper usage information
//  - manual page
//  - write proper tests (create mock modules?)
//  - how necessary are the -U & -m options from kldconfig?
//  - now that I think of it, I don't think the kldconfig stuff is very important here... you can just use 'sysctl kern.module_path'

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
#include <sys/queue.h>
#include <sys/stat.h>

#include <sys/linker.h>
#include <sys/module.h>
#include <sys/sysctl.h>

#define PATHCTL "kern.module_path"

TAILQ_HEAD(head_t, entry_t);
typedef struct head_t head_t;

typedef struct entry_t {
	char* path;
	TAILQ_ENTRY(entry_t) next;
} entry_t;

static void __dead2 usage(void) {
	fprintf(stderr,
		"usage: %1$s [-hv] [-l filename]\n"
		"       %1$s [-hv] [-u modname]\n",
	getprogname());

	exit(1);
}

typedef struct {
	int dry;
	int humanize;
	int insert;
	int force;
	int unique;
	int verbose;

	int id;
	char* file;
	char* mod;

	// path stuff

	char* mod_path;

	char* prev_path;
	int changed;
	
	head_t path_q;

	size_t path_len;
	char* path;

	size_t mib_len;
	int mib[5];
} opts_t;

static char* q_str(head_t* path_q) {
	char* str = strdup("");

	entry_t* entry;

	TAILQ_FOREACH(entry, path_q, next) {
		char* delim = TAILQ_NEXT(entry, next) ?
			";" : "";

		char* new;
		asprintf(&new, "%s%s%s", str, entry->path, delim);

		free(str);
		str = new;
	}

	return str;
}

static void get_mib(opts_t* opts) {
	if (opts->mib_len) {
		return;
	}

	opts->mib_len = nitems(opts->mib);

	if (sysctlnametomib(PATHCTL, opts->mib, &opts->mib_len) < 0) {
		err(EXIT_FAILURE, "sysctlnametomib(%s)", PATHCTL);
	}
}

static void get_path(opts_t* opts) {
	if (opts->path) {
		free(opts->path);
	}

	get_mib(opts);

	if (sysctl(opts->mib, opts->mib_len, NULL, &opts->path_len, NULL, 0) < 0) {
		err(EXIT_FAILURE, "getting path: sysctl(%s) - size only", PATHCTL);
	}

	opts->path = malloc(opts->path_len + 1);

	if (sysctl(opts->mib, opts->mib_len, opts->path, &opts->path_len, NULL, 0) < 0) {
		err(EXIT_FAILURE, "getting path: sysctl(%s)", PATHCTL);
	}
}

static void set_path(opts_t* opts) {
	get_mib(opts);

	char* new = q_str(&opts->path_q);
	size_t len = strlen(new) + 1;

	if (sysctl(opts->mib, opts->mib_len, NULL, NULL, new, len) < 0) {
		err(EXIT_FAILURE, "setting path: sysctl(%s)", PATHCTL);
	}

	if (opts->path) {
		free(opts->path);
	}

	opts->path = new;
}

static char* mod_path_buf(opts_t* opts, char* path) {
	char* buf = realpath(path, NULL);
	
	// as explained in kldconfig.c:
	// If the path exists, use it; otherwise, take the user-specified path at face value - may be a removed directory.

	if (!buf) {
		size_t bytes = strlen(path) + 1;

		buf = malloc(bytes);
		strncpy(buf, path, bytes - 1);
	}

	size_t len = strlen(buf);

	// if present, remove terminated path delimiter

	if (len > 0 && buf[len - 1] == '/') {
		buf[--len] = '\0';
	}

	return buf;
}

static void __add_path(opts_t* opts, char* path, int force, int insert) {
	char* buf = mod_path_buf(opts, path);

	// make sure the path isn't already in the queue

	entry_t* entry;

	TAILQ_FOREACH(entry, &opts->path_q, next) {
		printf("tailq entry %s\n", entry->path);

		if (!strcmp(entry->path, buf)) {
			break;
		}
	}

	printf("%s %p\n", buf, entry);

	if (entry) {
		if (force) {
			return;
		}

		errx(EXIT_FAILURE, "already in module search path (%s)", buf);
	}

	// all's good, add it

	entry = malloc(sizeof *entry);
	entry->path = strdup(buf);

	if (!insert) {
		TAILQ_INSERT_TAIL(&opts->path_q, entry, next);
		goto change;
	}

	// TODO I'm not sure I understand the stuff in addpath in kldconfig.c
	//      there's a chance there's a mistake in their for loop (?)

	entry_t* skip = TAILQ_FIRST(&opts->path_q);

	if (skip) {
		TAILQ_INSERT_BEFORE(skip, entry, next);
		goto change;
	}

	TAILQ_INSERT_TAIL(&opts->path_q, entry, next);

change:

	opts->changed = 1;
}

static void __del_path(opts_t* opts, char* path, int force) {
	char* buf = mod_path_buf(opts, path);

	// make sure the path isn't already in the queue

	entry_t* entry;

	TAILQ_FOREACH(entry, &opts->path_q, next) {
		if (!strcmp(entry->path, buf)) {
			break;
		}
	}

	if (!entry) {
		if (force) {
			return;
		}

		errx(EXIT_FAILURE, "not in module search path (%s)", buf);
	}

	// all's good, delete it

	TAILQ_REMOVE(&opts->path_q, entry, next);
	opts->changed = 1;
}

static void parse_path(opts_t* opts) {
	get_path(opts);
	opts->prev_path = strdup(opts->path);

	char* elem;

	while ((elem = strsep(&opts->path, ";"))) {
		if (opts->unique) {
			__add_path(opts, elem, 1, 0);
			continue;
		}

		entry_t* entry = malloc(sizeof *entry);
		entry->path = strdup(elem);

		TAILQ_INSERT_TAIL(&opts->path_q, entry, next);
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

	get_path(opts);

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

static int do_path_add(opts_t* opts) {
	TAILQ_INIT(&opts->path_q);

	parse_path(opts);

	__add_path(opts, opts->mod_path, opts->force, opts->insert);

	return 0;
}

static int do_path_del(opts_t* opts) {
	TAILQ_INIT(&opts->path_q);

	parse_path(opts);

	__del_path(opts, opts->mod_path, opts->force);

	return 0;
}

static int do_path_show(opts_t* opts) {
	TAILQ_INIT(&opts->path_q);

	parse_path(opts);

	char* str = q_str(&opts->path_q);
	printf("%s\n", PATHCTL);

	free(str);
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

	while ((c = getopt(argc, argv, "a:d:fhIi:lm:Nn:rUuv")) != -1) {
		// general options

		if (c == 'f') {
			opts.force = 1;
		}

		else if (c == 'h') {
			opts.humanize = 1;
		}

		else if (c == 'I') {
			opts.insert = 1;
		}

		else if (c == 'N') {
			opts.dry = 1;
		}

		else if (c == 'U') {
			opts.unique = 1;
		}

		else if (c == 'v') {
			opts.verbose = 1;
		}

		// action options

		else if (c == 'a') {
			action = do_path_add;
			opts.mod_path = optarg;
		}

		else if (c == 'd') {
			action = do_path_del;
			opts.mod_path = optarg;
		}

		else if (c == 'l') {
			action = do_load;
		}

		else if (c == 'u') {
			action = do_unload;
		}

		else if (c == 'r') {
			action = do_path_show;
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

	int rv = action(&opts);

	if (rv < 0) {
		return EXIT_FAILURE;
	}

	// if changed & verbose flag, show our original & new paths

	if (opts.changed && opts.verbose) {
		do_path_show(&opts);
	}

	// commit changes if there are any & we're not doing a dryrun

	if (opts.dry || !opts.changed) {
		return EXIT_SUCCESS;
	}

	set_path(&opts);
	return EXIT_SUCCESS;
}