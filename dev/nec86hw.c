/*	$OpenBSD$	*/
/*	$NecBSD: nec86hw.c,v 1.13 1998/03/14 07:04:54 kmatsuda Exp $	*/
/*	$NetBSD$	*/

/*
 * [NetBSD for NEC PC-98 series]
 *  Copyright (c) 1996, 1997, 1998
 *	NetBSD/pc98 porting staff. All rights reserved.
 * 
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *  3. The name of the author may not be used to endorse or promote products
 *     derived from this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * nec86hw.c
 *
 * NEC PC-9801-86 SoundBoard PCM driver for NetBSD/pc98.
 * Written by NAGAO Tadaaki, Feb 10, 1996.
 *
 * Modified by N. Honda, Mar 7, 1998
 */
/*
 * TODO:
 * - Add PC-9801-73 support.
 * - Fake the mixer device with electric volumes.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/ioctl.h>
#include <sys/syslog.h>
#include <sys/device.h>
#include <sys/proc.h>

#include <machine/bus.h>
#include <machine/cpu.h>

#include <sys/audioio.h>
#include <dev/audio_if.h>
#include <dev/mulaw.h>
#include <dev/auconv.h>

#if 0
#include <dev/ic/ym2203reg.h>
#endif
#include <luna88k/dev/nec86reg.h>
#include <luna88k/dev/nec86hwvar.h>
#include <luna88k/dev/nec86var.h>

#define AUDIO_DEBUG	/* XXX: for debug */
#ifdef AUDIO_DEBUG
extern void Dprintf(const char *, ...);
#define DPRINTF(x)	if (nec86hwdebug) printf x
#define DPRINTF2(l, x)	if (nec86hwdebug >= l) printf x
int	nec86hwdebug = 3;
#else	/* !AUDIO_DEBUG */
#define DPRINTF(x)
#define DPRINTF2(l, x)
#endif	/* AUDIO_DEBUG */

#ifndef VOLUME_DELAY
/*
 * XXX -  Delaytime in microsecond after an access
 *	  to the volume I/O port.
 */
#define VOLUME_DELAY	10
#endif	/* !VOLUME_DELAY */

/*
 * Sampling rates supported by the hardware.
 */
static int nec86hw_rate_table[NEC86HW_NRATE_TYPE][NEC86_NRATE] = {
    /* NEC PC-9801-86 or its full compatibles */
    { 44100, 33075, 22050, 16538, 11025, 8269, 5513, 4134 },
    /* Some earlier versions of Q-Vision's WaveMaster */
    { 44100, 33075, 22050, 16000, 11025, 8000, 5513, 4000 },
};

int nec86hw_set_output_block(struct nec86hw_softc *, int);
int nec86hw_set_input_block(struct nec86hw_softc *, int);

/*
 * Function tables.
 */
static struct nec86hw_functable_entry nec86hw_functable[] = {
    /* precision, channels,
       output function, input function (without resampling),
       output function, input function (with resampling) */
    { 8, 1, 
      nec86fifo_output_mono_8_direct, nec86fifo_input_mono_8_direct,
      nec86fifo_output_mono_8_resamp, nec86fifo_input_mono_8_resamp },
    { 16, 1,
      nec86fifo_output_mono_16_direct, nec86fifo_input_mono_16_direct,
      nec86fifo_output_mono_16_resamp, nec86fifo_input_mono_16_resamp },
    { 8, 2,
      nec86fifo_output_stereo_8_direct, nec86fifo_input_stereo_8_direct,
      nec86fifo_output_stereo_8_resamp, nec86fifo_input_stereo_8_resamp },
    { 16, 2,
      nec86fifo_output_stereo_16_direct, nec86fifo_input_stereo_16_direct,
      nec86fifo_output_stereo_16_resamp, nec86fifo_input_stereo_16_resamp },
};
#define NFUNCTABLEENTRY	(sizeof(nec86hw_functable) / sizeof(nec86hw_functable[0]))

/*
 * Attach hardware to driver, attach hardware driver to audio
 * pseudo-device driver.
 */
void
nec86hw_attach(struct nec86hw_softc *sc)
{
    bus_space_tag_t iot = sc->sc_iot;
    bus_space_handle_t ioh = sc->sc_ioh;
    u_int8_t data;

    /* Set default encoding. */
    sc->func_fifo_output = nec86fifo_output_mono_8_resamp;
    sc->func_fifo_input = nec86fifo_input_mono_8_resamp;
    (void) nec86hw_set_params(sc, AUMODE_RECORD, 0,
			      &audio_default, &audio_default);
    (void) nec86hw_set_params(sc, AUMODE_PLAY,   0,
			      &audio_default, &audio_default);

    /* Set default ports. */
    (void) nec86hw_set_in_port(sc, NEC86HW_INPUT_MIXER);
    (void) nec86hw_set_out_port(sc, NEC86HW_OUTPUT_MIXER);

    /* Set default gains. */
    nec86hw_set_volume(sc, NEC86_VOLUME_PORT_OPNAD, NEC86_MAXVOL);
    nec86hw_set_volume(sc, NEC86_VOLUME_PORT_OPNAI, NEC86_MAXVOL);
    nec86hw_set_volume(sc, NEC86_VOLUME_PORT_LINED, NEC86_MAXVOL);
    nec86hw_set_volume(sc, NEC86_VOLUME_PORT_LINEI, NEC86_MAXVOL);
    nec86hw_set_volume(sc, NEC86_VOLUME_PORT_PCMD, NEC86_MAXVOL);

    /* Internal Speaker ON */
    nec86hw_speaker_ctl(sc, SPKR_ON);

    /* Set miscellanous stuffs. */
    data = bus_space_read_1(iot, ioh, NEC86_CTRL);
    data &= NEC86_CTRL_MASK_PAN | NEC86_CTRL_MASK_PORT;
    data |= NEC86_CTRL_PAN_L | NEC86_CTRL_PAN_R;
    data |= NEC86_CTRL_PORT_STD;
    bus_space_write_1(iot, ioh, NEC86_CTRL, data);
}

/*
 * Various routines to interface to higher level audio driver.
 */

int
nec86hw_open(void *arg, int flags)
{
    struct nec86hw_softc *sc = arg;
    DPRINTF(("nec86hw_open: sc=0x%x\n", sc));

    if (sc->sc_open != 0 || nec86hw_reset(sc) != 0)
	return ENXIO;

    sc->sc_open = 1;
    sc->sc_intr = NULL;
    sc->sc_arg = NULL;

    sc->conv_acc = 0;
    sc->conv_last0 = 0;
    sc->conv_last0_l = 0;
    sc->conv_last0_r = 0;
    sc->conv_last1 = 0;
    sc->conv_last1_l = 0;
    sc->conv_last1_r = 0;

    /*
     * Leave most things including sampling format and rate as they were.
     * (See audio_open() in audio.c)
     */

    DPRINTF(("nec86hw_open: opened\n"));

    return 0;
}

void
nec86hw_close(void *addr)
{
    register struct nec86hw_softc *sc = (struct nec86hw_softc *) addr;

    DPRINTF(("nec86hw_close: sc=0x%x\n", sc));

    sc->sc_open = 0;
    sc->sc_intr = NULL;
    (void) nec86hw_reset(sc);

    DPRINTF(("nec86hw_close: closed\n"));
}

int
nec86hw_set_params(void *addr, int mode, int usemode, struct audio_params *p,
		struct audio_params *r)
{
    register struct nec86hw_softc *sc = (struct nec86hw_softc *) addr;
    int rate_type = NEC86HW_RATE_TYPE(sc->sc_cfgflags);
    u_int prec, enc;

    if ((p->channels != 1) && (p->channels != 2))
	return EINVAL;

    enc = p->encoding;
    prec = p->precision;
    switch (enc) {
    case AUDIO_ENCODING_SLINEAR_BE:
    case AUDIO_ENCODING_SLINEAR_LE:
	if (prec == 8)
	     enc = AUDIO_ENCODING_SLINEAR;
	break;
    case AUDIO_ENCODING_ULINEAR_BE:
    case AUDIO_ENCODING_ULINEAR_LE:
	if (prec == 8)
	     enc = AUDIO_ENCODING_ULINEAR;
	break;
    case AUDIO_ENCODING_SLINEAR:
	if (prec == 16)
	     enc = AUDIO_ENCODING_SLINEAR_LE;
	break;
    case AUDIO_ENCODING_ULINEAR:
	if (prec == 16)
	     enc = AUDIO_ENCODING_ULINEAR_LE;
	break;
    case AUDIO_ENCODING_ULAW:
    case AUDIO_ENCODING_ALAW:
	break;
    default:
	return EINVAL;
    }

