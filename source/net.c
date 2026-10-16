#include "net.h"
#include <stdlib.h>
#include <string.h>

#define WLAN_COMM_ID 0x54335444u /* arbitrary, unique to this game */
#define DATA_CHANNEL 1
#define PASSPHRASE   "tetris3ds-coop"
#define SCAN_BUFSIZE 0x4000

static bool initialized;
static bool hosting;
static bool joined;
static udsBindContext bindctx;

bool net_init(void)
{
	if (initialized)
		return true;
	if (R_FAILED(udsInit(0x3000, NULL)))
		return false;
	initialized = true;
	return true;
}

void net_exit(void)
{
	net_close();
	if (initialized)
		udsExit();
	initialized = false;
}

bool net_host_start(void)
{
	udsNetworkStruct network;
	udsGenerateDefaultNetworkStruct(&network, WLAN_COMM_ID, 0, 2);
	Result rc = udsCreateNetwork(&network, PASSPHRASE, strlen(PASSPHRASE) + 1,
	                             &bindctx, DATA_CHANNEL, UDS_DEFAULT_RECVBUFSIZE);
	if (R_FAILED(rc))
		return false;
	hosting = true;
	return true;
}

int net_join_try(void)
{
	void *buf = malloc(SCAN_BUFSIZE);
	if (!buf)
		return -1;
	memset(buf, 0, SCAN_BUFSIZE);

	udsNetworkScanInfo *networks = NULL;
	size_t total = 0;
	Result rc = udsScanBeacons(buf, SCAN_BUFSIZE, &networks, &total,
	                           WLAN_COMM_ID, 0, NULL, false);
	free(buf);
	if (R_FAILED(rc)) {
		free(networks);
		return -1;
	}

	int ret = 0;
	for (size_t i = 0; i < total && ret == 0; i++) {
		if (networks[i].network.total_nodes >= networks[i].network.max_nodes)
			continue;
		rc = udsConnectNetwork(&networks[i].network, PASSPHRASE, strlen(PASSPHRASE) + 1,
		                       &bindctx, UDS_BROADCAST_NETWORKNODEID, UDSCONTYPE_Client,
		                       DATA_CHANNEL, UDS_DEFAULT_RECVBUFSIZE);
		if (R_SUCCEEDED(rc)) {
			joined = true;
			ret = 1;
		}
	}
	free(networks);
	return ret;
}

bool net_peer_connected(void)
{
	if (!hosting && !joined)
		return false;
	udsConnectionStatus status;
	if (R_FAILED(udsGetConnectionStatus(&status)))
		return false;
	return status.total_nodes >= 2;
}

void net_send(const void *buf, size_t size)
{
	if (!hosting && !joined)
		return;
	udsSendTo(UDS_BROADCAST_NETWORKNODEID, DATA_CHANNEL, UDS_SENDFLAG_Default, buf, size);
}

size_t net_recv(void *buf, size_t size)
{
	if (!hosting && !joined)
		return 0;
	size_t actual = 0;
	u16 src = 0;
	if (R_FAILED(udsPullPacket(&bindctx, buf, size, &actual, &src)))
		return 0;
	return actual;
}

void net_close(void)
{
	if (hosting) {
		udsDestroyNetwork();
		udsUnbind(&bindctx);
	} else if (joined) {
		udsDisconnectNetwork();
		udsUnbind(&bindctx);
	}
	hosting = false;
	joined = false;
}
