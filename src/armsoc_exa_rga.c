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
#include <xorg/fb.h>

#include <time.h>
#include <unistd.h>

#define RGA_ENABLE_COPY
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

 	GCPtr pGC;
	Bool reverse;
	Bool upsidedown;
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

static unsigned int
translate_pixmap_depth(PixmapPtr pPixmap)
{
	switch (pPixmap->drawable.depth) {
    case 32:
        return DRM_FORMAT_ARGB8888;

    case 24:
	if (pPixmap->drawable.bitsPerPixel == 32)
		return DRM_FORMAT_XRGB8888;
	else
		return DRM_FORMAT_RGB888;

    case 16:
        return DRM_FORMAT_RGB565;

    case 8:
        return DRM_FORMAT_C8;

    default:
		assert(0);
		return 0;
    }
}

 static Bool
 RGAIsSupport(PixmapPtr pPix, int Mask)
 {
 	struct ARMSOCPixmapPrivRec *priv;

 	if(!pPix)
		return TRUE;

 	if(Mask != -1)
 		return FALSE;

 	if (pPix->drawable.width < 34 || pPix->drawable.height < 34)
 		return FALSE;

 	if (pPix->drawable.depth != 32 && pPix->drawable.depth != 24)
 		return FALSE;

 	priv = exaGetPixmapDriverPrivate(pPix);
 	if(!priv->bo)
 		return FALSE;

 	return TRUE;
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
	ChangeGCVal gcval[2];

	struct EXApixRGAimg *pImg;

	DEBUG_MSG("src = %p, dst = %p, alu = %s, "
		"planemask = 0x%x, xdir = %d, ydir = %d", pSrc,
		pDst, translate_gxop(alu), (unsigned int)planemask,
		xdir, ydir);

 	if (!RGAIsSupport(pSrc, planemask) ||
 		!RGAIsSupport(pDst, planemask))
		return FALSE;

	if (alu != GXcopy)
		return FALSE;

	pImg = calloc(1, sizeof(struct EXApixRGAimg));

	/* translate pSrc to rga_image */
	pImg->pSrc = pSrc;

	/* Prepare GC configuration */
	pImg->pGC = GetScratchGC(pDst->drawable.depth,
			pDst->drawable.pScreen);
	if (!pImg->pGC) {
		free(pImg);
		return FALSE;
	}

	gcval[0].val = alu;
	gcval[1].val = planemask;
	ChangeGC(NullClient, pImg->pGC, GCFunction | GCPlaneMask, gcval);
	ValidateGC(&pDst->drawable, pImg->pGC);

	pImg->simg.color_mode = translate_pixmap_depth(pSrc);
	pImg->simg.width = pSrc->drawable.width;
	pImg->simg.height = pSrc->drawable.height;
	pImg->simg.stride = exaGetPixmapPitch(pSrc);
	pImg->simg.buf_type = RGA_IMGBUF_GEM;

	ARMSOCRegisterExternalAccess(pSrc);
	if (!ARMSOCPrepareAccess(pSrc, EXA_NUM_PREPARE_INDICES)) {
		FreeScratchGC(pImg->pGC);
		free(pImg);
		return FALSE;
	}
	pImg->simg.bo[0] = armsoc_bo_dmabuf(privSrc->bo);

	/* translate pDst to rga_image */
	pImg->pDst = pDst;
	pImg->dimg.color_mode = translate_pixmap_depth(pDst);
	pImg->dimg.width = pDst->drawable.width;
	pImg->dimg.height = pDst->drawable.height;
	pImg->dimg.stride = exaGetPixmapPitch(pDst);
	pImg->dimg.buf_type = RGA_IMGBUF_GEM;

	ARMSOCRegisterExternalAccess(pDst);
	if (!ARMSOCPrepareAccess(pDst, EXA_NUM_PREPARE_INDICES)) {
		FreeScratchGC(pImg->pGC);
		free(pImg);
		return FALSE;
	}

	pImg->dimg.bo[0] = armsoc_bo_dmabuf(privDst->bo);

	/* Record reverse & upsidedown, we maybe use it by cpu copying */
	pImg->reverse = (xdir > 0 ? 0 : 1);
	pImg->upsidedown = (ydir > 0 ? 0 : 1);

	RGAExa->priv = (void *)pImg;

	return TRUE;
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

	pRGA = RGAExa->priv;

	/* If drawable is too small, we need to blt it by CPU */
	if (width < 34 || height < 34) {
		FbBits *src;
		FbStride srcStride;
		int srcBpp;
		FbBits *dst;
		FbStride dstStride;
		int dstBpp;


		DEBUG_MSG("[Debug_Sugar]: fbBlt by CPU............");
		fbGetPixmapBitsData(pRGA->pSrc, src, srcStride, srcBpp);
		fbGetPixmapBitsData(pRGA->pDst, dst, dstStride, dstBpp);

		fbBlt(src + srcY * srcStride, srcStride, srcX * srcBpp,
				dst + dstY * dstStride, dstStride, dstX * dstBpp,
				width * dstBpp, height,
				GXcopy, fbGetGCPrivate(pRGA->pGC)->pm,
				dstBpp, pRGA->reverse, pRGA->upsidedown);
	} else {
		rga_copy(RGAExa->rga_ctx, &pRGA->simg, &pRGA->dimg,
				srcX, srcY, dstX, dstY, width, height);
		rga_exec(RGAExa->rga_ctx);
	}
#endif
}

