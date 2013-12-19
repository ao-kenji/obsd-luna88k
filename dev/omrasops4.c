/*	$OpenBSD$	*/

/*
 * Copyright (c) 2005, Miodrag Vallat.
 * All rights reserved.
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
 *
 */
/*
 * Copyright (c) 1996 Jason R. Thorpe.  All rights reserved.
 * Copyright (c) 1991 University of Utah.
 * Copyright (c) 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * the Systems Programming Group of the University of Utah Computer
 * Science Department and Mark Davies of the Department of Computer
 * Science, Victoria University of Wellington, New Zealand.
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
 * from: Utah $Hdr: grf_hy.c 1.2 93/08/13$
 *
 *	@(#)grf_hy.c	8.4 (Berkeley) 1/12/94
 */

/*
 * Graphics routines for OMRON LUNA 4bpp frame buffer.  On LUNA's frame
 * buffer, pixels are not byte-addressed.
 *
 * Based on src/sys/arch/hp300/dev/diofb_mono.c
 */

#include <sys/param.h>
#include <sys/systm.h>

#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wsdisplayvar.h>
#include <dev/rasops/rasops.h>
#include <dev/rasops/rasops_masks.h>

#include <luna88k/dev/maskbits.h>
#include <luna88k/dev/omrasops.h>

/* Prototypes */
int om_cursor4(void *, int, int, int);
int om_putchar4(void *, int, int, u_int, long);
int om_windowmove4(struct rasops_info *, u_int16_t, u_int16_t,
	u_int16_t, u_int16_t, u_int16_t, u_int16_t, int16_t,
	int16_t /* ignored */);

/*
 * Position|{enable|disable} the cursor at the specified location
 * - 4bpp version -
 */
int
om_cursor4(void *cookie, int on, int row, int col)
{
	struct rasops_info *ri = cookie;
	u_int8_t *p;
	int scanspan, startx, height, width, align, y;
	u_int32_t lmask, rmask, image;

	if (!on) {
		/* make sure it's on */
		if ((ri->ri_flg & RI_CURSOR) == 0)
			return 0;

		row = ri->ri_crow;
		col = ri->ri_ccol;
	} else {
		/* unpaint the old copy. */
		ri->ri_crow = row;
		ri->ri_ccol = col;
	}

	scanspan = ri->ri_stride;
	y = ri->ri_font->fontheight * row;
	startx = ri->ri_font->fontwidth * col;
	height = ri->ri_font->fontheight;

	p = (u_int8_t *)ri->ri_bits + y * scanspan + ((startx / 32) * 4);
	align = startx & ALIGNMASK;
	width = ri->ri_font->fontwidth + align;
	lmask = ALL1BITS >> align;
	rmask = ALL1BITS << (-width & ALIGNMASK);
	if (width <= BLITWIDTH) {
		lmask &= rmask;
		while (height > 0) {
			image = *P0(p);
			*P0(p) = (image & ~lmask) | ((image ^ ALL1BITS) & lmask);
			image = *P1(p);
			*P1(p) = (image & ~lmask) | ((image ^ ALL1BITS) & lmask);
			image = *P2(p);
			*P2(p) = (image & ~lmask) | ((image ^ ALL1BITS) & lmask);
			image = *P3(p);
			*P3(p) = (image & ~lmask) | ((image ^ ALL1BITS) & lmask);
			p += scanspan;
			height--;
		}
	} else {
		u_int8_t *q = p;

		while (height > 0) {
			image = *P0(p);
			*P0(p) = (image & ~lmask) | ((image ^ ALL1BITS) & lmask);
			image = *P1(p);
			*P1(p) = (image & ~lmask) | ((image ^ ALL1BITS) & lmask);
			image = *P2(p);
			*P2(p) = (image & ~lmask) | ((image ^ ALL1BITS) & lmask);
			image = *P3(p);
			*P3(p) = (image & ~lmask) | ((image ^ ALL1BITS) & lmask);

			p += BYTESDONE;

			image = *P0(p);
			*P0(p) = ((image ^ ALL1BITS) & rmask) | (image & ~rmask);
			image = *P1(p);
			*P1(p) = ((image ^ ALL1BITS) & rmask) | (image & ~rmask);
			image = *P2(p);
			*P2(p) = ((image ^ ALL1BITS) & rmask) | (image & ~rmask);
			image = *P3(p);
			*P3(p) = ((image ^ ALL1BITS) & rmask) | (image & ~rmask);

			p = (q += scanspan);
			height--;
		}
	}
	ri->ri_flg ^= RI_CURSOR;

	return 0;
}

