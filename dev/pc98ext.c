/*	$OpenBSD$ */

/*
 * Copyright (c) 1988 University of Utah.
 * Copyright (c) 1982, 1986, 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * the Systems Programming Group of the University of Utah Computer
 * Science Department.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)mem.c	8.3 (Berkeley) 1/12/94
 */

/*
 * PC-9801 extention board slot support for luna88k.
 * (based on m88k/m88k/mem.c)
 */

#include <sys/param.h>
#include <sys/buf.h>	/* need this? */
#include <sys/systm.h>	/* need this? */
#include <sys/uio.h>
#include <sys/malloc.h>	/* need this? */
#include <sys/device.h>
#include <sys/ioctl.h>

#include <machine/autoconf.h>
#include <machine/conf.h>
#include <machine/intr.h>
#include <machine/pc98ext.h>

#include <uvm/uvm_extern.h>

#include <luna88k/luna88k/isr.h>

#define PC98EXT_INT_MASK_NONE	0x00

u_int8_t pc98ext_int_mask[] = {
	0x40,	/* INT 0 */
	0x20,	/* INT 1 */
	0x10,	/* INT 2 */
	0x08,	/* INT 3 */
	0x04,	/* INT 4 */
	0x02,	/* INT 5 */
	0x01	/* INT 6 */
};

u_int8_t pc98ext_int_clear[] = {
	0x40,	/* INT 0 */
	0x20,	/* INT 1 */
	0x10,	/* INT 2 */
	0x08,	/* INT 3 */
	0x04,	/* INT 4 */
	0x02,	/* INT 5 */
	0x01	/* INT 6 */
};

/* autoconf stuff */
int pc98ext_match(struct device *, void *, void *);
void pc98ext_attach(struct device *, struct device *, void *);

struct pc98ext_softc {
	struct device sc_dev;
	int enable_intr;
	u_int8_t int_mask;
};

const struct cfattach pc98ext_ca = {
	sizeof(struct pc98ext_softc), pc98ext_match, pc98ext_attach
};

struct cfdriver pc98ext_cd = {
	NULL, "pc98ext", DV_DULL
};

/* prototypes */
int pc98ext_intr(void *);
int pc98ext_enable_int(struct pc98ext_softc *, u_int);
int pc98ext_disable_int(struct pc98ext_softc *, u_int);
int pc98ext_wait_int(struct pc98ext_softc *, u_int);
int pc98ext_check_int(struct pc98ext_softc *, u_int);

/*
 * C-bus Interrupt Status Register
 */
volatile u_int8_t *cisr = (u_int8_t *)0x91100000;

int
pc98ext_match(struct device *parent, void *cf, void *aux)
{
	struct mainbus_attach_args *ma = aux;

	if (strcmp(ma->ma_name, pc98ext_cd.cd_name))
		return 0;
#if 0
	if (badaddr((vaddr_t)ma->ma_addr, 4))
		return 0;
#endif
	return 1;
}

void
pc98ext_attach(struct device *parent, struct device *self, void *args)
{
	struct pc98ext_softc *sc = (struct pc98ext_softc *)self;
	struct mainbus_attach_args *ma = args;

	sc->enable_intr = 0;
	sc->int_mask = PC98EXT_INT_MASK_NONE;

	isrlink_autovec(pc98ext_intr, (void *)self, ma->ma_ilvl,
		ISRPRI_TTY, self->dv_xname);

	printf("\n");
}

/*ARGSUSED*/
int
pc98extopen(dev_t dev, int flag, int mode, struct proc *p)
{

	switch (minor(dev)) {
		case 0:	/* memory */
		case 1:	/* I/O */
			return (0);
		default:
			return (ENXIO);
	}
}

/*ARGSUSED*/
int
pc98extclose(dev_t dev, int flag, int mode, struct proc *p)
{

	return (0);
}

/*ARGSUSED*/
paddr_t
pc98extmmap(dev_t dev, off_t off, int prot)
{
	switch (minor(dev)) {
	case 0:
	case 1:
		return off;
	default:
		return (-1);
	}
}

/*ARGSUSED*/
int
pc98extioctl(dev_t dev, u_long cmd, caddr_t data, int flags, struct proc *p)
{
	struct pc98ext_softc *sc = (struct pc98ext_softc *)dev;

	u_int level;

	level = *(u_int *)data;
	printf("%s: enable_intr = %d\n", __func__, sc->enable_intr);

	switch(cmd) {
	case PCEXSETLEVEL:
		return pc98ext_enable_int(sc, level);

	case PCEXRESETLEVEL:
		return pc98ext_disable_int(sc, level);

	case PCEXWAITINT:
		return pc98ext_wait_int(sc, level);

	case PCEXCHECKINT:
		return pc98ext_check_int(sc, level);

	default:
		return ENOTTY;
	}
	return 0;
}

int
pc98ext_enable_int(struct pc98ext_softc *sc, u_int level)
{
	u_int8_t pre_mask;

	if ((level < 0) || (level > 6))
		return EINVAL;

	pre_mask = sc->int_mask;
	sc->int_mask |= pc98ext_int_mask[level];
	if (pre_mask == PC98EXT_INT_MASK_NONE)
		sc->enable_intr = 1;

	return 0;
}

int
pc98ext_disable_int(struct pc98ext_softc *sc, u_int level)
{
	if ((level < 0) || (level > 6))
		return EINVAL;

	sc->int_mask &= ~pc98ext_int_mask[level];
	/* clear interrupt flag */
	*cisr = (u_int8_t)(6 - level);

	if (sc->int_mask == PC98EXT_INT_MASK_NONE)
		sc->enable_intr = 0;

	return 0;
}

int
pc98ext_wait_int(struct pc98ext_softc *sc, u_int level)
{
	if ((level < 0) || (level > 6))
		return EINVAL;

	while ((*cisr & sc->int_mask) == 0)
		;

	return 0;
}

int
pc98ext_check_int(struct pc98ext_softc *sc, u_int level)
{
	if ((level < 0) || (level > 6))
		return EINVAL;

	return (int)(*cisr & pc98ext_int_mask[level]);
}

/*
 * Interrupt handler
 */
int
pc98ext_intr(void *arg)
{
	struct pc98ext_softc *sc = (struct pc98ext_softc *)arg;

	if (sc->enable_intr == 0) return 1;

	printf("%s: interrupt with 0x%02x\n", __func__, *cisr);

	if ((*cisr & 0x02) != 0)	/* If it is not INT5, return */
		return 0;

	*cisr = (u_int8_t)(6 - 5);

	return 1;
}