    p->sw_code = NULL;
    r->sw_code = NULL;
    switch (enc)
    {
    case AUDIO_ENCODING_ULAW:
	p->sw_code = mulaw_to_ulinear8;
   	r->sw_code = ulinear8_to_mulaw;
        enc = AUDIO_ENCODING_ULINEAR;
	break;
    case AUDIO_ENCODING_ALAW:
	p->sw_code = alaw_to_ulinear8;
   	r->sw_code = ulinear8_to_alaw;
        enc = AUDIO_ENCODING_ULINEAR;
	break;
    case AUDIO_ENCODING_ULINEAR_LE:
	p->sw_code = r->sw_code = change_sign16;
        enc = AUDIO_ENCODING_SLINEAR_LE;
	break;
    case AUDIO_ENCODING_ULINEAR_BE:
	p->sw_code = r->sw_code = swap_bytes_change_sign16;
        enc = AUDIO_ENCODING_SLINEAR_LE;
	break;
    case AUDIO_ENCODING_SLINEAR_BE:
	p->sw_code = r->sw_code = swap_bytes;
        enc = AUDIO_ENCODING_SLINEAR_LE;
	break;
    case AUDIO_ENCODING_SLINEAR:
	p->sw_code = r->sw_code = change_sign8;
        enc = AUDIO_ENCODING_ULINEAR;
    default:
	break;
    }

    sc->channels = p->channels;
    sc->precision = prec;
    sc->encoding = enc;
    sc->sc_orate = p->sample_rate;
    sc->hw_orate_bits = nec86hw_rate_bits(sc, p->sample_rate);
    sc->hw_orate = nec86hw_rate_table[rate_type][sc->hw_orate_bits];
    sc->sc_irate = r->sample_rate;
    sc->hw_irate_bits = nec86hw_rate_bits(sc, r->sample_rate);
    sc->hw_irate = nec86hw_rate_table[rate_type][sc->hw_irate_bits];
    return 0;
}

int
nec86hw_query_encoding(void *addr, struct audio_encoding *fp)
{

    switch (fp->index) {
    case 0:
	strlcpy(fp->name, AudioEmulaw, sizeof(fp->name));
	fp->encoding = AUDIO_ENCODING_ULAW;
	fp->precision = 8;
	fp->bps = 1;
	fp->msb = 1;
	fp->flags = AUDIO_ENCODINGFLAG_EMULATED;
	break;
    case 1:
	strlcpy(fp->name, AudioEalaw, sizeof(fp->name));
	fp->encoding = AUDIO_ENCODING_ALAW;
	fp->precision = 8;
	fp->bps = 1;
	fp->msb = 1;
	fp->flags = AUDIO_ENCODINGFLAG_EMULATED;
	break;
    case 2:
	strlcpy(fp->name, AudioEulinear, sizeof(fp->name));
	fp->encoding = AUDIO_ENCODING_ULINEAR;
	fp->precision = 8;
	fp->bps = 1;
	fp->msb = 1;
	fp->flags = 0;
	break;
    case 3:
	strlcpy(fp->name, AudioEslinear, sizeof(fp->name));
	fp->encoding = AUDIO_ENCODING_SLINEAR;
	fp->precision = 8;
	fp->bps = 1;
	fp->msb = 1;
	fp->flags = AUDIO_ENCODINGFLAG_EMULATED;
	break;
    case 4:
	strlcpy(fp->name, AudioEulinear_le, sizeof(fp->name));
	fp->encoding = AUDIO_ENCODING_ULINEAR_LE;
	fp->precision = 16;
	fp->bps = 2;
	fp->msb = 1;	/* is this OK? */
	fp->flags = AUDIO_ENCODINGFLAG_EMULATED;
	break;
    case 5:
	strlcpy(fp->name, AudioEulinear_be, sizeof(fp->name));
	fp->encoding = AUDIO_ENCODING_ULINEAR_BE;
	fp->precision = 16;
	fp->bps = 2;
	fp->msb = 1;	/* is this OK? */
	fp->flags = AUDIO_ENCODINGFLAG_EMULATED;
	break;
    case 6:
	strlcpy(fp->name, AudioEslinear_le, sizeof(fp->name));
	fp->encoding = AUDIO_ENCODING_SLINEAR_LE;
	fp->precision = 16;
	fp->bps = 2;
	fp->msb = 1;	/* is this OK? */
	fp->flags = 0;
	break;
    case 7:
	strlcpy(fp->name, AudioEslinear_be, sizeof(fp->name));
	fp->encoding = AUDIO_ENCODING_SLINEAR_BE;
	fp->precision = 16;
	fp->bps = 2;
	fp->msb = 1;	/* is this OK? */
	fp->flags = AUDIO_ENCODINGFLAG_EMULATED;
	break;
    default:
	return EINVAL;
	/*NOTREACHED*/
    }

    return 0;
}

int
nec86hw_round_blocksize(void *addr, int blk)
{
    u_int base = NEC86_INTRBLK_UNIT;

    if (blk < NEC86_INTRBLK_UNIT * 2)
	return NEC86_INTRBLK_UNIT * 2;

    for ( ; base <= blk; base *= 2)
	;
    return base / 2;
}

int
nec86hw_set_out_port(void *addr, int port)
{
    register struct nec86hw_softc *sc = (struct nec86hw_softc *) addr;

    DPRINTF(("nec86hw_set_out_port:\n"));

    if (port != NEC86HW_OUTPUT_MIXER)
	return EINVAL;

    sc->out_port = port;

    return 0;
}

int
nec86hw_get_out_port(void *addr)
{
    register struct nec86hw_softc *sc = (struct nec86hw_softc *) addr;

    DPRINTF(("nec86hw_get_out_port:\n"));

    return sc->out_port;
}

int
nec86hw_set_in_port(void *addr, int port)
{
    register struct nec86hw_softc *sc = (struct nec86hw_softc *) addr;

    DPRINTF(("nec86hw_set_in_port:\n"));

    if (port != NEC86HW_INPUT_MIXER)
	return EINVAL;

    sc->in_port = port;

    return 0;
}

int
nec86hw_get_in_port(void *addr)
{
    register struct nec86hw_softc *sc = (struct nec86hw_softc *) addr;

    DPRINTF(("nec86hw_get_in_port:\n"));

    return sc->in_port;
}

int
nec86hw_commit_settings(void *addr)
{
    register struct nec86hw_softc *sc = (struct nec86hw_softc *) addr;
    int i;

    /*
     * Determine which function should be used to write to/read from
     * the FIFO ring buffer.
     */

    for (i = 0; i < NFUNCTABLEENTRY; i++) {
	if ((nec86hw_functable[i].precision == sc->precision)
	    && (nec86hw_functable[i].channels == sc->channels))
	    break;
    }

    if (i >= NFUNCTABLEENTRY) {
	/* ??? -  This should never happen. */
	return EINVAL;
    }

    if (sc->sc_orate == sc->hw_orate) {
	/* No need to resample. */
	sc->func_fifo_output = nec86hw_functable[i].func_fifo_output_direct;
    } else {
	/* Resampling needed. */
	sc->func_fifo_output = nec86hw_functable[i].func_fifo_output_resamp;
    }

    if (sc->sc_irate == sc->hw_irate) {
	/* No need to resample. */
	sc->func_fifo_input = nec86hw_functable[i].func_fifo_input_direct;
    } else {
	/* Resampling needed. */
	sc->func_fifo_input = nec86hw_functable[i].func_fifo_input_resamp;
    }

    return 0;
}

int
nec86hw_setfd(void *addr, int flag)
{

    /* Can't do full-duplex */
    return ENOTTY;
}

