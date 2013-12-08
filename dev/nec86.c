/*	$OpenBSD$	*/
/*	$NecBSD: nec86.c,v 1.11 1999/07/23 11:04:39 honda Exp $	*/
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
 * nec86.c
 *
 * NEC PC-9801-86 SoundBoard PCM driver for NetBSD/pc98.
 * Written by NAGAO Tadaaki, Feb 10, 1996.
 *
 * Modified by N. Honda, Mar 7, 1998
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

#include <luna88k/dev/nec86reg.h>
#include <luna88k/dev/nec86hwvar.h>
#include <luna88k/dev/nec86var.h>

#define NEC_SCR_SIDMASK		0xf0
#define NEC_SCR_MASK		0x0f
#define NEC_SCR_EXT_ENABLE	0x01
#if 0
extern struct cfdriver pcm_cd;
#endif

struct audio_device nec86_device = {
    "PC-9801-86",
    "",
    "pcm"
};

/*
 * Define our interface to the higher level audio driver.
 */

struct audio_hw_if nec86_hw_if = {
    nec86hw_open,
    nec86hw_close,
    NULL,			/* drain */
    nec86hw_query_encoding,
    nec86hw_set_params,
    nec86hw_round_blocksize,
    nec86hw_commit_settings,
    nec86hw_pdma_init_output,
    nec86hw_pdma_init_input,
    nec86hw_pdma_output,
    nec86hw_pdma_input,
    nec86hw_halt_pdma,
    nec86hw_halt_pdma,
    nec86hw_speaker_ctl,
    nec86getdev,
    nec86hw_setfd,
    nec86hw_mixer_set_port,
    nec86hw_mixer_get_port,
    nec86hw_mixer_query_devinfo,
    NULL,			/* allocm */
    NULL,			/* freem */
    NULL,			/* round_buffersize */
    NULL,			/* mappage */
    nec86_get_props,
    NULL,			/* trigger_output */
    NULL,			/* trigger_input */
    NULL			/* get_default_params */
};

/*
 * Probe for NEC PC-9801-86 SoundBoard hardware.
 */
int
nec86_probesubr(bus_space_tag_t iot, bus_space_handle_t ioh,
	bus_space_handle_t n86ioh)
{
    u_int8_t data;

#ifdef	notyet
    if (nec86hw_probesubr(iot, ioh) != 0)
	return -1;
#endif	/* notyet */

#if 0
    if (n86ioh == NULL)
#else
    if (n86ioh == 0)
#endif
	data = 0x40;
    else
        data = bus_space_read_1(iot, n86ioh, NEC86_SOUND_ID);

    switch (data & NEC_SCR_SIDMASK) {
#if 0	/* XXX -  PC-9801-73 not yet supported. */
    case 0x20:
    case 0x30:
	break;
#endif
    case 0x40:
    case 0x50:
	break;
    default:	/* No supported board found. */
	return -1;
	/*NOTREACHED*/
    }

    return ((data & NEC_SCR_SIDMASK) >> 4) - 2;
}

/*
 * Attach hardware to driver, attach hardware driver to audio
 * pseudo-device driver.
 */
#define MODEL0_NAME	"PC-9801-73 soundboard"
#define MODEL1_NAME	"PC-9801-86 soundboard"

void
nec86_attachsubr(struct nec86_softc *sc)
{
    struct nec86hw_softc *ysc = &sc->sc_nec86hw;
    bus_space_tag_t iot = sc->sc_n86iot;
    bus_space_handle_t n86ioh = sc->sc_n86ioh;
    char *boardname[] = {MODEL0_NAME, MODEL0_NAME, MODEL1_NAME, MODEL1_NAME};
    u_int8_t data;
    int model;
  
    if ((model = nec86_probesubr(iot, n86ioh, n86ioh)) < 0)
    {
	printf("%s: missing hardware\n", ysc->sc_dev.dv_xname);
	return;
    }
    ysc->model = model;

#if 0
    if (n86ioh != NULL)
#else
    if (n86ioh != 0)
#endif
    {
    	data = bus_space_read_1(iot, n86ioh, NEC86_SOUND_ID);
    	data &= ~NEC_SCR_MASK;
    	data |= NEC_SCR_EXT_ENABLE;
    	bus_space_write_1(iot, n86ioh, NEC86_SOUND_ID, data);
    }

    nec86hw_attach(ysc);

    if (sc->sc_attached == 0)
    {
	    printf(": %s\n", boardname[ysc->model]);
	    audio_attach_mi(&nec86_hw_if, ysc, &ysc->sc_dev);
	    sc->sc_attached = 1;
     }
}

/*
 * Various routines to interface to higher level audio driver.
 */
int
nec86getdev(void *addr, struct audio_device *retp)
{
    *retp = nec86_device;

    return 0;
}
