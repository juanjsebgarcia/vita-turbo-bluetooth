/*
 * btcall.c - the Bluetooth code's connect calls (btcall.h). They are looked up by number at run time, the same on every firmware 3.60-3.74.
 * ksceBtStartDisconnect is the call the Settings app's own Disconnect button makes; ksceBtStartConnect the one its Connect makes.
 */
#include <psp2kern/kernel/modulemgr.h>
#include <psp2kern/kernel/threadmgr.h>
#include <taihen.h>

#include "btcall.h"

int module_get_export_func(SceUID pid, const char *modname, uint32_t libnid, uint32_t funcnid, uintptr_t *func);

#define LIB_BT_FOR_DRIVER    0xD48CA62D    /* SceBtForDriver, as SceAVConfig imports it                  */
#define NID_START_CONNECT    0x6059113Au   /* ksceBtStartConnect(address 0, address 1)                    */
#define NID_START_DISCONNECT 0x50710281u   /* ksceBtStartDisconnect(address 0, address 1)                 */
#define NID_GET_CONNINFO     0xE9936446u   /* ksceBtGetConnectingInfo(address 0, address 1)               */
#define NID_GET_CONFIG       0x94C66A3Bu   /* ksceBtGetConfiguration(): the Bluetooth state word's low byte */
#define NID_INFO_FOR_TEST    0x2C9DD7BBu   /* ksceBtGetInfoForTest(address 0, address 1, kernel buffer of 0x50 bytes): the controller's link quality and signal strength */
#define INFO_BYTES           0x50
#define INFO_OFF_LQ          0x42          /* link quality, 0..255                                                                                                 */
#define INFO_OFF_RSSI        0x43          /* signal strength, a signed byte                                                                                       */

#define BT_UP(w)             (((w) & 0x38) == 8)
#define LOCK_PRIO            0x40          /* the worker's priority while it holds the Bluetooth code's lock */
#define WORKER_PRIO          0x70

static int (*bc_connect)(unsigned, unsigned);
static int (*bc_disconnect)(unsigned, unsigned);
static int (*bc_conninfo)(unsigned, unsigned);
static int (*bc_config)(void);
static int (*bc_info)(unsigned, unsigned, void *);

static int bc_lookup(uint32_t nid, uintptr_t *f)
{
	*f = 0;
	return module_get_export_func(KERNEL_PID, "SceBt", LIB_BT_FOR_DRIVER, nid, f) < 0 || !*f ? -1 : 0;
}

int btcall_find(void)
{
	uintptr_t c, i, g;

	if (bc_config)
		return 0;
	if (bc_lookup(NID_START_CONNECT, &c) < 0 || bc_lookup(NID_GET_CONNINFO, &i) < 0 || bc_lookup(NID_GET_CONFIG, &g) < 0)
		return -1;
	bc_connect = (int (*)(unsigned, unsigned))c;
	bc_conninfo = (int (*)(unsigned, unsigned))i;
	bc_config = (int (*)(void))g;   /* last: set only when all three are found */
	return 0;
}

int btcall_find_disconnect(void)
{
	uintptr_t d;

	if (btcall_find() < 0)
		return -1;
	if (bc_disconnect)
		return 0;
	if (bc_lookup(NID_START_DISCONNECT, &d) < 0)
		return -1;
	bc_disconnect = (int (*)(unsigned, unsigned))d;
	return 0;
}

int btcall_up(void)
{
	int w;

	ksceKernelChangeThreadPriority(0, LOCK_PRIO);
	w = bc_config();
	ksceKernelChangeThreadPriority(0, WORKER_PRIO);
	return w >= 0 && BT_UP(w);
}

int btcall_state(uint32_t mac0, uint32_t mac1)
{
	int s;

	ksceKernelChangeThreadPriority(0, LOCK_PRIO);
	s = bc_conninfo(mac0, mac1);
	ksceKernelChangeThreadPriority(0, WORKER_PRIO);
	return s;
}

int btcall_connect(uint32_t mac0, uint32_t mac1)
{
	int r;

	ksceKernelChangeThreadPriority(0, LOCK_PRIO);
	r = bc_connect(mac0, mac1);
	ksceKernelChangeThreadPriority(0, WORKER_PRIO);
	return r;
}

int btcall_disconnect(uint32_t mac0, uint32_t mac1)
{
	int r;

	ksceKernelChangeThreadPriority(0, LOCK_PRIO);
	r = bc_disconnect(mac0, mac1);
	ksceKernelChangeThreadPriority(0, WORKER_PRIO);
	return r;
}

int btcall_find_signal(void)
{
	uintptr_t f;

	if (bc_info)
		return 0;
	if (bc_lookup(NID_INFO_FOR_TEST, &f) < 0)
		return -1;
	bc_info = (int (*)(unsigned, unsigned, void *))f;
	return 0;
}

int btcall_signal(uint32_t mac0, uint32_t mac1, unsigned *lq, int *rssi)
{
	uint8_t raw[INFO_BYTES];
	int r;

	for (unsigned i = 0; i < sizeof(raw); i++)
		raw[i] = 0;
	ksceKernelChangeThreadPriority(0, LOCK_PRIO);
	r = bc_info(mac0, mac1, raw);
	ksceKernelChangeThreadPriority(0, WORKER_PRIO);
	if (r < 0)
		return r;
	*lq = raw[INFO_OFF_LQ];
	*rssi = (int)(int8_t)raw[INFO_OFF_RSSI];
	return 0;
}
