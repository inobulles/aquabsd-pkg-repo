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

		printf("%s\n", setting->key);

		setting_free(setting);
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

	while ((c = getopt(argc, argv, "bku")) >= 0) {
		// general options

		// privilege options

		if (c == 'b') {
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