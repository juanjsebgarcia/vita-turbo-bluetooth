/*
 * btldac_enc.c - see btldac_enc.h.
 */
#include <stddef.h>
#include <string.h>

#include "ldacBT.h"
#include "ldacBT_abr.h"

#include "btldac_enc.h"
#include "btldac_mem.h"

#define MODE_STEREO LDACBT_CHANNEL_MODE_STEREO

int btldac_enc_open(BtLdacEnc *e, uint32_t mode, uint32_t mtu)
{
	HANDLE_LDAC_BT h;
	HANDLE_LDAC_ABR a;

	memset(e, 0, sizeof(*e));
	if (!btldac_mtu_ok(mtu))
		return BTLDAC_E_MTU;
	btldac_heap_reset();
	h = ldacBT_get_handle();
	a = h ? ldac_ABR_get_handle() : NULL;
	if (!h || !a) {
		btldac_heap_reset();
		return BTLDAC_E_MEMORY;
	}
	e->h = h;
	e->abr = a;
	/* the adaptive mode starts on the safe rate */
	if (ldacBT_init_handle_encode(h, (int)mtu, (int)btldac_eqmid_of(mode == BTLDAC_MODE_ABR ? BTLDAC_MODE_330 : mode), MODE_STEREO, LDACBT_SMPL_FMT_S16, (int)BTLDAC_RATE) != 0) {
		e->err = ldacBT_get_error_code(h);
		btldac_enc_close(e);
		return BTLDAC_E_INIT;
	}
	if (ldac_ABR_Init(a, BTLDAC_ABR_MS) != 0) {
		btldac_enc_close(e);
		return BTLDAC_E_INIT;
	}
	e->mode = mode;
	e->open = 1;
	e->eqmid = ldacBT_get_eqmid(h);
	return BTLDAC_E_NONE;
}

void btldac_enc_close(BtLdacEnc *e)
{
	if (e->abr)
		ldac_ABR_free_handle((HANDLE_LDAC_ABR)e->abr);
	if (e->h) {
		ldacBT_close_handle((HANDLE_LDAC_BT)e->h);
		ldacBT_free_handle((HANDLE_LDAC_BT)e->h);
	}
	e->h = NULL;
	e->abr = NULL;
	e->open = 0;
	btldac_heap_reset();
}

int btldac_enc_call(BtLdacEnc *e, const int16_t *pcm, uint8_t *out, uint32_t *len, uint32_t *nframes)
{
	int used = 0, sz = 0, nf = 0, r;

	*len = 0;
	*nframes = 0;
	if (!e->open)
		return BTLDAC_E_INIT;
	r = ldacBT_encode((HANDLE_LDAC_BT)e->h, (void *)pcm, &used, out, &sz, &nf);
	if (r != 0 || used != (int)(BTLDAC_FRAME * 4u)) {
		e->err = ldacBT_get_error_code((HANDLE_LDAC_BT)e->h);
		return BTLDAC_E_ENCODE;
	}
	if (sz < 0 || (uint32_t)sz > BTLDAC_FRAMES_BYTES || nf < 0 || (uint32_t)nf > BTLDAC_NFRM_MAX || (sz > 0) != (nf > 0)) {
		e->err = ldacBT_get_error_code((HANDLE_LDAC_BT)e->h);
		return BTLDAC_E_TOOBIG;
	}
	if (sz > 0)
		e->kbps = ldacBT_get_bitrate((HANDLE_LDAC_BT)e->h);
	*len = (uint32_t)sz;
	*nframes = (uint32_t)nf;
	return BTLDAC_E_NONE;
}

int btldac_enc_set_mode(BtLdacEnc *e, uint32_t mode)
{
	if (!e->open || mode >= BTLDAC_MODE_COUNT)
		return BTLDAC_E_INIT;
	if (mode != BTLDAC_MODE_ABR && ldacBT_set_eqmid((HANDLE_LDAC_BT)e->h, (int)btldac_eqmid_of(mode)) != 0)
		return BTLDAC_E_INIT;
	if (mode == BTLDAC_MODE_ABR && e->mode != BTLDAC_MODE_ABR) {
		if (ldacBT_set_eqmid((HANDLE_LDAC_BT)e->h, (int)btldac_eqmid_of(BTLDAC_MODE_330)) != 0 || ldac_ABR_Init((HANDLE_LDAC_ABR)e->abr, BTLDAC_ABR_MS) != 0)
			return BTLDAC_E_INIT;
	}
	e->mode = mode;
	e->eqmid = ldacBT_get_eqmid((HANDLE_LDAC_BT)e->h);
	return BTLDAC_E_NONE;
}

int btldac_enc_set_eqmid(BtLdacEnc *e, int eqmid)
{
	if (!e->open || ldacBT_set_eqmid((HANDLE_LDAC_BT)e->h, eqmid) != 0)
		return BTLDAC_E_INIT;
	e->eqmid = ldacBT_get_eqmid((HANDLE_LDAC_BT)e->h);
	return BTLDAC_E_NONE;
}

int btldac_enc_abr(BtLdacEnc *e, uint32_t depth)
{
	int q, was, fix;

	if (!e->open || e->mode != BTLDAC_MODE_ABR)
		return -1;
	was = ldacBT_get_eqmid((HANDLE_LDAC_BT)e->h);
	q = ldac_ABR_Proc((HANDLE_LDAC_BT)e->h, (HANDLE_LDAC_ABR)e->abr, depth, 1u);
	e->abr_calls++;
	if (q < 0)
		return -1;
	fix = btldac_abr_clamp(q);
	if (fix >= 0 && ldacBT_set_eqmid((HANDLE_LDAC_BT)e->h, fix) == 0)
		q = fix;
	if (q > was)           /* a higher index is a lower rate */
		e->abr_steps_down++;
	else if (q < was)
		e->abr_steps_up++;
	e->eqmid = q;
	return q;
}

int btldac_selftest_encode(uint32_t mode, BtLdacRun *run)
{
	static int16_t pcm[BTLDAC_FRAME * 2u];
	static uint8_t out[BTLDAC_OUT_MAX];
	BtLdacEnc e;
	uint32_t call, i, len, nf, fpp = btldac_frames_per_pkt(mode);
	int r;

	memset(run, 0, sizeof(*run));
	r = btldac_enc_open(&e, mode, BTLDAC_MTU_REQUIRED);
	if (r)
		return r;
	run->ok = 1;
	run->hash = BTLDAC_FNV_BASIS;
	for (call = 0; call < BTLDAC_ST_CALLS && run->ok; call++) {
		for (i = 0; i < BTLDAC_FRAME; i++)
			btldac_sig(call * BTLDAC_FRAME + i, &pcm[2u * i], &pcm[2u * i + 1u]);
		r = btldac_enc_call(&e, pcm, out, &len, &nf);
		if (r) {
			run->ok = 0;
			break;
		}
		if (len) {
			if (len != BTLDAC_PKT_BYTES_CBR || nf != fpp || out[0] != 0xAAu)   /* every frame starts with the sync byte */
				run->ok = 0;
			run->pkts++;
			run->bytes += len;
			run->hash = btldac_fnv(run->hash, out, len);
		}
	}
	btldac_enc_close(&e);
	return 0;
}
