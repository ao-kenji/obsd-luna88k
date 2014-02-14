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
/* #include <machine/pc98ext.h> */

#include <uvm/uvm_extern.h>

#include <luna88k/luna88k/isr.h>

/* prototypes */
int pc98ext_intr(void *);

/* autoconf stuff */
int pc98ext_match(struct device *, void *, void *);
void pc98ext_attach(struct device *, struct device *, void *);

struct pc98ext_softc {
	struct device sc_dev;
	int initialized;
	u_int8_t intreg;
};

int pc98ext_initted;

const struct cfattach pc98ext_ca = {
	sizeof(struct pc98ext_softc), pc98ext_match, pc98ext_attach
};

struct cfdriver pc98ext_cd = {
	NULL, "pc98ext", DV_DULL
};

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

	isrlink_autovec(pc98ext_intr, (void *)sc, ma->ma_ilvl, ISRPRI_TTY,
		self->dv_xname);

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

#if 0
/*ARGSUSED*/
int
pc98extrw(dev_t dev, struct uio *uio, int flags)
{
	vaddr_t v;
	int c;
	struct iovec *iov;
	int error = 0;

	while (uio->uio_resid > 0 && error == 0) {
		iov = uio->uio_iov;
		if (iov->iov_len == 0) {
			uio->uio_iov++;
			uio->uio_iovcnt--;
			if (uio->uio_iovcnt < 0)
				panic("mmrw");
			continue;
		}
		switch (minor(dev)) {

/* minor device 0 is physical memory */
		case 0:
/* minor device 1 is memory mapped I/O port area */
		case 1:
			v = uio->uio_offset;
			error = uiomove((caddr_t)v, uio->uio_resid, uio);
			continue;
		default:
			return (ENXIO);
		}
		if (error)
			break;
		iov->iov_base += c;
		iov->iov_len -= c;
		uio->uio_offset += c;
		uio->uio_resid -= c;
	}

	return (error);
}
#endif

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
	return (EOPNOTSUPP);
}

/*
 * C-bus Interrupt Status Register
 */
volatile u_int8_t *cisr = (u_int8_t *)0x91100000;

int
pc98ext_intr(void *arg)
{
#if 0
	struct pc98ext_softc *sc = (struct pc98ext_softc *)arg;
#endif

	if (!pc98ext_initted) return 1;

	if ((*cisr & 0x7f) != 0)	/* If it is not INT5, return */
		return 0;

	printf("%s: interrupt with 0x%02x\n", __func__, *cisr);

	*cisr = 1;			/* Clear INT5 flag */

	return 1;
}
