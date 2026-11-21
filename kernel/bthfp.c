/*
 * bthfp.c - the answers. The console's Bluetooth code takes the line a
 * headset sends and knows three commands; everything else is answered ERROR. This is the part that answers the
 * hands-free service level handshake without a telephone behind it, and reads the headset's battery reports:
 *
 *   AT+BRSF=<n>            +BRSF: 1024        (the console's only feature: the headset indicators of the hands-free
 *                                              profile, bit 10; no calls, no codec negotiation, no three-way calling)
 *   AT+CIND=?              the seven indicators of the profile, in its order
 *   AT+CIND?               service up (see BTHFP_SERVICE), no call, no set-up, nothing held, signal 5, no roaming, battery 5
 *   AT+CMER=...            OK
 *   AT+CHLD=?              +CHLD: (0)         (only if the headset asks although three-way calling was not offered)
 *   AT+BIND=<list>         OK
 *   AT+BIND=?              +BIND: (2)         (the battery indicator)
 *   AT+BIND?               +BIND: 2,1         (and it is on)
 *   AT+BIEV=2,<0..100>     OK, and the level                    (the profile's own way: indicator 2 is the battery)
 *   AT+XAPL=<ids>,<n>      +XAPL=iPhone,2     (a vendor handshake some headsets use first; 2 = battery reports)
 *   AT+IPHONEACCEV=<n>,... OK, and the level for key 1 (0..9 is 10..100 percent)
 *   AT+CMEE= +CLIP= +CCWA= +NREC= +BIA= +COPS= AT+CLCC AT+CNUM AT+BTRH?    OK (AT+COPS? answers +COPS: 0 first)
 *
 * Anything else is not answered here (the caller leaves it to the console: calls, the stock volume and button
 * commands, unknown lines). A line of ours whose arguments are malformed is answered ERROR and carries no level.
 *
 * This file runs in the Bluetooth code's own thread, on every line a headset sends (tools/check_bteq_leaf.sh checks
 * its object file): general registers only, no library routine (no strlen/memcmp/printf: loops are written out),
 * no state, no allocation.
 */
#include "bthfp.h"
#include "../include/bluetoothturbo.h"

#define STR_(x) #x
#define STR(x)  STR_(x)

static const char R_OK[]       = "\r\nOK\r\n";
static const char R_ERROR[]    = "\r\nERROR\r\n";
static const char R_BRSF[]     = "\r\n+BRSF: 1024\r\n\r\nOK\r\n";
static const char R_CIND_T[]   = "\r\n+CIND: (\"service\",(0,1)),(\"call\",(0,1)),(\"callsetup\",(0-3)),(\"callheld\",(0-2)),"
                                 "(\"signal\",(0-5)),(\"roam\",(0,1)),(\"battchg\",(0-5))\r\n\r\nOK\r\n";
static const char R_CIND_R[]   = "\r\n+CIND: " STR(BTHFP_SERVICE) ",0,0,0,5,0,5\r\n\r\nOK\r\n";
static const char R_CHLD[]     = "\r\n+CHLD: (0)\r\n\r\nOK\r\n";
static const char R_BIND_T[]   = "\r\n+BIND: (2)\r\n\r\nOK\r\n";
static const char R_BIND_R[]   = "\r\n+BIND: 2,1\r\n\r\nOK\r\n";
static const char R_VENDOR[]     = "\r\n+XAPL=iPhone,2\r\n\r\nOK\r\n";
static const char R_COPS[]     = "\r\n+COPS: 0\r\n\r\nOK\r\n";

static int up(int c) { return c >= 'a' && c <= 'z' ? c - 32 : c; }
static int is_digit(int c) { return c >= '0' && c <= '9'; }

/* The upper-case `word` at p[*i], in any case: advances past it. */
static int eat(const uint8_t *p, uint32_t n, uint32_t *i, const char *w)
{
	uint32_t k = *i;

	for (; *w; w++, k++)
		if (k >= n || up(p[k]) != (int)(unsigned char)*w)
			return 0;
	*i = k;
	return 1;
}

