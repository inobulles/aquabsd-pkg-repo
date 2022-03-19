// TODO
//  - usage information
//  - manual page
//  - write proper tests

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <settings.h>

#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static void __dead2 usage(void) {
	fprintf(stderr,
		"usage: %1$s [idk yet man lol]\n",
	getprogname());

	exit(EXIT_FAILURE);
}

typedef struct {
	// general options

	bool verbose;
	bool binary;

	// action options

	char* read;

	// privilege & user options

	settings_privilege_t privilege;
	int user;
} opts_t;

static int do_list(opts_t* opts) {
	setting_t** settings;
	size_t settings_len;

	if (settings_list(&settings, &settings_len, opts->privilege, opts->user) < 0) {
		errx(EXIT_FAILURE, "settings_list: %s", settings_error_str());
	}

	for (int i = 0; i < settings_len; i++) {
		setting_t* setting = settings[i];

		// if verbose option set, print out setting description too

		if (opts->verbose) {
			if (setting_read_descr(setting) < 0) {
				errx(EXIT_FAILURE, "setting_read_descr: %s", settings_error_str());
			}

			printf("%s (%s)\n", setting->key, setting->descr);
		}

		else {
			printf("%s\n", setting->key);
		}

		setting_free(setting);
	}

	return 0;
}

static int do_read(opts_t* opts) {
	setting_t* setting = settings_search(opts->read, opts->privilege, opts->user);

	if (!setting) {
		errx(EXIT_FAILURE, "settings_search: %s", settings_error_str());
	}

	if (setting->privilege != opts->privilege) {
		errx(EXIT_FAILURE, "settings_search: Incorrect privileges");
	}

	if (opts->verbose && setting_read_descr(setting) < 0) {
		errx(EXIT_FAILURE, "setting_read_descr: %s", settings_error_str());
	}

	if (setting_read(setting) < 0) {
		errx(EXIT_FAILURE, "setting_read: %s", settings_error_str());
	}

	if (opts->binary) {
		fwrite(setting->data, 1, setting->len, stdout);
		return 0;
	}

	if (opts->verbose) {
		printf("%s: ", setting->descr);
	}

	if (setting->type == SETTINGS_TYPE_STRING) {
		printf("%s\n", (char*) setting->data);
	}

	else {
		errx(EXIT_FAILURE, "Unsupported type: %d", setting->type);
	}

	return 0;
}

typedef int (*action_t) (opts_t* opts);

int main(int argc, char* argv[]) {
	// options

	action_t action = do_list;

	opts_t opts = {
		.privilege = SETTINGS_PRIVILEGE_AQUA,
		.user = -1,
	};

	// get options

	int c;

	while ((c = getopt(argc, argv, "bBkr:uv")) >= 0) {
		// general options

		if (c == 'B') {
			opts.binary = true;
		}

		else if (c == 'v') {
			opts.verbose = true;
		}

		// action options

		else if (c == 'r') {
			opts.read = optarg;
			action = do_read;
		}

		// privilege options

		else if (c == 'b') {
			opts.privilege = SETTINGS_PRIVILEGE_BOOT;
		}

		else if (c == 'k') {
			opts.privilege = SETTINGS_PRIVILEGE_KERN;
		}

		else if (c == 'u') {
			opts.privilege = SETTINGS_PRIVILEGE_USER;
		}

		else {
			usage();
		}
	}

	argc -= optind;
	argv += optind;

	// take action

	int rv = action(&opts);

	return rv < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}