/*
 * Blit a character at the specified co-ordinates
 * - 4bpp version -
 */
int
om_putchar4(void *cookie, int row, int startcol, u_int uc, long attr)
{
	struct rasops_info *ri = cookie;
	u_int8_t *p;
	int scanspan, startx, height, width, align, y;
	u_int32_t lmask, rmask, glyph, glyphbg, fgpat, bgpat;
	int i, fg, bg;
	u_int8_t *fb;

	scanspan = ri->ri_stride;
	y = ri->ri_font->fontheight * row;
	startx = ri->ri_font->fontwidth * startcol;
	height = ri->ri_font->fontheight;
	fb = (u_int8_t *)ri->ri_font->data +
	    (uc - ri->ri_font->firstchar) * ri->ri_fontscale;
	ri->ri_ops.unpack_attr(cookie, attr, &fg, &bg, NULL);

	p = (u_int8_t *)ri->ri_bits + y * scanspan + ((startx / 32) * 4);
	align = startx & ALIGNMASK;
	width = ri->ri_font->fontwidth + align;
	lmask = ALL1BITS >> align;
	rmask = ALL1BITS << (-width & ALIGNMASK);
	if (width <= BLITWIDTH) {
		lmask &= rmask;
		while (height > 0) {
			glyph = 0;
			for (i = ri->ri_font->stride; i != 0; i--)
				glyph = (glyph << 8) | *fb++;
			glyph <<= (4 - ri->ri_font->stride) * NBBY;
			glyph = (glyph >> align);
			glyphbg = glyph ^ ALL1BITS;

			fgpat = (fg & 0x01) ? glyph : 0;
			bgpat = (bg & 0x01) ? glyphbg : 0;
			*P0(p) = (*P0(p) & ~lmask) | ((fgpat | bgpat) & lmask);
			fgpat = (fg & 0x02) ? glyph : 0;
			bgpat = (bg & 0x02) ? glyphbg : 0;
			*P1(p) = (*P1(p) & ~lmask) | ((fgpat | bgpat) & lmask);
			fgpat = (fg & 0x04) ? glyph : 0;
			bgpat = (bg & 0x04) ? glyphbg : 0;
			*P2(p) = (*P2(p) & ~lmask) | ((fgpat | bgpat) & lmask);
			fgpat = (fg & 0x08) ? glyph : 0;
			bgpat = (bg & 0x08) ? glyphbg : 0;
			*P3(p) = (*P3(p) & ~lmask) | ((fgpat | bgpat) & lmask);

			p += scanspan;
			height--;
		}
	} else {
		u_int8_t *q = p;
		u_int32_t lhalf, rhalf;
		u_int32_t lhalfbg, rhalfbg;

		while (height > 0) {
			glyph = 0;
			for (i = ri->ri_font->stride; i != 0; i--)
				glyph = (glyph << 8) | *fb++;
			glyph <<= (4 - ri->ri_font->stride) * NBBY;
			lhalf = (glyph >> align);
			lhalfbg = lhalf ^ ALL1BITS;

			fgpat = (fg & 0x01) ? lhalf : 0;
			bgpat = (bg & 0x01) ? lhalfbg : 0;
			*P0(p) = (*P0(p) & ~lmask) | ((fgpat | bgpat) & lmask);
			fgpat = (fg & 0x02) ? lhalf : 0;
			bgpat = (bg & 0x02) ? lhalfbg : 0;
			*P1(p) = (*P1(p) & ~lmask) | ((fgpat | bgpat) & lmask);
			fgpat = (fg & 0x04) ? lhalf : 0;
			bgpat = (bg & 0x04) ? lhalfbg : 0;
			*P2(p) = (*P2(p) & ~lmask) | ((fgpat | bgpat) & lmask);
			fgpat = (fg & 0x08) ? lhalf : 0;
			bgpat = (bg & 0x08) ? lhalfbg : 0;
			*P3(p) = (*P3(p) & ~lmask) | ((fgpat | bgpat) & lmask);

			p += BYTESDONE;

			rhalf = (glyph << (BLITWIDTH - align));
			rhalfbg = rhalf ^ ALL1BITS;

			fgpat = (fg & 0x01) ? rhalf : 0;
			bgpat = (bg & 0x01) ? rhalfbg : 0;
			*P0(p) = ((fgpat | bgpat) & rmask) | (*P0(p) & ~rmask);
			fgpat = (fg & 0x02) ? rhalf : 0;
			bgpat = (bg & 0x02) ? rhalfbg : 0;
			*P1(p) = ((fgpat | bgpat) & rmask) | (*P1(p) & ~rmask);
			fgpat = (fg & 0x04) ? rhalf : 0;
			bgpat = (bg & 0x04) ? rhalfbg : 0;
			*P2(p) = ((fgpat | bgpat) & rmask) | (*P2(p) & ~rmask);
			fgpat = (fg & 0x08) ? rhalf : 0;
			bgpat = (bg & 0x08) ? rhalfbg : 0;
			*P3(p) = ((fgpat | bgpat) & rmask) | (*P3(p) & ~rmask);

			p = (q += scanspan);
			height--;
		}
	}

	return 0;
}

