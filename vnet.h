#ifndef LNET_H_
#define LNET_H_ 1

#include <pthread.h>
#include "routing.h"


typedef struct virtual_net_interface {
	int fd;
	char *ifname;
	pthread_mutex_t wlock;

	pthread_rwlock_t m_lock;
	ether_addr_t mac;
} vnet_t;

/* initializes the tap device named `ifname' */
int vnet_init(vnet_t *vn, char *ifname);

/* send packet to the tap device */
int vnet_send(vnet_t *vn, void *packet, size_t size);

/* read a packet from the tap device (vnet thread only) */
int vnet_recv(vnet_t *nd, void *buf, size_t *nbyte);

/* return the mtu of the vnet device */
int vnet_get_mtu(vnet_t *vn);

/* return the current vnet address */
ether_addr_t vnet_get_mac(vnet_t *vn);

/* set vnet address */
int vnet_set_mac(vnet_t *vn, ether_addr_t mac);

#endif