int
nec86hw_mixer_set_port(void *addr, mixer_ctrl_t *cp)
{

    DPRINTF(("nec86hw_mixer_set_port:\n"));

    /* not yet implemented */
    return ENXIO;
}

int
nec86hw_mixer_get_port(void *addr, mixer_ctrl_t *cp)
{

    DPRINTF(("nec86hw_mixer_get_port:\n"));

    /* not yet implemented */
    return ENXIO;
}

int
nec86hw_mixer_query_devinfo(void *addr, mixer_devinfo_t *dip)
{

    DPRINTF(("nec86hw_mixer_query_devinfo:\n"));

    /* not yet implemented */
    return ENXIO;
}

int
nec86hw_set_output_block(struct nec86hw_softc *sc, int cc)
{
    int bpf, hw_blocksize, watermark;

    bpf = (sc->channels * sc->precision) / NBBY;	/* bytes per frame */
    sc->pdma_count = cc / bpf;

    /* Size of the block after the rate-conversion. */
    hw_blocksize =
	(sc->pdma_count * sc->hw_orate) / sc->sc_orate
	* (sc->precision / NBBY * 2);

    /* How many chunks the block should be divided into. */
    sc->pdma_nchunk =
	((hw_blocksize * (WATERMARK_MAX_RATIO + WATERMARK_RATIO_OUT))
	 + (NEC86_BUFFSIZE * WATERMARK_MAX_RATIO - 1))
	/ (NEC86_BUFFSIZE * WATERMARK_MAX_RATIO);

    /* Calculate the watermark. */
    watermark =
	(hw_blocksize * WATERMARK_RATIO_OUT)
	/ (sc->pdma_nchunk * WATERMARK_MAX_RATIO);
    sc->pdma_watermark = nec86hw_round_watermark(watermark);

    /*
     * If the formula (*1) does not hold, watermark may be less
     * than the minimum watermark (NEC86_INTRBLK_UNIT) and then
     * nec86hw_round_watermark() returns that minimum value.
     * In this case, the ring buffer on the hardware will potentially
     * overflow.
     * To avoid such a case, here calculate pdma_nchunk again.
     *
     * (*1)  NEC86_BUFFSIZE / NEC86_INTRBLK_UNIT
     *		>= WATERMARK_MAX_RATIO / WATERMARK_RATIO_OUT + 1
     */
    if (hw_blocksize + sc->pdma_watermark > NEC86_BUFFSIZE) {
	sc->pdma_nchunk =
	    (hw_blocksize + (NEC86_BUFFSIZE - sc->pdma_watermark - 1))
	    / (NEC86_BUFFSIZE - sc->pdma_watermark);
    }

    DPRINTF2(2, ("nec86hw_pdma_output: cc=%d count=%d hw_blocksize=%d "
		 "watermark=%d nchunk=%d ptr=0x%x\n",
		 cc, sc->pdma_count, hw_blocksize, sc->pdma_watermark,
		 sc->pdma_nchunk, sc->pdma_ptr));
    return 0;
}

int
nec86hw_set_input_block(struct nec86hw_softc *sc, int cc)
{
    int bpf, hw_blocksize, watermark, maxwatermark;

    bpf = (sc->channels * sc->precision) / NBBY;	/* bytes per frame */
    sc->pdma_count = cc / bpf;

    /* Maximum watermark. */
    watermark = (NEC86_BUFFSIZE * WATERMARK_RATIO_IN) / WATERMARK_MAX_RATIO;
    maxwatermark = nec86hw_round_watermark(watermark);

    /* Size of the block before the rate-conversion. */
    hw_blocksize =
	(sc->pdma_count * sc->hw_irate + (sc->sc_irate - 1)) / sc->sc_irate
	* (sc->precision / NBBY * 2);

    /* How many chunks the block should be divided into. */
    sc->pdma_nchunk = (hw_blocksize + (maxwatermark - 1)) / maxwatermark;

    /* Calculate the watermark. */
    watermark = (hw_blocksize / sc->pdma_nchunk) + (NEC86_INTRBLK_UNIT - 1);
    sc->pdma_watermark = nec86hw_round_watermark(watermark);

    DPRINTF2(2, ("nec86hw_pdma_input: cc=%d count=%d hw_blocksize=%d "
		 "watermark=%d nchunk=%d ptr=0x%x\n",
		 cc, sc->pdma_count, hw_blocksize, sc->pdma_watermark,
		 sc->pdma_nchunk, sc->pdma_ptr));
    return 0;
}

int
nec86hw_pdma_init_output(void *addr, void *buf, int cc)
{
	struct nec86hw_softc *sc = (struct nec86hw_softc *) addr;

	nec86hw_halt_pdma(addr);
	nec86hw_reset_fifo(sc);
	nec86hw_set_mode_playing(sc);
	nec86hw_set_rate_real(sc, sc->hw_orate_bits);
	nec86hw_set_precision_real(sc, sc->precision);

	nec86hw_set_output_block(sc, cc);
	nec86hw_reset_fifo(sc);

	nec86hw_enable_fifointr(sc);
	nec86hw_set_watermark(sc, sc->pdma_watermark);
        nec86hw_disable_fifointr(sc);

	return 0;
}

int
nec86hw_pdma_init_input(void *addr, void *buf, int cc)
{
	struct nec86hw_softc *sc = (struct nec86hw_softc *) addr;

	nec86hw_halt_pdma(addr);
	nec86hw_reset_fifo(sc);
	nec86hw_set_mode_recording(sc);
	nec86hw_set_rate_real(sc, sc->hw_irate_bits);
	nec86hw_set_precision_real(sc, sc->precision);

	nec86hw_set_input_block(sc, cc);
	nec86hw_reset_fifo(sc);

	nec86hw_enable_fifointr(sc);
	nec86hw_set_watermark(sc, sc->pdma_watermark);
        nec86hw_disable_fifointr(sc);

	return 0;
}

int
nec86hw_pdma_output(void *addr, void *p, int cc, void (*intr)(void *),
		void *arg)
{
    struct nec86hw_softc *sc = (struct nec86hw_softc *) addr;
    int bpf;

    bpf = (sc->channels * sc->precision) / NBBY;	/* bytes per frame */
    if ((cc % bpf) != 0) {
	DPRINTF(("nec86hw_pdma_output: odd bytes\n"));
	return EIO;
    }

    sc->sc_intr = intr;
    sc->sc_arg = arg;

    /*
     * We divide the data block into some relatively small chunks if the
     * block is so large that the ring buffer on the hardware would overflow.
     * In this case, we send the first chunk now and the rest in the
     * interrupt handler.
     */

    /* Set up for pseudo-DMA. */
    sc->pdma_ptr = (u_char *) p;
    sc->pdma_padded = 0;
    sc->pdma_mode = PDMA_MODE_OUTPUT;
    if (sc->pdma_count != cc / bpf)
	nec86hw_set_output_block(sc, cc);

    if (!sc->intr_busy) {
	nec86hw_set_precision_real(sc, sc->precision);
	nec86hw_set_rate_real(sc, sc->hw_orate_bits);
	nec86hw_set_mode_playing(sc);
	nec86hw_reset_fifo(sc);
    }

    /*
     * Send the first chunk.  The rest will be sent in the interrupt handler
     * nec86hw_intr(), if any.
     */
    nec86hw_disable_fifointr(sc);
    nec86hw_output_chunk(sc);
    nec86hw_enable_fifointr(sc);

    if (!sc->intr_busy) {
	/* Now start playing. */
	nec86hw_start_fifo(sc);

	sc->intr_busy = 1;
    }

    nec86hw_set_watermark(sc, sc->pdma_watermark);
    return 0;
}

