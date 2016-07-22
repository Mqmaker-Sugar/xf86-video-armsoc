/* -*- mode: C; c-file-style: "k&r"; tab-width 4; indent-tabs-mode: t; -*- */

/*
 * Copyright © 2011 Texas Instruments, Inc
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors:
 *    Rob Clark <rob@ti.com>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "armsoc_driver.h"
#include "armsoc_exa.h"

#include "exa.h"

#include <libdrm/rockchip_drmif.h>
#include <libdrm/rockchip_rga.h>
#include <drm_fourcc.h>

#include <time.h>

#undef RGA_ENABLE_COPY
#define RGA_ENABLE_SOLID
#undef RGA_ENABLE_COMPOSITE


/* This file has a trivial EXA implementation which accelerates nothing.  It
 * is used as the fall-back in case the EXA implementation for the current
 * chipset is not available.  (For example, on chipsets which used the closed
 * source IMG PowerVR EXA implementation, if the closed-source submodule is
 * not installed.
 */

struct EXApixRGAimg {
	PixmapPtr pSrc; 
	struct rga_image simg;
	
	PixmapPtr pDst;
	struct rga_image dimg;
};

struct RockchipRGAEXARec {
	struct ARMSOCEXARec base;
	ExaDriverPtr exa;

	/* add any other driver private data here.. */
	struct rga_context *rga_ctx;

	void *priv;
};

/*
static struct RockchipRGAEXARec*
RGAPrivFromPixmap(PixmapPtr pPixmap)
{
	ScrnInfoPtr pScrn = xf86ScreenToScrn(pPixmap->drawable.pScreen);
	struct ARMSOCRec *pARMSOC = ARMSOCPTR(pScrn);
	return (struct RockchipRGAEXARec*)(pARMSOC->pARMSOCEXA);
}
*/

static const char*
translate_gxop(unsigned int op)
{
	switch (op) {
	case GXclear:				/* 0x0: 0 */
		return "clear";
	case GXand:					/* 0x1: src AND dst */
		return "and";
	case GXandReverse:			/* 0x2: src AND NOT dst */
		return "and/reverse";
	case GXcopy:				/* 0x3: src */
		return "copy";
	case GXandInverted:			/* 0x4: NOT src AND dst */
		return "and/inverted";
	case GXnoop:				/* 0x5: dst */
		return "noop";
	case GXxor:					/* 0x6: src XOR dst */
		return "xor";
	case GXor:					/* 0x7: src OR dst */
		return "or";
	case GXnor:					/* 0x8: NOT src AND NOT dst */
		return "nor";
	case GXequiv:				/* 0x9: NOT src XOR dst */
		return "equiv";
	case GXinvert:				/* 0xa: NOT dst */
		return "invert";
	case GXorReverse:			/* 0xb: src OR NOT dst */
		return "or/reverse";
	case GXcopyInverted:		/* 0xc: NOT src */
		return "copy/inverted";
	case GXorInverted:			/* 0xd: NOT src OR dst */
		return "or/inverted";
	case GXnand:				/* 0xe: NOT src OR NOT dst */
		return "nand";
	case GXset:					/* 0xf: 1 */
		return "set";
	default:
		return "unknown GX operations";
	}
}