int
om_windowmove4(struct rasops_info *ri, u_int16_t sx, u_int16_t sy,
	u_int16_t dx, u_int16_t dy, u_int16_t cx, u_int16_t cy, int16_t rop,
	int16_t planemask /* ignored */)
{
	int width;		/* add to get to same position in next line */

	u_int32_t *psrcLine, *pdstLine;
				/* pointers to line with current src and dst */
	u_int32_t *psrc;	/* pointer to current src longword */
	u_int32_t *pdst;	/* pointer to current dst longword */

				/* following used for looping through a line */
	u_int32_t startmask, endmask;  /* masks for writing ends of dst */
	int nlMiddle;		/* whole longwords in dst */
	int nl;			/* temp copy of nlMiddle */
	int xoffSrc;		/* offset (>= 0, < 32) from which to
				   fetch whole longwords fetched in src */
	int nstart;		/* number of ragged bits at start of dst */
	int nend;		/* number of ragged bits at end of dst */
	int srcStartOver;	/* pulling nstart bits from src
				   overflows into the next word? */

	width = ri->ri_stride / 4;	/* convert to number in longword */

	if (sy < dy) {	/* start at last scanline of rectangle */
		psrcLine = ((u_int32_t *)OMFB_FB_WADDR)
				 + ((sy + cy - 1) * width);
		pdstLine = ((u_int32_t *)OMFB_FB_WADDR)
				 + ((dy + cy - 1) * width);
		width = -width;
	} else {	/* start at first scanline */
		psrcLine = ((u_int32_t *)OMFB_FB_WADDR) + (sy * width);
		pdstLine = ((u_int32_t *)OMFB_FB_WADDR) + (dy * width);
	}

	/* x direction doesn't matter for < 1 longword */
	if (cx <= 32) {
		int srcBit, dstBit;	/* bit offset of src and dst */

		pdstLine += (dx >> 5);
		psrcLine += (sx >> 5);
		psrc = psrcLine;
		pdst = pdstLine;

		srcBit = sx & 0x1f;
		dstBit = dx & 0x1f;

		while (cy--) {
			getandputrop(P0(psrc), srcBit, dstBit, cx, P0(pdst), rop);
			getandputrop(P1(psrc), srcBit, dstBit, cx, P1(pdst), rop);
			getandputrop(P2(psrc), srcBit, dstBit, cx, P2(pdst), rop);
			getandputrop(P3(psrc), srcBit, dstBit, cx, P3(pdst), rop);
			pdst += width;
			psrc += width;
		}
	} else {
		maskbits(dx, cx, startmask, endmask, nlMiddle);
		if (startmask)
			nstart = 32 - (dx & 0x1f);
		else
			nstart = 0;
		if (endmask)
			nend = (dx + cx) & 0x1f;
		else
			nend = 0;

		xoffSrc = ((sx & 0x1f) + nstart) & 0x1f;
		srcStartOver = ((sx & 0x1f) + nstart) > 31;

		if (sx >= dx) {	/* move left to right */
			pdstLine += (dx >> 5);
			psrcLine += (sx >> 5);

			while (cy--) {
				psrc = psrcLine;
				pdst = pdstLine;

				if (startmask) {
					getandputrop(P0(psrc), (sx & 0x1f),
					    (dx & 0x1f), nstart, P0(pdst), rop);
					getandputrop(P1(psrc), (sx & 0x1f),
					    (dx & 0x1f), nstart, P1(pdst), rop);
					getandputrop(P2(psrc), (sx & 0x1f),
					    (dx & 0x1f), nstart, P2(pdst), rop);
					getandputrop(P3(psrc), (sx & 0x1f),
					    (dx & 0x1f), nstart, P3(pdst), rop);
					pdst++;
					if (srcStartOver)
						psrc++;
				}

				/* special case for aligned operations */
				if (xoffSrc == 0) {
					nl = nlMiddle;
					while (nl--) {
						if (rop == RR_CLEAR) {
							*P0(pdst) = 0;
							*P1(pdst) = 0;
							*P2(pdst) = 0;
							*P3(pdst) = 0;
						} else {
							*P0(pdst) = *P0(psrc);
							*P1(pdst) = *P1(psrc);
							*P2(pdst) = *P2(psrc);
							*P3(pdst) = *P3(psrc);
						}
						psrc++;
						pdst++;
					}
				} else {
					nl = nlMiddle + 1;
					while (--nl) {
						if (rop == RR_CLEAR) {
							*P0(pdst) = 0;
							*P1(pdst) = 0;
							*P2(pdst) = 0;
							*P3(pdst) = 0;
						} else {
							getunalignedword(P0(psrc),
							    xoffSrc, *P0(pdst));
							getunalignedword(P1(psrc),
							    xoffSrc, *P1(pdst));
							getunalignedword(P2(psrc),
							    xoffSrc, *P2(pdst));
							getunalignedword(P3(psrc),
							    xoffSrc, *P3(pdst));
						}
						pdst++;
						psrc++;
					}
				}

				if (endmask) {
					getandputrop(P0(psrc), xoffSrc, 0, nend,
					    P0(pdst), rop);
					getandputrop(P1(psrc), xoffSrc, 0, nend,
					    P1(pdst), rop);
					getandputrop(P2(psrc), xoffSrc, 0, nend,
					    P2(pdst), rop);
					getandputrop(P3(psrc), xoffSrc, 0, nend,
					    P3(pdst), rop);
				}

				pdstLine += width;
				psrcLine += width;
			}
		} else {	/* move right to left */
			pdstLine += ((dx + cx) >> 5);
			psrcLine += ((sx + cx) >> 5);
			/*
			 * If fetch of last partial bits from source crosses
			 * a longword boundary, start at the previous longword
			 */
			if (xoffSrc + nend >= 32)
				--psrcLine;

			while (cy--) {
				psrc = psrcLine;
				pdst = pdstLine;

				if (endmask) {
					getandputrop(P0(psrc), xoffSrc, 0, nend,
					    P0(pdst), rop);
					getandputrop(P1(psrc), xoffSrc, 0, nend,
					    P1(pdst), rop);
					getandputrop(P2(psrc), xoffSrc, 0, nend,
					    P2(pdst), rop);
					getandputrop(P3(psrc), xoffSrc, 0, nend,
					    P3(pdst), rop);
				}

				nl = nlMiddle + 1;
				while (--nl) {
					--psrc;
					--pdst;
					if (rop == RR_CLEAR) {
						*P0(pdst) = 0;
						*P1(pdst) = 0;
						*P2(pdst) = 0;
						*P3(pdst) = 0;
					} else {
						getunalignedword(P0(psrc),
						    xoffSrc, *P0(pdst));
						getunalignedword(P1(psrc),
						    xoffSrc, *P1(pdst));
						getunalignedword(P2(psrc),
						    xoffSrc, *P2(pdst));
						getunalignedword(P3(psrc),
						    xoffSrc, *P3(pdst));
					}
				}

				if (startmask) {
					if (srcStartOver)
						--psrc;
					--pdst;
					getandputrop(P0(psrc), (sx & 0x1f),
					    (dx & 0x1f), nstart, P0(pdst), rop);
					getandputrop(P1(psrc), (sx & 0x1f),
					    (dx & 0x1f), nstart, P1(pdst), rop);
					getandputrop(P2(psrc), (sx & 0x1f),
					    (dx & 0x1f), nstart, P2(pdst), rop);
					getandputrop(P3(psrc), (sx & 0x1f),
					    (dx & 0x1f), nstart, P3(pdst), rop);
				}

				pdstLine += width;
				psrcLine += width;
			}
		}
	}

	return (0);
}