int
nec86hw_pdma_input(void *addr, void *p, int cc, void (*intr)(void *), void *arg)
{
    struct nec86hw_softc *sc = (struct nec86hw_softc *) addr;
    int bpf;

    bpf = (sc->channels * sc->precision) / NBBY;	/* bytes per frame */
    if ((cc % bpf) != 0) {
	DPRINTF(("nec86hw_pdma_input: odd bytes\n"));
	return EIO;
    }

    sc->sc_intr = intr;
    sc->sc_arg = arg;

    /* Set up for pseudo-DMA. */
    sc->pdma_ptr = (u_int8_t *) p;
    sc->pdma_padded = 0;	/* Never padded in recording, though. */
    sc->pdma_mode = PDMA_MODE_INPUT;

    if (sc->pdma_count != cc / bpf)
	nec86hw_set_input_block(sc, cc);

    if (!sc->intr_busy) {
	nec86hw_set_precision_real(sc, sc->precision);
	nec86hw_set_rate_real(sc, sc->hw_irate_bits);
	nec86hw_set_mode_recording(sc);
	nec86hw_reset_fifo(sc);

	/* Now start recording. */
	nec86hw_enable_fifointr(sc);
	nec86hw_start_fifo(sc);

	sc->intr_busy = 1;
    }

    nec86hw_set_watermark(sc, sc->pdma_watermark);

    return 0;
}

int
nec86hw_halt_pdma(void *addr)
{
    register struct nec86hw_softc *sc = (struct nec86hw_softc *) addr;

    DPRINTF(("nec86hw_halt_pdma: sc=0x%x\n", sc));

    nec86hw_stop_fifo(sc);
    nec86hw_disable_fifointr(sc);

    sc->intr_busy = 0;

    return 0;
}

int
nec86hw_cont_pdma(void *addr)
{
    register struct nec86hw_softc *sc = (struct nec86hw_softc *) addr;

    DPRINTF(("nec86hw_cont_pdma: sc=0x%x\n", sc));

    nec86hw_enable_fifointr(sc);
    nec86hw_start_fifo(sc);

    sc->intr_busy = 1;

    return 0;
}

int
nec86hw_speaker_ctl(void *addr, int onoff)
{
    register struct nec86hw_softc *sc = (struct nec86hw_softc *) addr;
    bus_space_tag_t iot = sc->sc_iot;
    bus_space_handle_t ioh = sc->sc_ioh;

    DPRINTF(("nec86hw_speaker_ctl:\n"));

    switch ( onoff ) {
    case SPKR_ON:
	bus_space_write_1(iot, ioh, NEC86_VOLUME, 0x0d1);
	delay(VOLUME_DELAY);
	break;
    case SPKR_OFF:
	bus_space_write_1(iot, ioh, NEC86_VOLUME, 0x0d0);
	delay(VOLUME_DELAY);
	break;
    default:
	return EINVAL;
    }

    return 0;
}


u_int8_t
nec86hw_rate_bits(struct nec86hw_softc *sc, u_long sr)
{
    int i;
    u_long rval, hr, min;
    int rate_type = NEC86HW_RATE_TYPE(sc->sc_cfgflags);

    /* Look for the minimum hardware rate equal to or more than sr. */
    min = 0;
    rval = 0;
    for (i = 0; i < NEC86_NRATE; i++) {
	hr = nec86hw_rate_table[rate_type][i];
	if ((hr >= sr) && ((min == 0) || (min > hr))) {
	    min = hr;
	    rval = (u_int8_t) i;
	}
    }

    return rval;
}

int
nec86hw_round_watermark(int wm)
{
    wm = (wm / NEC86_INTRBLK_UNIT) * NEC86_INTRBLK_UNIT;

    if (wm < NEC86_INTRBLK_UNIT)
	wm = NEC86_INTRBLK_UNIT;

    return wm;
}


/*
 * Lower-level routines.
 */

int
nec86hw_reset(struct nec86hw_softc *sc)
{
    nec86hw_stop_fifo(sc);
    nec86hw_disable_fifointr(sc);
    nec86hw_clear_intrflg(sc);
    nec86hw_reset_fifo(sc);

    sc->intr_busy = 0;
    sc->pdma_mode = PDMA_MODE_NONE;

    if (nec86hw_seeif_intrflg(sc))
	return -1;	/* The hardware vanished? */

    return 0;
}

/*
 * Set the mode for playing/recording.
 */
void
nec86hw_set_mode_playing(struct nec86hw_softc *sc)
{
    bus_space_tag_t iot = sc->sc_iot;
    bus_space_handle_t ioh = sc->sc_ioh;
    u_int8_t data;

    data = bus_space_read_1(iot, ioh, NEC86_FIFOCTL);
    data &= ~NEC86_FIFOCTL_RECMODE;
    bus_space_write_1(iot, ioh, NEC86_FIFOCTL, data);
}

void
nec86hw_set_mode_recording(struct nec86hw_softc *sc)
{
    bus_space_tag_t iot = sc->sc_iot;
    bus_space_handle_t ioh = sc->sc_ioh;
    u_int8_t data;

    data = bus_space_read_1(iot, ioh, NEC86_FIFOCTL);
    data |= NEC86_FIFOCTL_RECMODE;
    bus_space_write_1(iot, ioh, NEC86_FIFOCTL, data);
}

/*
 * Set the electric volumes.
 */
void
nec86hw_set_volume(struct nec86hw_softc *sc, int port, u_int8_t vol)
{
    bus_space_tag_t iot = sc->sc_iot;
    bus_space_handle_t ioh = sc->sc_ioh;

    bus_space_write_1(iot, ioh, NEC86_VOLUME, NEC86_VOL_TO_BITS(port, vol));
    delay(VOLUME_DELAY);
}

/*
 * Control the FIFO ring buffer on the board.
 */
void
nec86hw_start_fifo(struct nec86hw_softc *sc)
{
    bus_space_tag_t iot = sc->sc_iot;
    bus_space_handle_t ioh = sc->sc_ioh;
    u_int8_t data;

DPRINTF(("%s\n", __func__));

    /* Start playing/recording. */
    data = bus_space_read_1(iot, ioh, NEC86_FIFOCTL);
    data |= NEC86_FIFOCTL_RUN;
    bus_space_write_1(iot, ioh, NEC86_FIFOCTL, data);
}

void
nec86hw_stop_fifo(struct nec86hw_softc *sc)
{
    bus_space_tag_t iot = sc->sc_iot;
    bus_space_handle_t ioh = sc->sc_ioh;
    u_int8_t data;

DPRINTF(("%s\n", __func__));

    /* Stop playing/recording. */
    data = bus_space_read_1(iot, ioh, NEC86_FIFOCTL);
    data &= ~NEC86_FIFOCTL_RUN;
    bus_space_write_1(iot, ioh, NEC86_FIFOCTL, data);
}

void
nec86hw_enable_fifointr(struct nec86hw_softc *sc)
{
    bus_space_tag_t iot = sc->sc_iot;
    bus_space_handle_t ioh = sc->sc_ioh;
    u_int8_t data;

DPRINTF(("%s\n", __func__));

    data = bus_space_read_1(iot, ioh, NEC86_FIFOCTL);
    data |= NEC86_FIFOCTL_ENBLINTR;
    bus_space_write_1(iot, ioh, NEC86_FIFOCTL, data);
}

void
nec86hw_disable_fifointr(struct nec86hw_softc *sc)
{
    bus_space_tag_t iot = sc->sc_iot;
    bus_space_handle_t ioh = sc->sc_ioh;
    u_int8_t data;

DPRINTF(("%s\n", __func__));

    data = bus_space_read_1(iot, ioh, NEC86_FIFOCTL);
    data &= ~NEC86_FIFOCTL_ENBLINTR;
    bus_space_write_1(iot, ioh, NEC86_FIFOCTL, data);
}

int
nec86hw_seeif_intrflg(struct nec86hw_softc *sc)
{
    bus_space_tag_t iot = sc->sc_iot;
    bus_space_handle_t ioh = sc->sc_ioh;
    u_int8_t data;

DPRINTF(("%s\n", __func__));

    data = bus_space_read_1(iot, ioh, NEC86_FIFOCTL);

    return (data & NEC86_FIFOCTL_INTRFLG);
}

void
nec86hw_clear_intrflg(struct nec86hw_softc *sc)
{
    bus_space_tag_t iot = sc->sc_iot;
    bus_space_handle_t ioh = sc->sc_ioh;
    u_int8_t data;

DPRINTF(("%s\n", __func__));

    data = bus_space_read_1(iot, ioh, NEC86_FIFOCTL);
    data &= ~NEC86_FIFOCTL_INTRFLG;
    bus_space_write_1(iot, ioh, NEC86_FIFOCTL, data);
    data |= NEC86_FIFOCTL_INTRFLG;
    bus_space_write_1(iot, ioh, NEC86_FIFOCTL, data);
}