static Bool
PrepareCopyRGA(PixmapPtr pSrc, PixmapPtr pDst, int xdir, int ydir,
		int alu, Pixel planemask)
{
#ifdef RGA_ENABLE_COPY
	ScrnInfoPtr pScrn = xf86ScreenToScrn(pSrc->drawable.pScreen);
	struct ARMSOCRec *pARMSOC = ARMSOCPTR(pScrn);
	struct RockchipRGAEXARec *RGAExa = 
			(struct RockchipRGAEXARec*)(pARMSOC->pARMSOCEXA);

	struct ARMSOCPixmapPrivRec *privSrc = exaGetPixmapDriverPrivate(pSrc);
	struct ARMSOCPixmapPrivRec *privDst = exaGetPixmapDriverPrivate(pDst);


	struct EXApixRGAimg *pImg;

	DEBUG_MSG("src = %p, dst = %p, alu = %s, "
		"planemask = 0x%x, xdir = %d, ydir = %d", pSrc,
		pDst, translate_gxop(alu), (unsigned int)planemask,
		xdir, ydir);

	if (pSrc->drawable.depth < 8 || pDst->drawable.depth < 8)
		goto fail;

	if (alu != GXcopy)
		goto fail;

	if (planemask != 0xffffffff)
		goto fail;

	pImg = calloc(1, sizeof(struct EXApixRGAimg));

	/* translate pSrc to rga_image */
	pImg->pSrc = pSrc;

	pImg->simg.color_mode = DRM_FORMAT_ARGB8888;
	pImg->simg.width = pSrc->drawable.width;
	pImg->simg.height = pSrc->drawable.height;
	pImg->simg.stride = exaGetPixmapPitch(pSrc);
	pImg->simg.buf_type = RGA_IMGBUF_GEM;
	pImg->simg.bo[0] = armsoc_bo_handle(privSrc->bo);

	/* translate pDst to rga_image */
	pImg->pDst = pDst;
	pImg->dimg.color_mode = DRM_FORMAT_ARGB8888;
	pImg->dimg.width = pDst->drawable.width;
	pImg->dimg.height = pDst->drawable.height;
	pImg->dimg.stride = exaGetPixmapPitch(pDst);
	pImg->dimg.buf_type = RGA_IMGBUF_GEM;
	pImg->dimg.bo[0] = armsoc_bo_handle(privDst->bo);

	RGAExa->priv = (void *)pImg;

	return TRUE;

fail:
	return FALSE;
#else
	return FALSE;
#endif
}

static void
CopyRGA(PixmapPtr pDstPixmap, int srcX, int srcY, int dstX, int dstY,
		int width, int height)
{
#ifdef RGA_ENABLE_COPY
	ScrnInfoPtr pScrn = xf86ScreenToScrn(pDstPixmap->drawable.pScreen);
	struct ARMSOCRec *pARMSOC = ARMSOCPTR(pScrn);
	struct RockchipRGAEXARec *RGAExa = 
			(struct RockchipRGAEXARec*)(pARMSOC->pARMSOCEXA);
	struct EXApixRGAimg *pRGA;

	DEBUG_MSG("dst = %p, src_x = %d, src_y = %d, "
		"dst_x = %d, dst_y = %d, w = %d, h = %d", pDstPixmap,
		srcX, srcY, dstX, dstY, width, height);

/**
 * rga_copy - copy contents in source buffer to destination buffer.
 *
 * @ctx: a pointer to rga_context structure.
 * @src: a pointer to rga_image structure including image and buffer
 *	information to source.
 * @dst: a pointer to rga_image structure including image and buffer
 *	information to destination.
 * @src_x: x start position to source buffer.
 * @src_y: y start position to source buffer.
 * @dst_x: x start position to destination buffer.
 * @dst_y: y start position to destination buffer.
 * @w: width value to source and destination buffers.
 * @h: height value to source and destination buffers.
 */
	/*
int rga_copy(struct rga_context *ctx, struct rga_image *src,
	     struct rga_image *dst, unsigned int src_x, unsigned int src_y,
	     unsigned int dst_x, unsigned dst_y, unsigned int w,
	     unsigned int h)
	*/

	pRGA = RGAExa->priv;

	rga_copy(RGAExa->rga_ctx, &pRGA->simg, &pRGA->dimg,
			srcX, srcY, dstX, dstY, width, height);

	rga_exec(RGAExa->rga_ctx);
#endif
}

static void
DoneCopyRGA(PixmapPtr pDstPixmap)
{
#ifdef RGA_ENABLE_COPY
#if 0
	ScrnInfoPtr pScrn = xf86ScreenToScrn(pDstPixmap->drawable.pScreen);
	struct ARMSOCRec *pARMSOC = ARMSOCPTR(pScrn);
	struct RockchipRGAEXARec *RGAExa = 
			(struct RockchipRGAEXARec*)(pARMSOC->pARMSOCEXA);
	struct CopyG2DOp *copyOp;

#if defined(EXA_G2D_DEBUG_COPY)
	EARLY_INFO_MSG("DEBUG: DoneCopyRGA: dst = %p", pDstPixmap);
#endif

	assert(RGAExa->current_op == g2d_exa_op_copy);

	copyOp = RGAExa->priv;

	assert(pDstPixmap == copyOp->pDst);

	if (copyOp->num_rects == 0)
		goto out;

	// TODO: error handling
	if (copyOp->flags & g2d_exa_copy_move)
		g2d_move_multi(RGAExa->g2d_ctx, &copyOp->src,
			copyOp->src_rects, copyOp->dst_rects, copyOp->num_rects);
	else
		g2d_copy_multi(RGAExa->g2d_ctx, &copyOp->src, &copyOp->dst,
			copyOp->src_rects, copyOp->dst_rects, copyOp->num_rects);

out:
	g2d_exec(RGAExa->g2d_ctx);

	if (copyOp->flags & g2d_exa_src_userptr)
		userptr_unref(RGAExa, copyOp->pSrc);

	if (copyOp->flags & g2d_exa_dst_userptr)
		userptr_unref(RGAExa, pDstPixmap);

	free(copyOp);

	RGAExa->current_op = g2d_exa_op_unset;
	RGAExa->priv = NULL;
	
	rga_exec();
#endif
#endif
}