/* 1 at the end of a command: the end of the data or a CR or LF. */
static int cmd_end(const uint8_t *p, uint32_t n, uint32_t i) { return i >= n || p[i] == '\r' || p[i] == '\n'; }

/* A decimal number that fits 32 bits (checked digit by digit: no 64-bit arithmetic, which may call a library routine). */
static int number(const uint8_t *p, uint32_t n, uint32_t *i, uint32_t *v)
{
	uint32_t x = 0, k = *i;

	for (; k < n && is_digit(p[k]); k++) {
		uint32_t d = (uint32_t)(p[k] - '0');

		if (x > 429496729u || (x == 429496729u && d > 5))
			return 0;
		x = x * 10 + d;
	}
	if (k == *i)
		return 0;
	*v = x;
	*i = k;
	return 1;
}

/* 1 if p[*i] is a comma, and moves past it. */
static int comma(const uint8_t *p, uint32_t n, uint32_t *i)
{
	if (*i < n && p[*i] == ',') {
		(*i)++;
		return 1;
	}
	return 0;
}

/* After a command: the bytes through its line end and the line ends and blanks after that. */
static uint32_t finish(const uint8_t *p, uint32_t n, uint32_t i, BtHfpAnswer *a, const char *reply, uint32_t step)
{
	while (i < n && p[i] != '\r' && p[i] != '\n')
		i++;
	while (i < n && (p[i] == '\r' || p[i] == '\n' || p[i] == ' ' || p[i] == 0))
		i++;
	a->reply = reply;
	a->step = step;
	return i;
}

static uint32_t refuse(const uint8_t *p, uint32_t n, uint32_t i, BtHfpAnswer *a)
{
	a->battery = -1;
	a->is_error = 1;
	a->step = 0;
	return finish(p, n, i, a, R_ERROR, 0);
}

/* The rest of a line is arguments of printable characters only (so a stray binary byte is left to the console). */
static int printable_args(const uint8_t *p, uint32_t n, uint32_t i)
{
	for (; i < n && p[i] != '\r' && p[i] != '\n'; i++)
		if (p[i] < 0x20 || p[i] > 0x7E)
			return 0;
	return 1;
}