void
nec86hw_reset_fifo(struct nec86hw_softc *sc)
{
    bus_space_tag_t iot = sc->sc_iot;
    bus_space_handle_t ioh = sc->sc_ioh;
    u_int8_t data;

DPRINTF(("%s\n", __func__));

    data = bus_space_read_1(iot, ioh, NEC86_FIFOCTL);
    data |= NEC86_FIFOCTL_INIT;
    bus_space_write_1(iot, ioh, NEC86_FIFOCTL, data);
    data &= ~NEC86_FIFOCTL_INIT;
    bus_space_write_1(iot, ioh, NEC86_FIFOCTL, data);
}

void
nec86hw_set_watermark(struct nec86hw_softc *sc, int wm)
{
    bus_space_tag_t iot = sc->sc_iot;
    bus_space_handle_t ioh = sc->sc_ioh;
    /*
     * Must be called after nec86hw_start_fifo() and nec86hw_enable_fifointr()
     * are both called.
     */

#ifdef DIAGNOSTIC
    if ((wm < NEC86_INTRBLK_UNIT) || (wm > NEC86_BUFFSIZE)
	|| ((wm % NEC86_INTRBLK_UNIT) != 0))
	printf("nec86hw_set_watermark: invalid watermark %d\n", wm);
#endif	/* DIAGNOSTIC */

DPRINTF(("%s: wm=%d, set %d\n", __func__, wm, wm / NEC86_INTRBLK_UNIT - 1));

    /*
     * The interrupt occurs when the number of bytes in the FIFO ring buffer
     * exceeds this watarmark.
     */
    bus_space_write_1(iot, ioh, NEC86_FIFOINTRBLK, (wm / NEC86_INTRBLK_UNIT) - 1);
}

void
nec86hw_set_precision_real(struct nec86hw_softc *sc, u_int prec)
{
    bus_space_tag_t iot = sc->sc_iot;
    bus_space_handle_t ioh = sc->sc_ioh;
    u_int8_t data;

    data = bus_space_read_1(iot, ioh, NEC86_CTRL);
    data &= ~NEC86_CTRL_8BITS;
    if (prec == 8)
	data |= NEC86_CTRL_8BITS;
    bus_space_write_1(iot, ioh, NEC86_CTRL, data);
}

void
nec86hw_set_rate_real(struct nec86hw_softc *sc, u_int8_t bits)
{
    bus_space_tag_t iot = sc->sc_iot;
    bus_space_handle_t ioh = sc->sc_ioh;
    u_int8_t data;

    data = bus_space_read_1(iot, ioh, NEC86_FIFOCTL);
    data &= ~NEC86_FIFOCTL_MASK_RATE;
    data |= bits & NEC86_FIFOCTL_MASK_RATE;
    bus_space_write_1(iot, ioh, NEC86_FIFOCTL, data);
}

/*
 * Write data to the FIFO ring buffer on the board.
 */
void
nec86hw_output_chunk(struct nec86hw_softc *sc)
{
    int cc, nbyte;

    if (sc->pdma_nchunk > 0) {
	/* Update chunksize and then send the chunk to the board. */
	cc = sc->pdma_count / sc->pdma_nchunk;	/* chunksize in frames */
	nbyte = (*sc->func_fifo_output)(sc, cc);

	DPRINTF2(3, ("nec86hw_output_chunk: sc->pdma_count=%d "
		     "sc->pdma_nchunk=%d cc=%d nbyte=%d ptr=0x%x\n",
		     sc->pdma_count,
		     sc->pdma_nchunk, cc, nbyte, sc->pdma_ptr));

	sc->pdma_nchunk--;
	sc->pdma_count -= cc;
	sc->pdma_ptr += nbyte; 
    } else {
	/* ??? -  This should never happen. */
	nbyte = 0;
	DPRINTF(("nec86hw_output_chunk: sc->pdma_nchunk=%d\n",
		 sc->pdma_nchunk));
    }

    /*
     * If size of the sent chunk is not enough, then pad out the buffer
     * with zero's.
     */
    if (nbyte <= sc->pdma_watermark) {
	nec86fifo_padding(sc, sc->pdma_watermark);
	sc->pdma_padded = 1;

	DPRINTF(("nec86hw_output_chunk: write padding zero's\n"));
    }
}

/*
 * Routines to write data directly.
 */
int
nec86fifo_output_mono_8_direct(struct nec86hw_softc *sc, int cc)
{
    bus_space_tag_t iot = sc->sc_iot;
    bus_space_handle_t ioh = sc->sc_ioh;
    u_int8_t *p = sc->pdma_ptr;
    int i;
    register u_int8_t d;

DPRINTF(("m8d: sc->pdma_ptr=0x%x, cc=%d\n", sc->pdma_ptr, cc));

    for (i = 0; i < cc; i++) {
	d = *p++;
	d ^= 0x80;	/* unsigned -> signed */
	/* Fake monoral playing by duplicating a sample. */
	bus_space_write_1(iot, ioh, NEC86_FIFODATA, d);
	bus_space_write_1(iot, ioh, NEC86_FIFODATA, d);
    }

DPRINTF(("\n"));

    return cc * 2;
}

int
nec86fifo_output_mono_16_direct(struct nec86hw_softc *sc, int cc)
{
    bus_space_tag_t iot = sc->sc_iot;
    bus_space_handle_t ioh = sc->sc_ioh;
    u_int8_t *p = sc->pdma_ptr;
    int i;

DPRINTF(("m16d: sc->pdma_ptr=0x%x, cc=%d\n", sc->pdma_ptr, cc));

    for (i = 0; i < cc; i++) {
	/* Fake monoral playing by duplicating a sample. */
#if BYTE_ORDER == BIG_ENDIAN
	bus_space_write_1(iot, ioh, NEC86_FIFODATA, *p);
	bus_space_write_1(iot, ioh, NEC86_FIFODATA, *(p + 1));
	bus_space_write_1(iot, ioh, NEC86_FIFODATA, *p);
	bus_space_write_1(iot, ioh, NEC86_FIFODATA, *(p + 1));
#else	/* little endian -> big endian */
	bus_space_write_1(iot, ioh, NEC86_FIFODATA, *(p + 1));
	bus_space_write_1(iot, ioh, NEC86_FIFODATA, *p);
	bus_space_write_1(iot, ioh, NEC86_FIFODATA, *(p + 1));
	bus_space_write_1(iot, ioh, NEC86_FIFODATA, *p);
#endif
	p += 2;
    }

    return cc * 4;
}

int
nec86fifo_output_stereo_8_direct(struct nec86hw_softc *sc, int cc)
{
    bus_space_tag_t iot = sc->sc_iot;
    bus_space_handle_t ioh = sc->sc_ioh;
    u_int8_t *p = sc->pdma_ptr;
    int i;

DPRINTF(("s8d: sc->pdma_ptr=0x%x, cc=%d\n", sc->pdma_ptr, cc));

    for (i = 0; i < cc; i++) {
	bus_space_write_1(iot, ioh, NEC86_FIFODATA, (*p++) ^ 0x80);	/* unsigned -> signed (L) */
	bus_space_write_1(iot, ioh, NEC86_FIFODATA, (*p++) ^ 0x80);	/* unsigned -> signed (R) */
    }

    return cc * 2;
}

int
nec86fifo_output_stereo_16_direct(struct nec86hw_softc *sc, int cc)
{
    bus_space_tag_t iot = sc->sc_iot;
    bus_space_handle_t ioh = sc->sc_ioh;
    u_int8_t *p = sc->pdma_ptr;
    int i;

DPRINTF(("s16d: sc->pdma_ptr=0x%x, cc=%d\n", sc->pdma_ptr, cc));

    for (i = 0; i < cc; i++) {
#if BYTE_ORDER == BIG_ENDIAN
	bus_space_write_1(iot, ioh, NEC86_FIFODATA, *p);	/* (L) */
	bus_space_write_1(iot, ioh, NEC86_FIFODATA, *(p + 1));
	p += 2;
	bus_space_write_1(iot, ioh, NEC86_FIFODATA, *p);	/* (R) */
	bus_space_write_1(iot, ioh, NEC86_FIFODATA, *(p + 1));
	p += 2;
#else	/* little endian -> big endian */
	bus_space_write_1(iot, ioh, NEC86_FIFODATA, *(p + 1));	/* (L) */
	bus_space_write_1(iot, ioh, NEC86_FIFODATA, *p);
	p += 2;
	bus_space_write_1(iot, ioh, NEC86_FIFODATA, *(p + 1));	/* (R) */
	bus_space_write_1(iot, ioh, NEC86_FIFODATA, *p);
	p += 2;
#endif
    }

    return cc * 4;
}