static Bool
PrepareSolidRGA(PixmapPtr pPixmap, int alu, Pixel planemask, Pixel fill_colour)
{
#ifdef RGA_ENABLE_SOLID
	ScrnInfoPtr pScrn = xf86ScreenToScrn(pPixmap->drawable.pScreen);
	struct ARMSOCRec *pARMSOC = ARMSOCPTR(pScrn);
	struct RockchipRGAEXARec *RGAExa = 
			(struct RockchipRGAEXARec*)(pARMSOC->pARMSOCEXA);

	struct EXApixRGAimg *pImg;

	if (pSrc->drawable.depth < 8)
		goto fail;

	if (alu != GXcopy)
		goto fail;

	if (planemask != 0xffffffff)
		goto fail;

	pImg = calloc(1, sizeof(struct EXApixRGAimg));

	/* translate pSrc to rga_image */
	pImg->pSrc = pPixmap;
	pImg->pSrc.fill_color = fill_colour;
	pImg->pSrc.color_mode = DRM_FORMAT_ARGB8888;

fail:
	return FALSE;
#else
	return FALSE;
#endif
}

static void
SolidRGA(PixmapPtr pPixmap, int x1, int y1, int x2, int y2)
{
#ifdef RGA_ENABLE_SOLID
	ScrnInfoPtr pScrn = xf86ScreenToScrn(pPixmap->drawable.pScreen);
	struct ARMSOCRec *pARMSOC = ARMSOCPTR(pScrn);
	struct RockchipRGAEXARec *RGAExa = 
			(struct RockchipRGAEXARec*)(pARMSOC->pARMSOCEXA);

	struct EXApixRGAimg *pImg;
	struct g2d_rect *rect;

	DEBUG_MSG("DEBUG: Solid2D: pixmap = %p, "
		"x1 = %d, y1 = %d, x2 = %d, y2 = %d", pPixmap,
		x1, y1, x2, y2);

	pImg = RGAExa->priv;
	
	rga_solid_fill(RGAExa->rga_ctx, dst, 0, 0, dst->width, dst->height);

#endif
}

static void
DoneSolidRGA(PixmapPtr pPixmap)
{
#ifdef RGA_ENABLE_SOLID
#if 0
	ScrnInfoPtr pScrn = xf86ScreenToScrn(pPixmap->drawable.pScreen);
	struct ARMSOCRec *pARMSOC = ARMSOCPTR(pScrn);
	struct RockchipRGAEXARec *RGAExa = 
			(struct RockchipRGAEXARec*)(pARMSOC->pARMSOCEXA);

	struct SolidG2DOp *solidOp;

#if defined(EXA_G2D_DEBUG_SOLID)
	EARLY_INFO_MSG("DEBUG: DoneSolidRGA: pixmap = %p", pPixmap);
#endif

	assert(RGAExa->current_op == g2d_exa_op_solid);

	solidOp = RGAExa->priv;

	assert(pPixmap == solidOp->pDst);

	if (solidOp->num_rects == 0)
		goto out;

	// TODO: error handling
	g2d_solid_fill_multi(RGAExa->g2d_ctx, &solidOp->dst, solidOp->rects, solidOp->num_rects);

out:
	g2d_exec(RGAExa->g2d_ctx);

	if (solidOp->flags & g2d_exa_dst_userptr)
		userptr_unref(RGAExa, pPixmap);

	free(solidOp);

	RGAExa->current_op = g2d_exa_op_unset;
	RGAExa->priv = NULL;
#endif
#endif
}

