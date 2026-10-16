#ifndef NET_H
#define NET_H

#include <3ds.h>

bool net_init(void);
void net_exit(void);

/* Host: create a local wireless network and wait for one player. */
bool net_host_start(void);
/* Client: scan once and join the first Tetris network found.
 * Returns 1 when connected, 0 when nothing was found, -1 on error. */
int  net_join_try(void);

/* True while another console is on the network with us. */
bool net_peer_connected(void);

void   net_send(const void *buf, size_t size);
/* Returns the size of a received packet, or 0 when none is waiting. */
size_t net_recv(void *buf, size_t size);

/* Leave or tear down whatever network we're on. */
void net_close(void);

#endif
