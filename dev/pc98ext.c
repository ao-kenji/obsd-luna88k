/*	$OpenBSD:$	*/

/*
 * Copyright (c) 2014 Kenji Aoyama.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * PC-9801 extention board slot support for luna88k.
 */

#include <sys/param.h>
#include <sys/systm.h>	/* tsleep()/wakeup() */
#include <sys/uio.h>
#include <sys/device.h>
#include <sys/ioctl.h>

#include <machine/asm_macro.h>	/* ff1() */
#include <machine/autoconf.h>
#include <machine/conf.h>
#include <machine/intr.h>
#include <machine/pc98ext.h>

#include <uvm/uvm_extern.h>

#include <luna88k/luna88k/isr.h>

u_int8_t pc98ext_int_bits[] = {
	0x40,	/* INT 0 */
	0x20,	/* INT 1 */
	0x10,	/* INT 2 */
	0x08,	/* INT 3 */
	0x04,	/* INT 4 */
	0x02,	/* INT 5 */
	0x01	/* INT 6 */
/*	0x80	   NMI(?), not supported in this driver now */
};

/* autoconf stuff */
int pc98ext_match(struct device *, void *, void *);
void pc98ext_attach(struct device *, struct device *, void *);

struct pc98ext_softc {
	struct device sc_dev;
	u_int8_t int_bits;
};

const struct cfattach pc98ext_ca = {
	sizeof(struct pc98ext_softc), pc98ext_match, pc98ext_attach
};

struct cfdriver pc98ext_cd = {
	NULL, "pc98ext", DV_DULL
};

/* prototypes */
int pc98ext_intr(void *);
int pc98ext_set_int(struct pc98ext_softc *, u_int);
int pc98ext_reset_int(struct pc98ext_softc *, u_int);
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

	sc->int_bits = 0x00;

	isrlink_autovec(pc98ext_intr, (void *)self, ma->ma_ilvl,
		ISRPRI_TTY, self->dv_xname);

	printf("\n");
}

int
pc98extopen(dev_t dev, int flag, int mode, struct proc *p)
{

	switch (minor(dev)) {
	case 0:	/* memory area */
	case 1:	/* I/O port area */
		return 0;
	default:
		return ENXIO;
	}
}

int
pc98extclose(dev_t dev, int flag, int mode, struct proc *p)
{

	return (0);
}

paddr_t
pc98extmmap(dev_t dev, off_t off, int prot)
{
	switch (minor(dev)) {
	case 0:	/* memory area */
	case 1:	/* I/O port area */
		return off;
	default:
		return -1;
	}
}

int
pc98extioctl(dev_t dev, u_long cmd, caddr_t data, int flags, struct proc *p)
{
	struct pc98ext_softc *sc = pc98ext_cd.cd_devs[0];	/* XXX */
	u_int level;

	if (sc == NULL)
		return ENXIO;

	level = *(u_int *)data;

	switch(cmd) {
	case PCEXSETLEVEL:
		return pc98ext_set_int(sc, level);

	case PCEXRESETLEVEL:
		return pc98ext_reset_int(sc, level);

	case PCEXWAITINT:
		return pc98ext_wait_int(sc, level);

	case PCEXCHECKINT:
		return pc98ext_check_int(sc, level);

	default:
		return ENOTTY;
	}
}

int
pc98ext_set_int(struct pc98ext_softc *sc, u_int level)
{
	if ((level < 0) || (level > 6))
		return EINVAL;

	sc->int_bits |= pc98ext_int_bits[level];

	return 0;
}

int
pc98ext_reset_int(struct pc98ext_softc *sc, u_int level)
{
	if ((level < 0) || (level > 6))
		return EINVAL;

	sc->int_bits &= ~pc98ext_int_bits[level];

	return 0;
}

int
pc98ext_wait_int(struct pc98ext_softc *sc, u_int level)
{
	int ret;

	if ((level < 0) || (level > 6))
		return EINVAL;

	ret = tsleep((void *)sc, 0 | PCATCH, "pc98ext", 100 /* XXX */);
#ifdef PC98EXT_DEBUG
	printf("%s: wakeup from tsleep%s\n", __func__,
		ret == EWOULDBLOCK ? ", timeout" : "");
#endif
	return ret;
}

int
pc98ext_check_int(struct pc98ext_softc *sc, u_int level)
{
	if ((level < 0) || (level > 6))
		return EINVAL;

	return (int)(*cisr & pc98ext_int_bits[level]);
}

/*
 * Interrupt handler
 *
 * PC-9801 extention slot (so-called 'C-bus' in Japan) has 8 own interrupt
 * levels, INT0-INT6, and NMI.  On LUNA-88K{,2}, the interrput status register
 * for C-bus is (u_int8_t *)0x91100000.
 * Each bit of the register shows each interrupt status as follows:
 *
 * bit 7 = NMI(?)
 * bit 6 = INT0
 * bit 5 = INT1
 *  :
 * bit 0 = INT6
 *
 * If interrupt occurs, the bit becomes 0, otherwise 1.
 */
int
pc98ext_intr(void *arg)
{
	struct pc98ext_softc *sc = (struct pc98ext_softc *)arg;
	u_int8_t int_status;
	int n;

	/*
	 * LUNA's interrupt level 4 is shared with other devices, such as
	 * le(4), for example.  So we check:
	 * - the value of our PC98 interrupt status register, and
	 * - if the INT level is what we are looking for.
	 */
	int_status = *cisr & sc->int_bits;
	if (int_status == sc->int_bits) return -1;

	/* Just wakeup(9) for now */
	wakeup((void *)sc);

	/* Make a bit pattern that we should clear interrupt flag */
	int_status = int_status ^ sc->int_bits;

	/* Clear each interrupt flag */
	while ((n = ff1(int_status)) != 32) {
		*cisr = (u_int8_t)n;
		int_status &= ~(0x01 << n); 
	}

	return 1;
}
