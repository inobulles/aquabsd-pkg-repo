#include <settings.h>

#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <err.h>

#include <sys/sysctl.h>

static inline setting_t* __list_add(setting_t*** settings_ref, size_t* settings_len_ref) {
	size_t settings_len = *settings_len_ref;

	setting_t** settings = realloc(*settings_ref, (settings_len + 1) * sizeof *settings);

	setting_t* setting = calloc(1, sizeof *setting);
	settings[settings_len++] = setting;

	*settings_ref = settings;
	*settings_len_ref = settings_len;

	return setting;
}

static inline int __query_sysctl(query, oid, oid_len, buf, buf_len_ref)
	int query;
	size_t oid_len;
	int oid[oid_len];
	void* buf;
	size_t* buf_len_ref;
{
	int name[CTL_MAXNAME + 2] = {
		CTL_SYSCTL,
		query,
	};

	memcpy(name + 2, oid, oid_len);

	if (sysctl(name, oid_len + 2, buf, buf_len_ref, 0, 0) < 0) {
		errx(EXIT_FAILURE, "sysctl(query %d): %s", query, strerror(errno));
	}

	return 0;
}

static inline int __list_sysctl_fill(settings_ref, settings_len_ref, privilege, oid_len, oid)
	setting_t*** settings_ref;
	size_t* settings_len_ref;
	settings_privilege_t privilege;
	size_t oid_len;
	int oid[oid_len];
{
	// get name of sysctl from OID

	char name[BUFSIZ];
	size_t name_len = sizeof name;

	if (__query_sysctl(CTL_SYSCTL_NAME, oid, oid_len, name, &name_len) < 0) {
		return -1;
	}

	// get sysctl OID format
	// make sure it's a sysctl kind we actually care about, based on our privilege

	uint8_t buf[BUFSIZ];
	size_t buf_len = sizeof buf;

	if (__query_sysctl(CTL_SYSCTL_OIDFMT, oid, oid_len, buf, &buf_len) < 0) {
		return -1;
	}

	unsigned kind = *(unsigned*) buf;
	char* fmt = (void*) (buf + sizeof kind);

	bool tuneable = !(kind & CTLFLAG_TUN);

	if (privilege == SETTINGS_PRIVILEGE_BOOT && !tuneable) {
		return 0;
	}

	if (privilege == SETTINGS_PRIVILEGE_KERN && tuneable) {
		return 0;
	}

	int type = kind & CTLTYPE;

	// actually add and fill in the setting

	setting_t* setting = __list_add(settings_ref, settings_len_ref);

	if (!setting) {
		return -1;
	}

	setting->key = strdup(name);
	setting->privilege = privilege;
	setting->tuneable = tuneable;

	// fill in type field of setting

	setting->type = SETTINGS_TYPE_OPAQUE;

	#define SETTING_TYPE(name) \
		else if (type == CTLTYPE_##name) { \
			setting->type = SETTINGS_TYPE_##name; \
		}

	if (type == CTLTYPE_OPAQUE) {
		#define OPAQUE_SETTING_TYPE(name, settings_type) \
			else if (strcmp(fmt, (name)) == 0) { \
				setting->type = SETTINGS_TYPE_##settings_type; \
			}

		if (0) {}

		OPAQUE_SETTING_TYPE("S,clockinfo", CLOCKINFO)
		OPAQUE_SETTING_TYPE("S,timeval",   TIMEVAL)
		OPAQUE_SETTING_TYPE("S,loadavg",   LOADAVG)
		OPAQUE_SETTING_TYPE("S,vmtotal",   VMTOTAL)
		OPAQUE_SETTING_TYPE("S,input_id",  INPUT_ID)
		OPAQUE_SETTING_TYPE("S,pagesizes", PAGESIZES)

	#if defined(__amd64__)
		OPAQUE_SETTING_TYPE("S,efi_map_header", EFI_MAP_HEADER)
	#endif

	#if defined(__amd64__) || defined(__i386__)
		OPAQUE_SETTING_TYPE("S,bios_smap_xattr", BIOS_SMAP_XATTR)
	#endif

		#undef OPAQUE_SETTING_TYPE
	}

	SETTING_TYPE(STRING)

	SETTING_TYPE(INT)
	SETTING_TYPE(UINT)
	SETTING_TYPE(LONG)
	SETTING_TYPE(ULONG)

	SETTING_TYPE(S8)
	SETTING_TYPE(S16)
	SETTING_TYPE(S32)
	SETTING_TYPE(S64)

	SETTING_TYPE(U8)
	SETTING_TYPE(U16)
	SETTING_TYPE(U32)
	SETTING_TYPE(U64)

	// anything else will already have been assigned to 'SETTINGS_TYPE_OPAQUE' previously

	#undef SETTING_TYPE

	return -1;
}

static inline int __list_sysctl(setting_t*** settings_ref, size_t* settings_len_ref, settings_privilege_t privilege) {
	// taking heavily from 'sysctl_all' in 'sbin/sysctl/sysctl.c'

	int name[22] = {
		CTL_SYSCTL,
		CTL_SYSCTL_NEXT, // TODO difference with CTL_SYSCTL_NEXTNOSKIP?
		CTL_KERN,
	};

	// TODO make sure all this * & / by 'sizeof(int)' is correct here because this seems sus
	//      AFAICT, 'oid_len' should be byte count because 'sysctl's 'oldp' (is this a typo for 'oidp'?) expects an untyped pointer

	size_t name_len = 3;

	while (1) {
		int oid[22];
		size_t oid_len = sizeof oid;

		if (sysctl(name, name_len, oid, &oid_len, 0, 0) < 0) {
			if (errno == ENOENT) {
				break;
			}

			errx(EXIT_FAILURE, "sysctl(getnext) %zu", oid_len);
		}

		if (oid_len < 0) {
			break;
		}

		if (__list_sysctl_fill(settings_ref, settings_len_ref, privilege, oid_len, oid) < 0) {
			return -1;
		}

		memcpy(name + 2, oid, oid_len);
		name_len += 2 + oid_len / sizeof(int);
	}

	return 0;
}

int settings_list(setting_t*** settings_ref, size_t* settings_len_ref, settings_privilege_t privilege, int user) {
	int rv = -1;

	setting_t** settings = NULL;
	size_t settings_len = 0;

	if (privilege == SETTINGS_PRIVILEGE_BOOT || privilege == SETTINGS_PRIVILEGE_KERN) {
		if (__list_sysctl(&settings, &settings_len, privilege) < 0) {
			goto error;
		}
	}

	// success;

	rv = 0;

error:

	*settings_ref = settings;
	*settings_len_ref = settings_len;

	return rv;
}

int setting_read(setting_t* setting, void** data_ref, size_t* len_ref) {
	return -1; // TODO
}

int setting_write(setting_t* setting, void* data, size_t len, settings_priority_t priority) {
	return -1; // TODO
}

int setting_create(char* key, settings_privilege_t privilege, settings_type_t type, settings_priority_t priority) {
	return -1; // TODO
}

int setting_remove(setting_t* setting, settings_priority_t priority) {
	return -1; // TODO
}

int setting_free(setting_t* setting) {
	if (!setting) {
		return -1;
	}

	if (setting->key) {
		free(setting->key);
	}

	return 0;
}