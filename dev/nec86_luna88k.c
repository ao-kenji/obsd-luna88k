/*	$OpenBSD$	*/
/*	$NecBSD: nec86_isa.c,v 1.9 1998/09/26 11:31:11 kmatsuda Exp $	*/
/*	$NetBSD$	*/

/*
 * [NetBSD for NEC PC-98 series]
 *  Copyright (c) 1995, 1996, 1997, 1998
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/ioctl.h>
#include <sys/syslog.h>
#include <sys/device.h>
#include <sys/proc.h>

#include <machine/autoconf.h>
#include <machine/bus.h>
#include <machine/cpu.h>

#include <sys/audioio.h>
#include <dev/audio_if.h>
#include <dev/mulaw.h>

#include <luna88k/dev/nec86reg.h>
#include <luna88k/dev/nec86hwvar.h>
#include <luna88k/dev/nec86var.h>

#include <luna88k/luna88k/isr.h>

int	nec86_match(struct device *, void *, void *);
void	nec86_attach(struct device *, struct device *, void *);

#if 0
struct cfattach nec86_ca = {
#else
struct cfattach pcm_ca = {
#endif
	sizeof(struct nec86_softc), nec86_match, nec86_attach
};

#if 0
struct cfdriver nec86_cd = {
#else
struct cfdriver pcm_cd = {
#endif
	NULL, "pcm", DV_DULL
};

/* bus space tag for nec86 */
struct luna88k_bus_space_tag nec86_bst = {
	1,	/* when reading/writing 1 byte, the stride is 1. */
	0,
	0,
	0,
};

int
nec86_match(struct device *parent, void *cf, void *aux)
{
	struct mainbus_attach_args *ma = aux;
#if 0
	if (strcmp(ma->ma_name, nec86_cd.cd_name))
#else
	if (strcmp(ma->ma_name, pcm_cd.cd_name))
#endif
		return 0;

	return 1;
}

void
nec86_attach(struct device *parent, struct device *self, void *aux)
{
	struct nec86_softc *nsc = (struct nec86_softc *)self;
	struct nec86hw_softc *ysc = &nsc->sc_nec86hw;
	struct mainbus_attach_args *ma = aux;

	bus_space_tag_t iot = &nec86_bst;
	bus_space_handle_t coreh, n86h;

	n86h  = (bus_space_handle_t)(ma->ma_addr + 0xa460);
	coreh = (bus_space_handle_t)(ma->ma_addr + 0xa466);

#if 0
	if (bus_space_map(iot, ma->ma_addr, 1, 0, &n86h) ||
	    bus_space_map(iot, ma->ma_addr + NEC86_COREOFFSET, 
		NEC86_CORESIZE, 0, &coreh)) {
		printf("%s: can not map\n", ysc->sc_dev.dv_xname);
		return;
	}
#endif
	nsc->sc_n86iot = iot;
	nsc->sc_n86ioh = n86h;
	ysc->sc_iot = iot;
	ysc->sc_ioh = coreh;
	ysc->sc_cfgflags = 0;	/* ia->ia_cfgflags */

#if 0
	systmmsg_bind(self, nec86_systmmsg);
#endif

	nec86_attachsubr(nsc);

	isrlink_autovec(nec86hw_intr, ysc, ma->ma_ilvl, ISRPRI_NET,
	    self->dv_xname);
}