static Bool
CheckCompositeFail(int op, PicturePtr pSrcPicture, PicturePtr pMaskPicture,
		PicturePtr pDstPicture)
{
#ifdef RGA_ENABLE_COMPOSITE
#else
	return FALSE;
#endif
}

static Bool
PrepareCompositeFail(int op, PicturePtr pSrcPicture, PicturePtr pMaskPicture,
		PicturePtr pDstPicture, PixmapPtr pSrc,
		PixmapPtr pMask, PixmapPtr pDst)
{
#ifdef RGA_ENABLE_COMPOSITE
#else
	return FALSE;
#endif
}

/**
 * CloseScreen() is called at the end of each server generation and
 * cleans up everything initialised in InitNullEXA()
 */
static Bool
CloseScreen(CLOSE_SCREEN_ARGS_DECL)
{
	ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
	struct ARMSOCRec *pARMSOC = ARMSOCPTR(pScrn);

	exaDriverFini(pScreen);
	free(((struct RockchipRGAEXARec *)pARMSOC->pARMSOCEXA)->exa);
	free(pARMSOC->pARMSOCEXA);
	pARMSOC->pARMSOCEXA = NULL;

	return TRUE;
}

/* FreeScreen() is called on an error during PreInit and
 * should clean up anything initialised before InitNullEXA()
 * (which currently is nothing)
 *
 */
static void
FreeScreen(FREE_SCREEN_ARGS_DECL)
{
}

struct ARMSOCEXARec *
InitRockchipRGAEXA(ScreenPtr pScreen, ScrnInfoPtr pScrn, int fd)
{
	struct RockchipRGAEXARec *RGAExa;
	struct ARMSOCEXARec *armsoc_exa;
	ExaDriverPtr exa;
	struct rga_context *rga_ctx;

	INFO_MSG("Rockchip/RGA EXA mode");

	RGAExa = calloc(1, sizeof(*RGAExa));
	if (!RGAExa)
		goto out;

	armsoc_exa = (struct ARMSOCEXARec *)RGAExa;

	exa = exaDriverAlloc();
	if (!exa)
		goto free_RGAExa;

	RGAExa->exa = exa;

	rga_ctx = rga_init(fd);
	if (!rga_ctx)
		goto free_exa;

	RGAExa->rga_ctx = rga_ctx;

	exa->exa_major = EXA_VERSION_MAJOR;
	exa->exa_minor = EXA_VERSION_MINOR;

	exa->pixmapOffsetAlign = 0;
	exa->pixmapPitchAlign = 32;
	exa->flags = EXA_OFFSCREEN_PIXMAPS |
			EXA_HANDLES_PIXMAPS | EXA_SUPPORTS_PREPARE_AUX;
	exa->maxX = 4096;
	exa->maxY = 4096;

	/* Required EXA functions: */
	exa->WaitMarker = ARMSOCWaitMarker;
	exa->CreatePixmap2 = ARMSOCCreatePixmap2;
	exa->DestroyPixmap = ARMSOCDestroyPixmap;
	exa->ModifyPixmapHeader = ARMSOCModifyPixmapHeader;

	exa->PrepareAccess = ARMSOCPrepareAccess;
	exa->FinishAccess = ARMSOCFinishAccess;
	exa->PixmapIsOffscreen = ARMSOCPixmapIsOffscreen;

	/* Accelerate copy and solid fill calls with the RGA. */
	exa->PrepareCopy = PrepareCopyRGA;
	exa->Copy = CopyRGA;
	exa->DoneCopy = DoneCopyRGA;
	exa->PrepareSolid = PrepareSolidRGA;
	exa->Solid = SolidRGA;
	exa->DoneSolid = DoneSolidRGA;

	/* Always fallback for software operations for composite for now. */
	exa->CheckComposite = CheckCompositeFail;
	exa->PrepareComposite = PrepareCompositeFail;

	if (!exaDriverInit(pScreen, exa)) {
		ERROR_MSG("exaDriverInit failed");
		goto free_exa;
	}

	armsoc_exa->CloseScreen = CloseScreen;
	armsoc_exa->FreeScreen = FreeScreen;

	return armsoc_exa;

free_exa:
	free(exa);
free_RGAExa:
	free(RGAExa);
out:
	return NULL;
}