/*
 * Routines to write data with resampling. (linear interpolation)
 */
int
nec86fifo_output_mono_8_resamp(struct nec86hw_softc *sc, int cc)
{
    bus_space_tag_t iot = sc->sc_iot;
    bus_space_handle_t ioh = sc->sc_ioh;
    u_int8_t *p = sc->pdma_ptr;
    int i;
    register int rval;
    register u_int8_t d;
    register u_int8_t d0, d1;
    register u_long acc, orate, hw_orate;

DPRINTF(("m8r: sc->pdma_ptr=0x%x, cc=%d\n", sc->pdma_ptr, cc));

    rval = 0;

    orate = sc->sc_orate;
    hw_orate = sc->hw_orate;
    acc = sc->conv_acc;
    d0 = (u_int8_t) sc->conv_last0;
    d1 = (u_int8_t) sc->conv_last1;

    for (i = 0; i < cc; i++) {
	d0 = d1;
	d1 = *p++;

	while (acc <= hw_orate) {
	    /* Linear interpolation. */
	    d = ((d0 * (hw_orate - acc)) + (d1 * acc)) / hw_orate;
	    d ^= 0x80;		/* unsigned -> signed */

	    /* Fake monoral playing by duplicating a sample. */
	    bus_space_write_1(iot, ioh, NEC86_FIFODATA, d);
	    bus_space_write_1(iot, ioh, NEC86_FIFODATA, d);

	    acc += orate;
	    rval += 2;
	}

	acc -= hw_orate;
    }
    
    sc->conv_acc = acc;
    sc->conv_last0 = (u_short) d0;
    sc->conv_last1 = (u_short) d1;

    return rval;
}

int
nec86fifo_output_mono_16_resamp(struct nec86hw_softc *sc, int cc)
{
    bus_space_tag_t iot = sc->sc_iot;
    bus_space_handle_t ioh = sc->sc_ioh;
    u_int8_t *p = sc->pdma_ptr;
    int i;
    register int rval;
    register u_short d, d0, d1;
    register u_long acc, orate, hw_orate;

DPRINTF(("m16r: sc->pdma_ptr=0x%x, cc=%d\n", sc->pdma_ptr, cc));

    rval = 0;

    orate = sc->sc_orate;
    hw_orate = sc->hw_orate;
    acc = sc->conv_acc;
    d0 = sc->conv_last0;
    d1 = sc->conv_last1;
    
    for (i = 0; i < cc; i++) {
	d0 = d1;
	/* little endian signed -> unsigned */
	d1 = (*p | (*(p + 1) << 8)) ^ 0x8000;
	p += 2;

	while (acc <= hw_orate) {
	    /* Linear interpolation. */
	    d = ((d0 * (hw_orate - acc)) + (d1 * acc)) / hw_orate;
	    d ^= 0x8000;	/* unsigned -> signed */

	    /* Fake monoral playing by duplicating a sample. */
	    bus_space_write_1(iot, ioh, NEC86_FIFODATA, (d >> 8) & 0xff); /* -> big endian */
	    bus_space_write_1(iot, ioh, NEC86_FIFODATA, d & 0xff);
	    bus_space_write_1(iot, ioh, NEC86_FIFODATA, (d >> 8) & 0xff);
	    bus_space_write_1(iot, ioh, NEC86_FIFODATA, d & 0xff);

	    acc += orate;
	    rval += 4;
	}

	acc -= hw_orate;
    }
    
    sc->conv_acc = acc;
    sc->conv_last0 = d0;
    sc->conv_last1 = d1;

    return rval;
}

int
nec86fifo_output_stereo_8_resamp(struct nec86hw_softc *sc, int cc)
{
    bus_space_tag_t iot = sc->sc_iot;
    bus_space_handle_t ioh = sc->sc_ioh;
    u_int8_t *p = sc->pdma_ptr;
    int i;
    register int rval;
    register u_int8_t d, d0_l, d0_r, d1_l, d1_r;
    register u_long acc, orate, hw_orate;

DPRINTF(("s8r: sc->pdma_ptr=0x%x, cc=%d\n", sc->pdma_ptr, cc));

    rval = 0;

    orate = sc->sc_orate;
    hw_orate = sc->hw_orate;
    acc = sc->conv_acc;
    d0_l = sc->conv_last0_l;
    d0_r = sc->conv_last0_r;
    d1_l = sc->conv_last1_l;
    d1_r = sc->conv_last1_r;
    
    for (i = 0; i < cc; i++) {
	d0_l = d1_l;
	d0_r = d1_r;
	d1_l = *p++;
	d1_r = *p++;

	while (acc <= hw_orate) {
	    /* Linear interpolation. (L) */
	    d = ((d0_l * (hw_orate - acc)) + (d1_l * acc)) / hw_orate;
	    d ^= 0x80;		/* unsigned -> signed (L) */

	    bus_space_write_1(iot, ioh, NEC86_FIFODATA, d);

	    /* Linear interpolation. (R) */
	    d = ((d0_r * (hw_orate - acc)) + (d1_r * acc)) / hw_orate;
	    d ^= 0x80;		/* unsigned -> signed (R) */

	    bus_space_write_1(iot, ioh, NEC86_FIFODATA, d);

	    acc += orate;
	    rval += 2;
	}

	acc -= hw_orate;
    }
    
    sc->conv_acc = acc;
    sc->conv_last0_l = d0_l;
    sc->conv_last0_r = d0_r;
    sc->conv_last1_l = d1_l;
    sc->conv_last1_r = d1_r;
    
    return rval;
}

int
nec86fifo_output_stereo_16_resamp(struct nec86hw_softc *sc, int cc)
{
    bus_space_tag_t iot = sc->sc_iot;
    bus_space_handle_t ioh = sc->sc_ioh;
    u_int8_t *p = sc->pdma_ptr;
    int i;
    register int rval;
    register u_short d, d0_l, d0_r, d1_l, d1_r;
    register u_long acc, orate, hw_orate;
    
DPRINTF(("s16r: sc->pdma_ptr=0x%x, cc=%d\n", sc->pdma_ptr, cc));

    rval = 0;

    orate = sc->sc_orate;
    hw_orate = sc->hw_orate;
    acc = sc->conv_acc;
    
    d0_l = sc->conv_last0_l;
    d0_r = sc->conv_last0_r;
    d1_l = sc->conv_last1_l;
    d1_r = sc->conv_last1_r;
    
    for (i = 0; i < cc; i++) {
	d0_l = d1_l;
	d0_r = d1_r;
	/* little endian signed -> unsigned (L) */
	d1_l = (*p | (*(p + 1) << 8)) ^ 0x8000;
	p += 2;
	/* little endian signed -> unsigned (R) */
	d1_r = (*p | (*(p + 1) << 8)) ^ 0x8000;
	p += 2;

	while (acc <= hw_orate) {
	    /* Linear interpolation. (L) */
	    d = ((d0_l * (hw_orate - acc)) + (d1_l * acc)) / hw_orate;
	    d ^= 0x8000;	/* unsigned -> signed (L) */

	    bus_space_write_1(iot, ioh, NEC86_FIFODATA, (d >> 8) & 0xff); /* -> big endian (L) */
	    bus_space_write_1(iot, ioh, NEC86_FIFODATA, d & 0xff);

	    /* Linear interpolation. (R) */
	    d = ((d0_r * (hw_orate - acc)) + (d1_r * acc)) / hw_orate;
	    d ^= 0x8000;	/* unsigned -> signed (R) */

	    bus_space_write_1(iot, ioh, NEC86_FIFODATA, (d >> 8) & 0xff); /* -> big endian (R) */
	    bus_space_write_1(iot, ioh, NEC86_FIFODATA, d & 0xff);

	    acc += orate;
	    rval += 4;
	}

	acc -= hw_orate;
    }
    
    sc->conv_acc = acc;
    sc->conv_last0_l = d0_l;
    sc->conv_last0_r = d0_r;
    sc->conv_last1_l = d1_l;
    sc->conv_last1_r = d1_r;
    
    return rval;
}

