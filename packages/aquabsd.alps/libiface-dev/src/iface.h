#if !defined(__AQUABSD__IFACE)
#define __AQUABSD__IFACE

#include <ifaddrs.h>

#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_types.h>

#include <netinet/in.h>
#include <arpa/inet.h>

// types

typedef enum {
	IFACE_KIND_OTHER, // could be some kind of WLAN interface, haven't yet looked into that
	IFACE_KIND_LOOP,  // loopback
	IFACE_KIND_ETH,   // ethernet
	IFACE_KIND_USB,   // USB
} iface_kind_t;

typedef struct {
	char* name;
	iface_kind_t kind;

	int dgram_sock;
	struct ifreq ifr;
} iface_t;

// this really serves no purpose aside from making the API more cohesive

typedef enum {
	#define MAP(name) IFACE_##name = IFF_##name,

	MAP(UP)
	MAP(BROADCAST)
	MAP(DEBUG)
	MAP(LOOPBACK)
	MAP(POINTOPOINT)
	MAP(KNOWSEPOCH)
	MAP(DRV_RUNNING)
	MAP(NOARP)
	MAP(PROMISC)
	MAP(ALLMULTI)
	MAP(DRV_OACTIVE)
	MAP(SIMPLEX)
	MAP(LINK0)
	MAP(LINK1)
	MAP(LINK2)
	MAP(ALTPHYS)
	MAP(MULTICAST)
	MAP(CANTCONFIG)
	MAP(PPROMISC)
	MAP(MONITOR)
	MAP(STATICARP)
	MAP(DYING)
	MAP(RENAMING)
	MAP(NOGROUP)

	#undef MAP
} iface_flag_t;

// function prototypes

char* iface_error_str(void);

int  iface_list(iface_t** ifaces_ref, size_t* iface_len_ref);
void iface_free(iface_t*  ifaces,     size_t  iface_len);

int iface_get_flags(iface_t* iface, iface_flag_t* flag_ref);
int iface_set_flags(iface_t* iface, int val);

char* iface_get_inet(iface_t* iface);

#endif