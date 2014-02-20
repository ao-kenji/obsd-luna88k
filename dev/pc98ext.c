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
 * (partially based on m88k/m88k/mem.c)
 */

#include <sys/param.h>
#include <sys/buf.h>	/* need this? */
#include <sys/systm.h>	/* tsleep()/wakeup() */
#include <sys/uio.h>
#include <sys/malloc.h>	/* need this? */
#include <sys/device.h>
#include <sys/ioctl.h>

#include <machine/asm_macro.h>	/* ff1() */
#include <machine/autoconf.h>
#include <machine/conf.h>
#include <machine/intr.h>
#include <machine/pc98ext.h>

#include <uvm/uvm_extern.h>

#include <luna88k/luna88k/isr.h>

#define PC98EXT_INT_BITS_NONE	0x00

u_int8_t pc98ext_int_bits[] = {
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
	int intr_handled;
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

	sc->int_bits = PC98EXT_INT_BITS_NONE;

	isrlink_autovec(pc98ext_intr, (void *)self, ma->ma_ilvl,
		ISRPRI_TTY, self->dv_xname);

	printf("\n");
}

int
pc98extopen(dev_t dev, int flag, int mode, struct proc *p)
{

	switch (minor(dev)) {
	case 0:
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
	case 0:
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

	if (sc == NULL) {
		printf("%s: no such device\n", __func__);
		return ENXIO;
	}

	level = *(u_int *)data;

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
}

int
pc98ext_enable_int(struct pc98ext_softc *sc, u_int level)
{
	if ((level < 0) || (level > 6))
		return EINVAL;

	sc->int_bits |= pc98ext_int_bits[level];

	return 0;
}

int
pc98ext_disable_int(struct pc98ext_softc *sc, u_int level)
{
	if ((level < 0) || (level > 6))
		return EINVAL;

	sc->int_bits &= ~pc98ext_int_bits[level];
	/* clear interrupt flag */
	*cisr = (u_int8_t)(6 - level);

	return 0;
}

int
pc98ext_wait_int(struct pc98ext_softc *sc, u_int level)
{
	if ((level < 0) || (level > 6))
		return EINVAL;

	sc->intr_handled = 0;

	while (sc->intr_handled == 0) {
		tsleep((void *)sc, 0, "pc98ext", 0);
		printf("%s: wakeup from tsleep\n", __func__);
	}

	return 0;
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
 */
int
pc98ext_intr(void *arg)
{
#if 1
	struct pc98ext_softc *sc = (struct pc98ext_softc *)arg;

	/*
	 * Interrupt level 4 is shared with other devices.  So check my
	 * interrupt status register first.
	 */

	/* Should we compare with use sc->int_bits ?*/
	if ((*cisr & 0x02) != 0) return -1;	/* XXX: INT 5 fixed */

	/* Do something */
	printf("%s: called\n", __func__);

	sc->intr_handled = 1;
	wakeup((void *)sc);

	/* Clear interrupt flag */
	*cisr = 1;				/* XXX: INT 5 fixed */

	return 1;
#else
	struct pc98ext_softc *sc = (struct pc98ext_softc *)arg;
	u_int8_t int_stat;
	u_int32_t int_bits;
	int b;

	if (sc->intr_enabled == 0) return 1;

	/* If it is not waiting INT, return */
	int_stat = *cisr;

	int_bits = (u_int32_t)sc->int_bits;

	while ((b = ff1(int_bits)) != 32) {
		if (b > 7) {
			printf("stray C-bus INT, bit %d\n", b);
			int_bits |= ~(1 << b);
			continue;
		}
		printf("check C-bus INT%d\n", 6 - b);
		if ((int_stat & pc98ext_int_bits[6 - b]) == 0) {
			printf("INT%d found\n", 6 - b);

			/* do something */

			/* clear CISR */
			printf("%s: writing  %d\n", b);
			*cisr = (u_int8_t)(b);
			int_bits |= ~(1 << b);
		}
	}

	return 1;
#endif
}