/*
 * Read data from the FIFO ring buffer on the board.
 */
void
nec86hw_input_chunk(struct nec86hw_softc *sc)
{
    int cc, bpf;

    bpf = (sc->channels * sc->precision) / NBBY;
    if (sc->pdma_nchunk > 0) {
	/* Update chunksize and then receive the chunk from the board. */
	cc = sc->pdma_count / sc->pdma_nchunk;	/* chunksize in frames */
	(*sc->func_fifo_input)(sc, cc);

	DPRINTF2(3, ("nec86hw_input_chunk: sc->pdma_count=%d "
		     "sc->pdma_nchunk=%d cc=%d ptr=0x%x\n",
		     sc->pdma_count, sc->pdma_nchunk, cc, sc->pdma_ptr));

	sc->pdma_nchunk--;
	sc->pdma_count -= cc;
	sc->pdma_ptr += (cc * bpf);
    } else {
	/* ??? -  This should never happen. */
	cc = 0;
	DPRINTF(("nec86hw_input_chunk: ??? sc->pdma_nchunk=%d ???",
		 sc->pdma_nchunk));
    }
}

/*
 * Routines to read data directly.
 */
void
nec86fifo_input_mono_8_direct(struct nec86hw_softc *sc, int cc)
{
    bus_space_tag_t iot = sc->sc_iot;
    bus_space_handle_t ioh = sc->sc_ioh;
    u_int8_t *p = sc->pdma_ptr;
    int i;
    register u_int8_t d_l, d_r;

    for (i = 0; i < cc; i++) {
	d_l = bus_space_read_1(iot, ioh, NEC86_FIFODATA) ^ 0x80;	/* signed -> unsigned (L) */
	d_r = bus_space_read_1(iot, ioh, NEC86_FIFODATA) ^ 0x80;	/* signed -> unsigned (R) */

	/* Fake monoral recording by taking arithmetical mean. */
	*p++ = (d_l + d_r) / 2;
    }
}

void
nec86fifo_input_mono_16_direct(struct nec86hw_softc *sc, int cc)
{
    bus_space_tag_t iot = sc->sc_iot;
    bus_space_handle_t ioh = sc->sc_ioh;
    u_int8_t *p = sc->pdma_ptr;
    int i;
    register u_short d, d_l, d_r;

    for (i = 0; i < cc; i++) {
	/* big endian signed -> unsigned (L) */
	d_l = (bus_space_read_1(iot, ioh, NEC86_FIFODATA) ^ 0x80) << 8;
	d_l |= bus_space_read_1(iot, ioh, NEC86_FIFODATA);
	/* big endian signed -> unsigned (R) */
	d_r = (bus_space_read_1(iot, ioh, NEC86_FIFODATA) ^ 0x80) << 8;
	d_r |= bus_space_read_1(iot, ioh, NEC86_FIFODATA);

	/* Fake monoral recording by taking arithmetical mean. */
	d = (d_l + d_r) / 2;

#if BYTE_ORDER == BIG_ENDIAN
	*p++ = ((d >> 8) & 0xff) ^ 0x80;	/* -> big endian signed */
	*p++ = d & 0xff;
#else
	*p++ = d & 0xff;	/* -> little endian signed */
	*p++ = ((d >> 8) & 0xff) ^ 0x80;
#endif
    }
}

void
nec86fifo_input_stereo_8_direct(struct nec86hw_softc *sc, int cc)
{
    bus_space_tag_t iot = sc->sc_iot;
    bus_space_handle_t ioh = sc->sc_ioh;
    u_int8_t *p = sc->pdma_ptr;
    int i;

    for (i = 0; i < cc; i++) {
	*p++ = bus_space_read_1(iot, ioh, NEC86_FIFODATA) ^ 0x80;	/* signed -> unsigned (L) */
	*p++ = bus_space_read_1(iot, ioh, NEC86_FIFODATA) ^ 0x80;	/* signed -> unsigned (R) */
    }
}

void
nec86fifo_input_stereo_16_direct(struct nec86hw_softc *sc, int cc)
{
    bus_space_tag_t iot = sc->sc_iot;
    bus_space_handle_t ioh = sc->sc_ioh;
    u_int8_t *p = sc->pdma_ptr;
    int i;

    for (i = 0; i < cc; i++) {
#if BYTE_ORDER == BIG_ENDIAN
	*p = bus_space_read_1(iot, ioh, NEC86_FIFODATA);	/* (L) */
	*(p + 1) = bus_space_read_1(iot, ioh, NEC86_FIFODATA);
	p += 2;
	*p = bus_space_read_1(iot, ioh, NEC86_FIFODATA);	/* (R) */
	*(p + 1) = bus_space_read_1(iot, ioh, NEC86_FIFODATA);
	p += 2;
#else	/* big endian -> little endian */
	*(p + 1) = bus_space_read_1(iot, ioh, NEC86_FIFODATA);	/* (L) */
	*p = bus_space_read_1(iot, ioh, NEC86_FIFODATA);
	p += 2;
	*(p + 1) = bus_space_read_1(iot, ioh, NEC86_FIFODATA);	/* (R) */
	*p = bus_space_read_1(iot, ioh, NEC86_FIFODATA);
	p += 2;
#endif
    }
}

/*
 * Routines to read data with resampling. (linear interpolation)
 */
void
nec86fifo_input_mono_8_resamp(struct nec86hw_softc *sc, int cc)
{
    bus_space_tag_t iot = sc->sc_iot;
    bus_space_handle_t ioh = sc->sc_ioh;
    u_int8_t *p = sc->pdma_ptr;
    int i;
    register u_int8_t d_l, d_r;
    register u_int8_t d0, d1;
    register u_long acc, irate, hw_irate;
    
    irate = sc->sc_irate;
    hw_irate = sc->hw_irate;
    acc = sc->conv_acc;
    d0 = sc->conv_last0;
    d1 = sc->conv_last1;
    
    for (i = 0; i < cc; i++) {
	while (acc > irate) {
	    d0 = d1;
	    d_l = bus_space_read_1(iot, ioh, NEC86_FIFODATA) ^ 0x80; /* signed -> unsigned (L) */
	    d_r = bus_space_read_1(iot, ioh, NEC86_FIFODATA) ^ 0x80; /* signed -> unsigned (R) */
	    /* Fake monoral recording by taking arithmetical mean. */
	    d1 = (d_l + d_r) / 2;

	    acc -= irate;
	}

	/* Linear interpolation. */
	*p++ = ((d0 * (irate - acc)) + (d1 * acc)) / irate;

	acc += hw_irate;
    }
    
    sc->conv_acc = acc;
    sc->conv_last0 = d0;
    sc->conv_last1 = d1;
}

void
nec86fifo_input_mono_16_resamp(struct nec86hw_softc *sc, int cc)
{
    bus_space_tag_t iot = sc->sc_iot;
    bus_space_handle_t ioh = sc->sc_ioh;
    u_int8_t *p = sc->pdma_ptr;
    int i;
    register u_short d, d_l, d_r;
    register u_short d0, d1;
    register u_long acc, irate, hw_irate;
    
    irate = sc->sc_irate;
    hw_irate = sc->hw_irate;
    acc = sc->conv_acc;
    d0 = sc->conv_last0;
    d1 = sc->conv_last1;
    
    for (i = 0; i < cc; i++) {
	while (acc > irate) {
	    d0 = d1;
	    /* big endian signed -> unsigned (L) */
	    d_l = (bus_space_read_1(iot, ioh, NEC86_FIFODATA) ^ 0x80) << 8;
	    d_l |= bus_space_read_1(iot, ioh, NEC86_FIFODATA);
	    /* big endian signed -> unsigned (R) */
	    d_r = (bus_space_read_1(iot, ioh, NEC86_FIFODATA) ^ 0x80) << 8;
	    d_r |= bus_space_read_1(iot, ioh, NEC86_FIFODATA);
	    /* Fake monoral recording by taking arithmetical mean. */
	    d1 = (d_l + d_r) / 2;

	    acc -= irate;
	}

	/* Linear interpolation. */
	d = ((d0 * (irate - acc)) + (d1 * acc)) / irate;

#if BYTE_ORDER == BIG_ENDIAN
	*p++ = ((d >> 8) & 0xff) ^ 0x80;	/* -> big endian signed */
	*p++ = d & 0xff;
#else
	*p++ = d & 0xff;			/* -> little endian signed */
	*p++ = ((d >> 8) & 0xff) ^ 0x80;
#endif

	acc += hw_irate;
    }
    
    sc->conv_acc = acc;
    sc->conv_last0 = d0;
    sc->conv_last1 = d1;
}

