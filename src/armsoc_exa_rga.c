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

#include <time.h>

/* This file has a trivial EXA implementation which accelerates nothing.  It
 * is used as the fall-back in case the EXA implementation for the current
 * chipset is not available.  (For example, on chipsets which used the closed
 * source IMG PowerVR EXA implementation, if the closed-source submodule is
 * not installed.
 */

struct EXAsrcRGAimg {
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


static Bool
PrepareCopyRGA(PixmapPtr pSrc, PixmapPtr pDst, int xdir, int ydir,
		int alu, Pixel planemask)
{
	struct ARMSOCPixmapPrivRec *privSrc = exaGetPixmapDriverPrivate(pSrc);
	struct ARMSOCPixmapPrivRec *privDst = exaGetPixmapDriverPrivate(pDst);
	struct RockchipRGAEXARec *RGAExa;
	struct EXApRGAimg *pImg;

	if (pSrc->drawable.depth < 8 || pDst->drawable.depth < 8)
		goto fail;

	if (pSrc == pDst)
		goto out;

	pImg = calloc(1, sizeof(struct EXApRGAimg));

	pImg->pSrc = pSrc;
	pImg->simg.color_mode = DRM_FORMAT_ARG8888;
	pImg->simg.width = pSrc->drawable.width;
	pImg->simg.height = pSrc->drawable.height;
	pImg->simg.stride = exaGetPixmapPitch(pSrc);
	pImg->simg.buf_type = RGA_IMGBUF_GEM;
	pImg->simg.bo[0] = armsoc_bo_handle(privSrc->bo);

	pImg->pDst = pDst;
	pImg->dimg.color_mode = translate_pixmap_depth(pDst);
	pImg->dimg.width = pDst->drawable.width;
	pImg->dimg.height = pDst->drawable.height;
	pImg->dimg.stride = exaGetPixmapPitch(pDst);
	pImg->dimg.buf_type = RGA_IMGBUF_GEM;
	pImg->dimg.bo[0] = armsoc_bo_handle(privDst->bo);

	RGAExa->priv = (void *)pImg;

out:
	return TRUE;

fail:
	return FALSE;
}

static void
CopyRGA(PixmapPtr pDstPixmap, int srcX, int srcY, int dstX, int dstY,
		int width, int height)
{
#if 0
	struct RockchipRGAEXARec *g2dPriv = G2DPrivFromPixmap(pDstPixmap);
	struct EXApRGAimg *pRGA;
	struct g2d_rect *src_rect, *dst_rect;

#if defined(EXA_G2D_DEBUG_COPY)
	EARLY_INFO_MSG("DEBUG: CopyRGA: dst = %p, src_x = %d, src_y = %d, "
		"dst_x = %d, dst_y = %d, w = %d, h = %d", pDstPixmap,
		srcX, srcY, dstX, dstY, width, height);
#endif

	assert(g2dPriv->current_op == g2d_exa_op_copy);

	copyOp = g2dPriv->op_data;

	if (copyOp->num_rects == g2d_exa_copy_batch) {
		// TODO: error handling
		if (copyOp->flags & g2d_exa_copy_move)
			g2d_move_multi(g2dPriv->g2d_ctx, &copyOp->src,
				copyOp->src_rects, copyOp->dst_rects, g2d_exa_copy_batch);
		else
			g2d_copy_multi(g2dPriv->g2d_ctx, &copyOp->src, &copyOp->dst,
				copyOp->src_rects, copyOp->dst_rects, g2d_exa_copy_batch);

		copyOp->num_rects = 0;
	}

	src_rect = &copyOp->src_rects[copyOp->num_rects];
	dst_rect = &copyOp->dst_rects[copyOp->num_rects];

	assert(pDstPixmap == copyOp->pDst);

	src_rect->x = srcX;
	src_rect->y = srcY;
	src_rect->w = width;
	src_rect->h = height;

	dst_rect->x = dstX;
	dst_rect->y = dstY;

	copyOp->num_rects++;
#endif
	rga_copy();
}

static void
DoneCopyRGA(PixmapPtr pDstPixmap)
{
#if 0
	struct RockchipRGAEXARec *g2dPriv = G2DPrivFromPixmap(pDstPixmap);
	struct CopyG2DOp *copyOp;

#if defined(EXA_G2D_DEBUG_COPY)
	EARLY_INFO_MSG("DEBUG: DoneCopyRGA: dst = %p", pDstPixmap);
#endif

	assert(g2dPriv->current_op == g2d_exa_op_copy);

	copyOp = g2dPriv->op_data;

	assert(pDstPixmap == copyOp->pDst);

	if (copyOp->num_rects == 0)
		goto out;

	// TODO: error handling
	if (copyOp->flags & g2d_exa_copy_move)
		g2d_move_multi(g2dPriv->g2d_ctx, &copyOp->src,
			copyOp->src_rects, copyOp->dst_rects, copyOp->num_rects);
	else
		g2d_copy_multi(g2dPriv->g2d_ctx, &copyOp->src, &copyOp->dst,
			copyOp->src_rects, copyOp->dst_rects, copyOp->num_rects);

out:
	g2d_exec(g2dPriv->g2d_ctx);

	if (copyOp->flags & g2d_exa_src_userptr)
		userptr_unref(g2dPriv, copyOp->pSrc);

	if (copyOp->flags & g2d_exa_dst_userptr)
		userptr_unref(g2dPriv, pDstPixmap);

	free(copyOp);

	g2dPriv->current_op = g2d_exa_op_unset;
	g2dPriv->op_data = NULL;
#endif
}

static Bool
PrepareSolidRGA(PixmapPtr pPixmap, int alu, Pixel planemask, Pixel fill_colour)
{
	return FALSE;
}

static void
SolidRGA(PixmapPtr pPixmap, int x1, int y1, int x2, int y2)
{
#if 0
	struct RockchipRGAEXARec *g2dPriv = G2DPrivFromPixmap(pPixmap);
	struct SolidG2DOp *solidOp;
	struct g2d_rect *rect;

#if defined(EXA_G2D_DEBUG_SOLID)
	EARLY_INFO_MSG("DEBUG: Solid2D: pixmap = %p, "
		"x1 = %d, y1 = %d, x2 = %d, y2 = %d", pPixmap,
		x1, y1, x2, y2);
#endif

	assert(g2dPriv->current_op == g2d_exa_op_solid);

	solidOp = g2dPriv->op_data;
	
	if (solidOp->num_rects == g2d_exa_solid_batch) {
		// TODO: error handling
		g2d_solid_fill_multi(g2dPriv->g2d_ctx, &solidOp->dst, solidOp->rects, g2d_exa_solid_batch);

		solidOp->num_rects = 0;
	}

	rect = &solidOp->rects[solidOp->num_rects];

	assert(pPixmap == solidOp->pDst);

	rect->x = x1;
	rect->y = y1;
	rect->w = x2 - x1;
	rect->h = y2 - y1;

	solidOp->num_rects++;
#endif
}

static void
DoneSolidRGA(PixmapPtr pPixmap)
{
#if 0
	struct RockchipRGAEXARec *g2dPriv = G2DPrivFromPixmap(pPixmap);
	struct SolidG2DOp *solidOp;

#if defined(EXA_G2D_DEBUG_SOLID)
	EARLY_INFO_MSG("DEBUG: DoneSolidRGA: pixmap = %p", pPixmap);
#endif

	assert(g2dPriv->current_op == g2d_exa_op_solid);

	solidOp = g2dPriv->op_data;

	assert(pPixmap == solidOp->pDst);

	if (solidOp->num_rects == 0)
		goto out;

	// TODO: error handling
	g2d_solid_fill_multi(g2dPriv->g2d_ctx, &solidOp->dst, solidOp->rects, solidOp->num_rects);

out:
	g2d_exec(g2dPriv->g2d_ctx);

	if (solidOp->flags & g2d_exa_dst_userptr)
		userptr_unref(g2dPriv, pPixmap);

	free(solidOp);

	g2dPriv->current_op = g2d_exa_op_unset;
	g2dPriv->op_data = NULL;
#endif
}

static Bool
CheckCompositeFail(int op, PicturePtr pSrcPicture, PicturePtr pMaskPicture,
		PicturePtr pDstPicture)
{
	return FALSE;
}

static Bool
PrepareCompositeFail(int op, PicturePtr pSrcPicture, PicturePtr pMaskPicture,
		PicturePtr pDstPicture, PixmapPtr pSrc,
		PixmapPtr pMask, PixmapPtr pDst)
{
	return FALSE;
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

