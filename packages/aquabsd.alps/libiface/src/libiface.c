#include <iface.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include <sys/ioctl.h>

static char* error_str = NULL;

char* iface_error_str(void) {
	return error_str;
}

static inline int __emit_error(char* fmt, char* data) {
	if (error_str) {
		free(error_str);
	}

	if (data) {
		asprintf(&error_str, fmt, data);
	}

	else {
		error_str = strdup(fmt);
	}

	return -1;
}

int iface_list(iface_t** ifaces_ref, size_t* iface_len_ref) {
	// check references

	if (!ifaces_ref) {
		return __emit_error("ifaces_ref argument points to NULL", NULL);
	}

	if (!iface_len_ref) {
		return __emit_error("iface_len_ref argument points to NULL", NULL);
	}

	// get interfaces

	struct ifaddrs* ifaddr;

	if (getifaddrs(&ifaddr) < 0) {
		return __emit_error("getifaddrs failed: %s", strerror(errno));
	}

	// create dynamic list

	int iface_len = 0;
	iface_t* ifaces = NULL;

	// traverse through network interfaces linked list

	for (struct ifaddrs* ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
		if (!ifa->ifa_addr) {
			continue;
		}

		sa_family_t family = ifa->ifa_addr->sa_family;

		if (family != AF_LINK) { // we're looking for link interfaces here
			continue;
		}

		// get interface type

		struct sockaddr_dl* sdl = (void*) ifa->ifa_addr;
		iface_kind_t kind = IFACE_KIND_OTHER;

		if (sdl->sdl_type == IFT_LOOP) {
			kind = IFACE_KIND_LOOP;
		}

		else if (sdl->sdl_type == IFT_USB) {
			kind = IFACE_KIND_USB;
		}

		else if (sdl->sdl_type == IFT_ETHER) {
			kind = IFACE_KIND_ETH;
		}

		else {
			fprintf(stderr, "TODO iface_list: unknown interface type for %s: %d\n", ifa->ifa_name, sdl->sdl_type);
		}

		// create interface object

		ifaces = realloc(ifaces, ++iface_len * sizeof *ifaces);

		iface_t* iface = &ifaces[iface_len - 1];
		memset(iface, 0, sizeof *iface); // TODO yup, 'realloc' still doesn't zero-out memory for us... I should make that a thing actually

		iface->kind = kind;

		strncpy(iface->ifr.ifr_name, ifa->ifa_name, sizeof iface->ifr.ifr_name);
		iface->name = iface->ifr.ifr_name;

		iface->ifr.ifr_addr.sa_family = AF_INET;
		iface->dgram_sock = socket(iface->ifr.ifr_addr.sa_family, SOCK_DGRAM, 0);

		if (iface->dgram_sock < 0) {
			__emit_error("failed to create datagram socket for interface: %s", strerror(errno));

			iface_free(ifaces, iface_len);
			return -1;
		}
	}

	// set our references

	*ifaces_ref = ifaces;
	*iface_len_ref = iface_len;

	return 0;
}

void iface_free(iface_t* ifaces, size_t iface_len) {
	for (int i = 0; i < iface_len; i++) {
		iface_t* iface = &ifaces[i];

		if (iface->dgram_sock) {
			close(iface->dgram_sock);
		}
	}

	free(ifaces);
}

static inline int __get_flags(iface_t* iface, struct ifreq* ifr, iface_flag_t* flag_ref) {
	// check references

	if (!ifr) {
		return __emit_error("ifr argument points to NULL", NULL);
	}

	if (!flag_ref) {
		return __emit_error("flag_ref argument points to NULL", NULL);
	}

	// make private copy of 'ifr' because 'SIOCGIFLAGS' scribbles on its union portion

	memcpy(ifr, &iface->ifr, sizeof *ifr);

	// get flags

	if (ioctl(iface->dgram_sock, SIOCGIFFLAGS, (caddr_t) ifr) < 0) {
		return __emit_error("SIOCGIFFLAGS ioctl failed: %s", strerror(errno));
	}

	*flag_ref = ifr->ifr_flags;
	return 0;
}

int iface_get_flags(iface_t* iface, iface_flag_t* flag_ref) {
	struct ifreq ifr;
	return __get_flags(iface, &ifr, flag_ref);
}

int iface_set_flags(iface_t* iface, int val) {
	struct ifreq ifr;
	iface_flag_t curr_flags;

	if (__get_flags(iface, &ifr, &curr_flags) < 0) {
		return -1;
	}

	// if the given flag value is negative, we wanna *remove* the flag

	if (val < 0) {
		curr_flags &= ~(-val);
	}

	else {
		curr_flags |= val;
	}

	ifr.ifr_flags = curr_flags & 0xFFFF;

	// set flags

	if (ioctl(iface->dgram_sock, SIOCSIFFLAGS, (caddr_t) &ifr) < 0) {
		return __emit_error("SIOCSIFFLAGS ioctl failed: %s", strerror(errno));
	}

	iface->ifr.ifr_flags = ifr.ifr_flags;
	return 0;
}

char* iface_get_inet(iface_t* iface) {
	return "127.0.0.1"; // TODO
}