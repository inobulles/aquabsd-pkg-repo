#if !defined(__AQUABSD__SETTINGS)
#define __AQUABSD__SETTINGS

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

typedef enum {
	SETTINGS_PRIVILEGE_BOOT, // variables settable by the bootloader
	SETTINGS_PRIVILEGE_KERN, // kernel state variables (this and 'SETTINGS_PRIVILEGE_BOOT' equate to sysctl)
	SETTINGS_PRIVILEGE_USER, // user-specific settings (usually stored in '$HOME/.settings')
	SETTINGS_PRIVILEGE_AQUA, // aqua-specific settings (usually stored in '$AQUAROOT/conf/settings')
} settings_privilege_t;

// for the operations which support it, the priority at which a setting will be propagated across the GrapeVine network
// this is only applicable for settings with privilege 'SETTINGS_PRIVILEGE_AQUA'

typedef enum {
	SETTINGS_PRIORITY_NEVER,
	SETTINGS_PRIORITY_SOMETIME,
	SETTINGS_PRIORITY_ASAP,
	SETTINGS_PRIORITY_IMMEDIATE,
} settings_priority_t;

typedef enum {
	// these are "raw" settings, which aren't really meant for users to touch
	// rather, its specialized programs which will know how to deal with this data
	// these programs could either be users of libsettings, or the underlying sysctl library (e.g., 'ps', 'systat', 'netstat')

	SETTINGS_TYPE_OPAQUE,

	// mirror 'CTLTYPE_*' from the sysctl MIB
	// (these are still used by non-MIB settings, it's just the reason why there are so many different (read: redundant) types)

	SETTINGS_TYPE_NODE,
	SETTINGS_TYPE_STRING,

	SETTINGS_TYPE_INT,
	SETTINGS_TYPE_UINT,
	SETTINGS_TYPE_LONG,
	SETTINGS_TYPE_ULONG,

	SETTINGS_TYPE_S8,
	SETTINGS_TYPE_S16,
	SETTINGS_TYPE_S32,
	SETTINGS_TYPE_S64,

	SETTINGS_TYPE_U8,
	SETTINGS_TYPE_U16,
	SETTINGS_TYPE_U32,
	SETTINGS_TYPE_U64,

	// some much more specific aquaBSD- & architechture-specific types

	SETTINGS_TYPE_CLOCKINFO,
	SETTINGS_TYPE_TIMEVAL,
	SETTINGS_TYPE_LOADAVG,
	SETTINGS_TYPE_VMTOTAL,
	SETTINGS_TYPE_INPUT_ID,
	SETTINGS_TYPE_PAGESIZES,

#if defined(__amd64__)
	SETTINGS_TYPE_EFI_MAP_HEADER,
#endif

#if defined(__amd64__) || defined(__i386__)
	SETTINGS_TYPE_BIOS_SMAP_XATTR,
#endif
} settings_type_t;

typedef struct {
	// general fields

	char* key;
	char* descr;

	size_t len;
	void* data;

	// properties of the setting

	settings_privilege_t privilege;
	settings_type_t type;
	bool writeable; // some settings are writeable at runtime, some not

	// 'sysctl' specific stuff

	size_t oid_len;
	int* oid;
} setting_t;

char* settings_error_str(void);

// allocate a flat list of 'setting_t*' objects in 'settings_ref' & 'settings_len_ref'
// 'privilege' specifies which setting privilege level we're targetting
// for emulating sysctl functionality, use either 'SETTINGS_PRIVILEGE_BOOT' or 'SETTINGS_PRIVILEGE_KERN'
// 'user' specifies which user to run this as (only applicable if 'privilege' is either 'SETTINGS_PRIVILEGE_USER' or 'SETTINGS_PRIVILEGE_AQUA')
// 'user == -1' uses the current effective user

int settings_list(setting_t*** settings_ref, size_t* settings_len_ref, settings_privilege_t privilege, int user);

// read the data from a 'setting_t' object in 'data_ref' & 'len_ref'

int setting_read(setting_t* setting);

// write the data in 'data' & 'len' to a 'setting_t' object

int setting_write(setting_t* setting, void* data, size_t len, settings_priority_t priority);

// create a new setting with key 'key', privilege 'privilege', & type 'type'
// created settings are always tuneable
// 'SETTINGS_PRIVILEGE_BOOT' & 'SETTINGS_PRIVILEGE_KERN' settings are provided by the bootloader & kernel, and thus are not privileges you can create settings at

int setting_create(char* key, settings_privilege_t privilege, settings_type_t type, settings_priority_t priority);

// remove a setting from its 'setting_t' object
// 'SETTINGS_PRIVILEGE_BOOT' & 'SETTINGS_PRIVILEGE_KERN' settings are provided by the bootloader & kernel, and thus can not be removed

int setting_remove(setting_t* setting, settings_priority_t priority);

// free a 'setting_t' object

int setting_free(setting_t* setting);

#endif