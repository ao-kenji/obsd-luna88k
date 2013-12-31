/* $OpenBSD: omrasops.c,v 1.10 2013/11/16 22:45:37 aoyama Exp $ */
/* $NetBSD: omrasops.c,v 1.1 2000/01/05 08:48:56 nisimura Exp $ */

/*-
 * Copyright (c) 2000 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Tohru Nishimura.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Designed specifically for 'm68k bitorder';
 *	- most significant byte is stored at lower address,
 *	- most significant bit is displayed at left most on screen.
 * Implementation relies on;
 *	- every memory reference is done in aligned 32bit chunks,
 *	- font glyphs are stored in 32bit padded.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>

#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wsdisplayvar.h>
#include <dev/rasops/rasops.h>

#include <luna88k/dev/omrasops.h>

struct hwcmap {
#define CMAP_SIZE 256
	u_int8_t r[CMAP_SIZE];
	u_int8_t g[CMAP_SIZE];
	u_int8_t b[CMAP_SIZE];
};

struct om_hwdevconfig {
	int	dc_wid;			/* width of frame buffer */
	int	dc_ht;			/* height of frame buffer */
	int	dc_depth;		/* depth, bits per pixel */
	int	dc_rowbytes;		/* bytes in a FB scan line */
	int	dc_depth_checked;	/* depth is really checked or not */
	int	dc_cmsize;		/* colormap size */
	struct hwcmap dc_cmap;		/* software copy of colormap */
	vaddr_t	dc_videobase;		/* base of flat frame buffer */
	struct rasops_info dc_ri;	/* raster blitter variables */
	/* block move function, depend on depth */
	int	(*dc_bmv)(struct rasops_info *, u_int16_t, u_int16_t, u_int16_t,
			u_int16_t, u_int16_t, u_int16_t, int16_t, int16_t);
};

#define om_windowmove1 om_windowmove4

/* wscons emulator operations */
int	om_copycols(void *, int, int, int, int);
int	om_copyrows(void *, int, int, int num);
int	om_erasecols(void *, int, int, int, long);
int	om_eraserows(void *, int, int, long);

/* internal functions (for 1bpp, in omrasops1.c) */
int	om_windowmove1(struct rasops_info *, u_int16_t, u_int16_t,
		u_int16_t, u_int16_t, u_int16_t, u_int16_t, int16_t,
		int16_t /* ignored */);

/* internal functions (for 4bpp, in omrasops4.c) */
int	om_windowmove4(struct rasops_info *, u_int16_t, u_int16_t,
		u_int16_t, u_int16_t, u_int16_t, u_int16_t, int16_t,
		int16_t /* ignored */);

int
om_erasecols(void *cookie, int row, int col, int num, long attr)
{
	struct rasops_info *ri = cookie;
	struct om_hwdevconfig *hw = ri->ri_hw;
	int fg, bg;
	int snum, scol, srow;

	ri->ri_ops.unpack_attr(cookie, attr, &fg, &bg, NULL);

	snum = num * ri->ri_font->fontwidth;
	scol = col * ri->ri_font->fontwidth  + ri->ri_xorigin;
	srow = row * ri->ri_font->fontheight + ri->ri_yorigin;

	/*
	 * If this is too tricky for the simple raster ops engine,
	 * pass the fun to rasops.
	 */
	if ((*hw->dc_bmv)(ri, scol, srow, scol, srow, snum,
	    ri->ri_font->fontheight, RR_CLEAR, 0xff ^ bg) != 0)
		rasops_erasecols(cookie, row, col, num, attr);

	return 0;
}

int
om_eraserows(void *cookie, int row, int num, long attr)
{
	struct rasops_info *ri = cookie;
	struct om_hwdevconfig *hw = ri->ri_hw;
	int fg, bg;
	int srow, snum;
	int rc;

	ri->ri_ops.unpack_attr(cookie, attr, &fg, &bg, NULL);
	bg ^= 0xff;

	if (num == ri->ri_rows && (ri->ri_flg & RI_FULLCLEAR)) {
		rc = (*hw->dc_bmv)(ri, 0, 0, 0, 0, ri->ri_width, ri->ri_height,
		    RR_CLEAR, bg);
	} else {
		srow = row * ri->ri_font->fontheight + ri->ri_yorigin;
		snum = num * ri->ri_font->fontheight;
		rc = (*hw->dc_bmv)(ri, ri->ri_xorigin, srow, ri->ri_xorigin,
		    srow, ri->ri_emuwidth, snum, RR_CLEAR, bg);
	}
	if (rc != 0)
		rasops_eraserows(cookie, row, num, attr);

	return 0;
}

int
om_copyrows(void *cookie, int src, int dst, int n)
{
	struct rasops_info *ri = cookie;
	struct om_hwdevconfig *hw = ri->ri_hw;

	n   *= ri->ri_font->fontheight;
	src *= ri->ri_font->fontheight;
	dst *= ri->ri_font->fontheight;

	(*hw->dc_bmv)(ri, ri->ri_xorigin, ri->ri_yorigin + src,
		ri->ri_xorigin, ri->ri_yorigin + dst,
		ri->ri_emuwidth, n, RR_COPY, 0xff);

	return 0;
}

int
om_copycols(void *cookie, int row, int src, int dst, int n)
{
	struct rasops_info *ri = cookie;
	struct om_hwdevconfig *hw = ri->ri_hw;

	n   *= ri->ri_font->fontwidth;
	src *= ri->ri_font->fontwidth;
	dst *= ri->ri_font->fontwidth;
	row *= ri->ri_font->fontheight;

	(*hw->dc_bmv)(ri, ri->ri_xorigin + src, ri->ri_yorigin + row,
		ri->ri_xorigin + dst, ri->ri_yorigin + row,
		n, ri->ri_font->fontheight, RR_COPY, 0xff);

	return 0;
}