void
nec86fifo_input_stereo_8_resamp(struct nec86hw_softc *sc, int cc)
{
    bus_space_tag_t iot = sc->sc_iot;
    bus_space_handle_t ioh = sc->sc_ioh;
    u_int8_t *p = sc->pdma_ptr;
    int i;
    register u_int8_t d0_l, d0_r, d1_l, d1_r;
    register u_long acc, irate, hw_irate;
    
    irate = sc->sc_irate;
    hw_irate = sc->hw_irate;
    acc = sc->conv_acc;
    d0_l = sc->conv_last0_l;
    d0_r = sc->conv_last0_r;
    d1_l = sc->conv_last1_l;
    d1_r = sc->conv_last1_r;
    
    for (i = 0; i < cc; i++) {
	while (acc > irate) {
	    d0_l = d1_l;
	    d0_r = d1_r;
	    d1_l = bus_space_read_1(iot, ioh, NEC86_FIFODATA) ^ 0x80; /* signed -> unsigned (L) */
	    d1_r = bus_space_read_1(iot, ioh, NEC86_FIFODATA) ^ 0x80; /* signed -> unsigned (R) */

	    acc -= irate;
	}

	/* Linear interpolation. (L) */
	*p++ = ((d0_l * (irate - acc)) + (d1_l * acc)) / irate;

	/* Linear interpolation. (R) */
	*p++ = ((d0_r * (irate - acc)) + (d1_r * acc)) / irate;

	acc += hw_irate;
    }
    
    sc->conv_acc = acc;
    sc->conv_last0_l = d0_l;
    sc->conv_last0_r = d0_r;
    sc->conv_last1_l = d1_l;
    sc->conv_last1_r = d1_r;
}

void
nec86fifo_input_stereo_16_resamp(struct nec86hw_softc *sc, int cc)
{
    bus_space_tag_t iot = sc->sc_iot;
    bus_space_handle_t ioh = sc->sc_ioh;
    u_int8_t *p = sc->pdma_ptr;
    int i;
    register u_short d, d0_l, d0_r, d1_l, d1_r;
    register u_long acc, irate, hw_irate;
    
    irate = sc->sc_irate;
    hw_irate = sc->hw_irate;
    acc = sc->conv_acc;
    d0_l = sc->conv_last0_l;
    d0_r = sc->conv_last0_r;
    d1_l = sc->conv_last1_l;
    d1_r = sc->conv_last1_r;
    
    for (i = 0; i < cc; i++) {
	while (acc > irate) {
	    d0_l = d1_l;
	    d0_r = d1_r;
	    /* big endian signed -> unsigned (L) */
	    d1_l = (bus_space_read_1(iot, ioh, NEC86_FIFODATA) ^ 0x80) << 8;
	    d1_l |= bus_space_read_1(iot, ioh, NEC86_FIFODATA);
	    /* big endian signed -> unsigned (R) */
	    d1_r = (bus_space_read_1(iot, ioh, NEC86_FIFODATA) ^ 0x80) << 8;
	    d1_r |= bus_space_read_1(iot, ioh, NEC86_FIFODATA);

	    acc -= irate;
	}

	/* Linear interpolation. (L) */
	d = ((d0_l * (irate - acc)) + (d1_l * acc)) / irate;

#if BYTE_ORDER == BIG_ENDIAN
	*p++ = ((d >> 8) & 0xff) ^ 0x80;	/* -> big endian signed (L) */
	*p++ = d & 0xff;
#else
	*p++ = d & 0xff;		/* -> little endian signed (L) */
	*p++ = ((d >> 8) & 0xff) ^ 0x80;
#endif

	/* Linear interpolation. (R) */
	d = ((d0_r * (irate - acc)) + (d1_r * acc)) / irate;

#if BYTE_ORDER == BIG_ENDIAN
	*p++ = ((d >> 8) & 0xff) ^ 0x80;	/* -> big endian signed (R) */
	*p++ = d & 0xff;
#else
	*p++ = d & 0xff;		/* -> little endian signed (R) */
	*p++ = ((d >> 8) & 0xff) ^ 0x80;
#endif

	acc += hw_irate;
    }
    
    sc->conv_acc = acc;
    sc->conv_last0_l = d0_l;
    sc->conv_last0_r = d0_r;
    sc->conv_last1_l = d1_l;
    sc->conv_last1_r = d1_r;
}

/*
 * Write padding zero's to the FIFO ring buffer on the board.
 */
void
nec86fifo_padding(struct nec86hw_softc *sc, int cc)
{
    bus_space_tag_t iot = sc->sc_iot;
    bus_space_handle_t ioh = sc->sc_ioh;
    register int i;

    DPRINTF2(2, ("nec86fifo_padding: %d\n", cc));

    for (i = 0; i < cc; i++)
	bus_space_write_1(iot, ioh, NEC86_FIFODATA, 0);
}

/*
 * Interrupt handler.
 */

/*
 * C-bus Interrupt Status Register
 */
volatile u_int8_t *cisr = (u_int8_t *)0x91100000;

int
nec86hw_intr(void *arg)
{
    struct nec86hw_softc *sc = (struct nec86hw_softc *) arg;

/*
 * XXX: C-bus-on-luna88k specific interrupt handling
 */
    if ((*cisr & 0x02) != 0)		/* If it is not INT5, return */
	return 0;

    if (!nec86hw_seeif_intrflg(sc)) {
	/* Seems to be an FM sound interrupt. */
	DPRINTF(("nec86hw_intr: ??? FM sound interrupt ???\n"));
	return 0;
    }

/* for debug */
    printf("%s: 0x%x, pdma_mode: 0x%x\n", __func__, *cisr, sc->pdma_mode);

    mtx_enter(&audio_lock);

    nec86hw_clear_intrflg(sc);

    switch(sc->pdma_mode) {
    case PDMA_MODE_OUTPUT:
	if (sc->pdma_padded) {
	    /* Clear the padding zero's. */
	    nec86hw_reset_fifo(sc);
	    sc->pdma_padded = 0;

	    DPRINTF(("nec86hw_intr: clear padding zero's\n"));
	}
	if (sc->pdma_count > 0) {
	    /* Send the next chunk. */
	    nec86hw_disable_fifointr(sc);
	    nec86hw_output_chunk(sc);
	    nec86hw_enable_fifointr(sc);
	}
	else
	    (*sc->sc_intr)(sc->sc_arg);
	break;
    case PDMA_MODE_INPUT:
	if (sc->pdma_count > 0) {
	    /* Receive the next chunk. */
	    nec86hw_disable_fifointr(sc);
	    nec86hw_input_chunk(sc);
	    nec86hw_enable_fifointr(sc);
	}
	if (sc->pdma_count <= 0)
	    (*sc->sc_intr)(sc->sc_arg);
	break;
    default:
	/* This should never happen. */
	nec86hw_stop_fifo(sc);
	nec86hw_disable_fifointr(sc);
	sc->intr_busy = 0;

	DPRINTF(("nec86hw_intr: ??? unexpected interrupt ???\n"));

	mtx_leave(&audio_lock);

	return 0;
    }

    mtx_leave(&audio_lock);

    /* clear INT5 interrupt (!?) */
    *cisr = 1;	/* Now we use INT5, then write 1 */

    return 1;
}

int
nec86_get_props(void *addr)
{

	return 0; 
}