uint32_t bthfp_answer(const uint8_t *p, uint32_t n, BtHfpAnswer *a)
{
	uint32_t i = 0, v, w, k;

	a->reply = R_ERROR;
	a->battery = -1;
	a->source = BTT_BATT_SRC_NONE;
	a->step = 0;
	a->hf_brsf = 0;
	a->vendor = 0;
	a->is_error = 0;

	while (i < n && (p[i] == '\r' || p[i] == '\n' || p[i] == ' '))
		i++;
	if (!(eat(p, n, &i, "AT")))
		return 0;

	/* the handshake */
	if (eat(p, n, &i, "+BRSF=")) {
		if (!number(p, n, &i, &v) || !cmd_end(p, n, i))
			return refuse(p, n, i, a);
		a->hf_brsf = v;
		return finish(p, n, i, a, R_BRSF, BTHFP_STEP_BRSF);
	}
	if (eat(p, n, &i, "+CIND=?"))
		return cmd_end(p, n, i) ? finish(p, n, i, a, R_CIND_T, BTHFP_STEP_CIND_TEST) : 0;
	if (eat(p, n, &i, "+CIND?"))
		return cmd_end(p, n, i) ? finish(p, n, i, a, R_CIND_R, BTHFP_STEP_CIND_READ) : 0;
	if (eat(p, n, &i, "+CMER=")) {
		if (!printable_args(p, n, i))
			return refuse(p, n, i, a);
		return finish(p, n, i, a, R_OK, BTHFP_STEP_CMER);
	}
	if (eat(p, n, &i, "+CHLD=?"))
		return cmd_end(p, n, i) ? finish(p, n, i, a, R_CHLD, BTHFP_STEP_CHLD) : 0;
	if (eat(p, n, &i, "+BIND=?"))
		return cmd_end(p, n, i) ? finish(p, n, i, a, R_BIND_T, BTHFP_STEP_BIND_TEST) : 0;
	if (eat(p, n, &i, "+BIND?"))
		return cmd_end(p, n, i) ? finish(p, n, i, a, R_BIND_R, BTHFP_STEP_BIND_READ) : 0;
	if (eat(p, n, &i, "+BIND=")) {
		/* a list of 1 to 20 indicator numbers separated by commas (no empty item, no trailing comma) */
		for (k = 0;;) {
			if (!number(p, n, &i, &v) || ++k > 20)
				return refuse(p, n, i, a);
			if (!comma(p, n, &i))
				break;
		}
		if (!cmd_end(p, n, i))
			return refuse(p, n, i, a);
		return finish(p, n, i, a, R_OK, BTHFP_STEP_BIND_SET);
	}

	/* the battery */
	if (eat(p, n, &i, "+BIEV=")) {
		if (!number(p, n, &i, &v) || !comma(p, n, &i) || !number(p, n, &i, &w) || !cmd_end(p, n, i) || v != 2 || w > 100)
			return refuse(p, n, i, a);
		a->battery = (int)w;
		a->source = BTT_BATT_SRC_HFIND;
		return finish(p, n, i, a, R_OK, 0);
	}
	if (eat(p, n, &i, "+XAPL=")) {
		/* <vendor>-<product>-<version>,<features>: the first part is letters, digits and dashes */
		for (k = 0; i < n && (is_digit(p[i]) || (up(p[i]) >= 'A' && up(p[i]) <= 'Z') || p[i] == '-'); i++, k++)
			;
		if (!k || !comma(p, n, &i) || !number(p, n, &i, &v) || !cmd_end(p, n, i))
			return refuse(p, n, i, a);
		a->vendor = v;
		return finish(p, n, i, a, R_VENDOR, BTHFP_STEP_VENDOR);
	}
	if (eat(p, n, &i, "+IPHONEACCEV=")) {
		int level = -1;

		/* <pairs>,<key>,<value>,...: every pair is read before anything is taken from it */
		if (!number(p, n, &i, &v) || v < 1 || v > 8)
			return refuse(p, n, i, a);
		for (k = 0; k < v; k++) {
			uint32_t key, val;

			if (!comma(p, n, &i) || !number(p, n, &i, &key) || !comma(p, n, &i) || !number(p, n, &i, &val))
				return refuse(p, n, i, a);
			if (key == 1) {
				if (val > 9)
					return refuse(p, n, i, a);
				level = (int)(val + 1) * 10;
			}
		}
		if (!cmd_end(p, n, i))
			return refuse(p, n, i, a);
		a->battery = level;
		a->source = level >= 0 ? BTT_BATT_SRC_VENDOR : BTT_BATT_SRC_NONE;
		return finish(p, n, i, a, R_OK, 0);
	}

	/* the harmless rest: switches the console has no use for, and lists with nothing in them */
	if (eat(p, n, &i, "+COPS?"))
		return cmd_end(p, n, i) ? finish(p, n, i, a, R_COPS, 0) : 0;
	if (eat(p, n, &i, "+CMEE=") || eat(p, n, &i, "+CLIP=") || eat(p, n, &i, "+CCWA=") || eat(p, n, &i, "+NREC=") ||
	    eat(p, n, &i, "+BIA=") || eat(p, n, &i, "+COPS=")) {
		if (!printable_args(p, n, i))
			return refuse(p, n, i, a);
		return finish(p, n, i, a, R_OK, 0);
	}
	if (eat(p, n, &i, "+BTRH?") || eat(p, n, &i, "+CLCC") || eat(p, n, &i, "+CNUM"))
		return cmd_end(p, n, i) ? finish(p, n, i, a, R_OK, 0) : 0;
	return 0;
}
