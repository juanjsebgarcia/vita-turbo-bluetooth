/*
 * bthfp.h - what the console answers a Bluetooth headset that speaks the hands-free handshake and reports its
 * battery (bthfp.c).
 */
#ifndef BTT_BTHFP_H
#define BTT_BTHFP_H

#include <stdint.h>

/* The service indicator in the answer to AT+CIND?: 1 = a network is up (the quiet choice: some headsets announce
 * "no service" otherwise), 0 = none. Change here, rebuild, to try the other. */
#ifndef BTHFP_SERVICE
#define BTHFP_SERVICE  1
#endif

/* The handshake steps a line can be (BtHfpAnswer.step, and the bits kept as "reached"). */
#define BTHFP_STEP_BRSF       (1u << 0)   /* AT+BRSF=<features>: features exchange                 */
#define BTHFP_STEP_CIND_TEST  (1u << 1)   /* AT+CIND=?: the indicator list                         */
#define BTHFP_STEP_CIND_READ  (1u << 2)   /* AT+CIND?: the indicator values                        */
#define BTHFP_STEP_CMER       (1u << 3)   /* AT+CMER=...: indicator reporting on                   */
#define BTHFP_STEP_BIND_SET   (1u << 4)   /* AT+BIND=<list>: the headset's indicators              */
#define BTHFP_STEP_BIND_TEST  (1u << 5)   /* AT+BIND=?: the console's indicators                   */
#define BTHFP_STEP_BIND_READ  (1u << 6)   /* AT+BIND?: which are on                                */
#define BTHFP_STEP_VENDOR     (1u << 7)   /* the vendor handshake some headsets use first          */
#define BTHFP_STEP_CHLD       (1u << 8)   /* AT+CHLD=?: asked although it was not offered          */

typedef struct {
	const char *reply;      /* the whole answer, CR LF and all, NUL ended; always set when bthfp_answer returns > 0 */
	int battery;            /* 0..100 when the line carried a level, else -1                                       */
	uint32_t source;        /* BTT_BATT_SRC_* of that level                                                        */
	uint32_t step;          /* the BTHFP_STEP_* bit of this line, 0 if it is not a step                            */
	uint32_t hf_brsf;       /* with BTHFP_STEP_BRSF: the headset's feature word                                    */
	uint32_t vendor;        /* with BTHFP_STEP_VENDOR: the feature word the headset announced                      */
	int is_error;           /* the reply is ERROR (a line of ours that was malformed)                              */
} BtHfpAnswer;

/* One command at the start of `line` (len bytes, the console's receive buffer; never written). Returns the bytes
 * it used (up to and including the command's end of line and any line ends and blanks that follow it), with `*a`
 * filled in, or 0 when the line does not start with a command this file answers: the caller then leaves it to the
 * console's own code (which answers ERROR to what it does not know). Pure: no calls, no state. */
uint32_t bthfp_answer(const uint8_t *line, uint32_t len, BtHfpAnswer *a);

#endif