static void
DoneCopyRGA(PixmapPtr pDstPixmap)
{
#ifdef RGA_ENABLE_COPY

	ScrnInfoPtr pScrn = xf86ScreenToScrn(pDstPixmap->drawable.pScreen);
	struct ARMSOCRec *pARMSOC = ARMSOCPTR(pScrn);
	struct RockchipRGAEXARec *RGAExa =
			(struct RockchipRGAEXARec*)(pARMSOC->pARMSOCEXA);

	struct EXApixRGAimg *pImg = RGAExa->priv;

	ARMSOCDeregisterExternalAccess(pImg->pSrc);
	ARMSOCFinishAccess(pImg->pSrc, EXA_NUM_PREPARE_INDICES);

	ARMSOCDeregisterExternalAccess(pImg->pDst);
	ARMSOCFinishAccess(pImg->pDst, EXA_NUM_PREPARE_INDICES);

	FreeScratchGC(pImg->pGC);
	free(pImg);
	RGAExa->priv = NULL;
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

	struct ARMSOCPixmapPrivRec *pixPriv = exaGetPixmapDriverPrivate(pPixmap);

	struct EXApixRGAimg *pImg;
 	ChangeGCVal gcval[3];

	DEBUG_MSG("RGA: pixmap = %p, alu = %s, "
		"planemask = 0x%x color = 0x%x",
		pPixmap, translate_gxop(alu),
		(unsigned int)planemask, (unsigned int)fill_colour);

	if (!RGAIsSupport(pPixmap, planemask))
		return FALSE;

	if (alu != GXcopy)
		return FALSE;

	pImg = calloc(1, sizeof(struct EXApixRGAimg));

	/* translate pSrc to rga_image */
	pImg->pDst = pPixmap;

	pImg->dimg.fill_color = fill_colour;
	pImg->dimg.color_mode = translate_pixmap_depth(pPixmap);
	pImg->dimg.width = pPixmap->drawable.width;
	pImg->dimg.height = pPixmap->drawable.height;
	pImg->dimg.stride = exaGetPixmapPitch(pPixmap);
	pImg->dimg.buf_type = RGA_IMGBUF_GEM;

	ARMSOCRegisterExternalAccess(pPixmap);
	if (!ARMSOCPrepareAccess(pPixmap, EXA_NUM_PREPARE_INDICES))
		goto fail;

	/* Set dma_buf fd */
	pImg->dimg.bo[0] = armsoc_bo_dmabuf(pixPriv->bo);

	/* Prepare GC configuration */
	pImg->pGC = GetScratchGC(pPixmap->drawable.depth,
			pPixmap->drawable.pScreen);
	if (!pImg->pGC) {
		ARMSOCFinishAccess(pPixmap, EXA_NUM_PREPARE_INDICES);
		goto fail;
	}

 	gcval[0].val = alu;
 	gcval[1].val = planemask;
 	gcval[2].val = fill_colour;
 	ChangeGC (NullClient, pImg->pGC, GCFunction|GCPlaneMask|GCForeground, gcval);
 	ValidateGC (&pPixmap->drawable, pImg->pGC);

	RGAExa->priv = (void *)pImg;

	return TRUE;

fail:
	free(pImg);
#endif
	return FALSE;
}

static void
SolidRGA(PixmapPtr pPixmap, int x1, int y1, int x2, int y2)
{
#ifdef RGA_ENABLE_SOLID
	ScrnInfoPtr pScrn = xf86ScreenToScrn(pPixmap->drawable.pScreen);
	struct ARMSOCRec *pARMSOC = ARMSOCPTR(pScrn);
	struct RockchipRGAEXARec *RGAExa =
			(struct RockchipRGAEXARec*)(pARMSOC->pARMSOCEXA);

	struct ARMSOCPixmapPrivRec *pixPriv = exaGetPixmapDriverPrivate(pPixmap);

	struct EXApixRGAimg *pImg;


	DEBUG_MSG("RGA: pixmap = %p, "
		"x1 = %d, y1 = %d, x2 = %d, y2 = %d", pPixmap,
		x1, y1, x2, y2);

	pImg = RGAExa->priv;

	if ((x2 - x1) < 34 || (y2 - y1) < 34) {
		pPixmap->devPrivate.ptr  = armsoc_bo_map(pixPriv->bo);
		fbFill(&pPixmap->drawable, pImg->pGC, x1, y1, x2 - x1, y2 - y1);
		pPixmap->devPrivate.ptr  = NULL;
	} else {
		rga_solid_fill(RGAExa->rga_ctx, &pImg->dimg, x1, y1, x2 - x1, y2 - y1);
		rga_exec(RGAExa->rga_ctx);
	}
#endif
}

static void
DoneSolidRGA(PixmapPtr pPixmap)
{
#ifdef RGA_ENABLE_SOLID
	ScrnInfoPtr pScrn = xf86ScreenToScrn(pPixmap->drawable.pScreen);
	struct ARMSOCRec *pARMSOC = ARMSOCPTR(pScrn);
	struct RockchipRGAEXARec *RGAExa =
			(struct RockchipRGAEXARec*)(pARMSOC->pARMSOCEXA);

	struct EXApixRGAimg *pImg = RGAExa->priv;

	ARMSOCDeregisterExternalAccess(pPixmap);
	ARMSOCFinishAccess(pPixmap, EXA_NUM_PREPARE_INDICES);
	FreeScratchGC(pImg->pGC);
	free(pImg);
	RGAExa->priv = NULL;
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

