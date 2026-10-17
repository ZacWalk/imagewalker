// ImageWalker by Zac Walker
//
// Purpose: Lossless JPEG transform implementation, adapted from jpegtran.
//
// Based on transupp.c from the Independent JPEG Group's software, by Thomas
//          G. Lane. The original notice below applies.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

/*
* transupp.c
*
* Copyright (C) 1997, Thomas G. Lane.
* This file is part of the Independent JPEG Group's software.
* For conditions of distribution and use, see the accompanying README file.
*
* This file contains image transformation routines and other utility code
* used by the jpegtran sample application.  These are NOT part of the core
* JPEG library.  But we keep these routines separate from jpegtran.c to
* ease the task of maintaining jpegtran-like programs that have other user
* interfaces.
*/

/* Although this file really shouldn't have access to the library internals,
* it's helpful to let it call jround_up() and jcopy_block_row().
*/

#include "stdafx.h"


#include "FileJpegTran.h"		/* My own external interface */
#include "FileJpeg.h"
#include "MetadataExif.h"


#if TRANSFORMS_SUPPORTED

/*
* Lossless image transformation routines.  These routines work on DCT
* coefficient arrays and thus do not require any lossy decompression
* or recompression of the image.
* Thanks to Guido Vollbeding for the initial design and code of this feature,
* and to Ben Jackson for introducing the cropping feature.
*
* Horizontal flipping is done in-place, using a single top-to-bottom
* pass through the virtual source array.  It will thus be much the
* fastest option for images larger than main memory.
*
* The other routines require a set of destination virtual arrays, so they
* need twice as much memory as jpegtran normally does.  The destination
* arrays are always written in normal scan order (top to bottom) because
* the virtual array manager expects this.  The source arrays will be scanned
* in the corresponding order, which means multiple passes through the source
* arrays for most of the transforms.  That could result in much thrashing
* if the image is larger than main memory.
*
* If cropping or trimming is involved, the destination arrays may be smaller
* than the source arrays.  Note it is not possible to do horizontal flip
* in-place when a nonzero Y crop offset is specified, since we'd have to move
* data from one block row to another but the virtual array manager doesn't
* guarantee we can touch more than one row at a time.  So in that case,
* we have to use a separate destination array.
*
* Some notes about the operating environment of the individual transform
* routines:
* 1. Both the source and destination virtual arrays are allocated from the
*    source JPEG object, and therefore should be manipulated by calling the
*    source's memory manager.
* 2. The destination's component count should be used.  It may be smaller
*    than the source's when forcing to grayscale.
* 3. Likewise the destination's sampling factors should be used.  When
*    forcing to grayscale the destination's sampling factors will be all 1,
*    and we may as well take that as the effective iMCU size.
* 4. When "trim" is in effect, the destination's dimensions will be the
*    trimmed values but the source's will be untrimmed.
* 5. When "crop" is in effect, the destination's dimensions will be the
*    cropped values but the source's will be uncropped.  Each transform
*    routine is responsible for picking up source data starting at the
*    correct X and Y offset for the crop region.  (The X and Y offsets
*    passed to the transform routines are measured in iMCU blocks of the
*    destination.)
* 6. All the routines assume that the source and destination buffers are
*    padded out to a full iMCU boundary.  This is true, although for the
*    source buffer it is an undocumented property of jdcoefct.c.
*/

static int CountSteps(j_compress_ptr dstinfo)
{
	int nSteps = 0;

	for (int ci = 0; ci < dstinfo->num_components; ci++)
	{
		jpeg_component_info* compptr = dstinfo->comp_info + ci;
		nSteps += compptr->height_in_blocks / compptr->v_samp_factor;
	}

	return nSteps;
}

LOCAL(void)
do_crop(j_decompress_ptr srcinfo, j_compress_ptr dstinfo,
        JDIMENSION x_crop_offset, JDIMENSION y_crop_offset,
        jvirt_barray_ptr* src_coef_arrays,
        jvirt_barray_ptr* dst_coef_arrays,
        IW::IStatus* pStatus)
/* Crop.  This is only used when no rotate/flip is requested with the crop. */
{
	JDIMENSION dst_blk_y, x_crop_blocks, y_crop_blocks;
	int ci, offset_y;
	JBLOCKARRAY src_buffer, dst_buffer;
	jpeg_component_info* compptr;
	int nStep = 0, nTotalSteps = CountSteps(dstinfo);

	/* We simply have to copy the right amount of data (the destination's
	* image size) starting at the given X and Y offsets in the source.
	*/
	for (ci = 0; ci < dstinfo->num_components; ci++)
	{
		compptr = dstinfo->comp_info + ci;
		x_crop_blocks = x_crop_offset * compptr->h_samp_factor;
		y_crop_blocks = y_crop_offset * compptr->v_samp_factor;
		for (dst_blk_y = 0; dst_blk_y < compptr->height_in_blocks;
		     dst_blk_y += compptr->v_samp_factor)
		{
			pStatus->Progress(nStep++, nTotalSteps);
			if (pStatus->QueryCancel()) return;

			dst_buffer = (*srcinfo->mem->access_virt_barray)
			((j_common_ptr)srcinfo, dst_coef_arrays[ci], dst_blk_y,
			 static_cast<JDIMENSION>(compptr->v_samp_factor), TRUE);
			src_buffer = (*srcinfo->mem->access_virt_barray)
			((j_common_ptr)srcinfo, src_coef_arrays[ci],
			 dst_blk_y + y_crop_blocks,
			 static_cast<JDIMENSION>(compptr->v_samp_factor), FALSE);
			for (offset_y = 0; offset_y < compptr->v_samp_factor; offset_y++)
			{
				jcopy_block_row(src_buffer[offset_y] + x_crop_blocks,
				                dst_buffer[offset_y],
				                compptr->width_in_blocks);
			}
		}
	}
}


LOCAL(void)
do_flip_h_no_crop(j_decompress_ptr srcinfo, j_compress_ptr dstinfo,
                  JDIMENSION x_crop_offset,
                  jvirt_barray_ptr* src_coef_arrays,
                  IW::IStatus* pStatus)
/* Horizontal flip; done in-place, so no separate dest array is required.
* NB: this only works when y_crop_offset is zero.
*/
{
	JDIMENSION MCU_cols, comp_width, blk_x, blk_y, x_crop_blocks;
	int ci, k, offset_y;
	JBLOCKARRAY buffer;
	JCOEFPTR ptr1, ptr2;
	JCOEF temp1, temp2;
	jpeg_component_info* compptr;

	/* Horizontal mirroring of DCT blocks is accomplished by swapping
	* pairs of blocks in-place.  Within a DCT block, we perform horizontal
	* mirroring by changing the signs of odd-numbered columns.
	* Partial iMCUs at the right edge are left untouched.
	*/
	MCU_cols = srcinfo->image_width / (dstinfo->max_h_samp_factor * DCTSIZE);

	for (ci = 0; ci < dstinfo->num_components; ci++)
	{
		compptr = dstinfo->comp_info + ci;
		comp_width = MCU_cols * compptr->h_samp_factor;
		x_crop_blocks = x_crop_offset * compptr->h_samp_factor;
		for (blk_y = 0; blk_y < compptr->height_in_blocks;
		     blk_y += compptr->v_samp_factor)
		{
			buffer = (*srcinfo->mem->access_virt_barray)
			((j_common_ptr)srcinfo, src_coef_arrays[ci], blk_y,
			 static_cast<JDIMENSION>(compptr->v_samp_factor), TRUE);
			for (offset_y = 0; offset_y < compptr->v_samp_factor; offset_y++)
			{
				/* Do the mirroring */
				for (blk_x = 0; blk_x * 2 < comp_width; blk_x++)
				{
					ptr1 = buffer[offset_y][blk_x];
					ptr2 = buffer[offset_y][comp_width - blk_x - 1];
					/* this unrolled loop doesn't need to know which row it's on... */
					for (k = 0; k < DCTSIZE2; k += 2)
					{
						temp1 = *ptr1; /* swap even column */
						temp2 = *ptr2;
						*ptr1++ = temp2;
						*ptr2++ = temp1;
						temp1 = *ptr1; /* swap odd column with sign change */
						temp2 = *ptr2;
						*ptr1++ = -temp2;
						*ptr2++ = -temp1;
					}
				}
				if (x_crop_blocks > 0)
				{
					/* Now left-justify the portion of the data to be kept.
					* We can't use a single jcopy_block_row() call because that routine
					* depends on IW::MemCopy(), whose behavior is unspecified for overlapping
					* source and destination areas.  Sigh.
					*/
					for (blk_x = 0; blk_x < compptr->width_in_blocks; blk_x++)
					{
						jcopy_block_row(buffer[offset_y] + blk_x + x_crop_blocks,
						                buffer[offset_y] + blk_x,
						                1);
					}
				}
			}
		}
	}
}


LOCAL(void)
do_flip_h(j_decompress_ptr srcinfo, j_compress_ptr dstinfo,
          JDIMENSION x_crop_offset, JDIMENSION y_crop_offset,
          jvirt_barray_ptr* src_coef_arrays,
          jvirt_barray_ptr* dst_coef_arrays,
          IW::IStatus* pStatus)
/* Horizontal flip in general cropping case */
{
	JDIMENSION MCU_cols, comp_width, dst_blk_x, dst_blk_y;
	JDIMENSION x_crop_blocks, y_crop_blocks;
	int ci, k, offset_y;
	JBLOCKARRAY src_buffer, dst_buffer;
	JBLOCKROW src_row_ptr, dst_row_ptr;
	JCOEFPTR src_ptr, dst_ptr;
	jpeg_component_info* compptr;
	int nStep = 0, nTotalSteps = CountSteps(dstinfo);

	/* Here we must output into a separate array because we can't touch
	* different rows of a single virtual array simultaneously.  Otherwise,
	* this is essentially the same as the routine above.
	*/
	MCU_cols = srcinfo->image_width / (dstinfo->max_h_samp_factor * DCTSIZE);

	for (ci = 0; ci < dstinfo->num_components; ci++)
	{
		compptr = dstinfo->comp_info + ci;
		comp_width = MCU_cols * compptr->h_samp_factor;
		x_crop_blocks = x_crop_offset * compptr->h_samp_factor;
		y_crop_blocks = y_crop_offset * compptr->v_samp_factor;
		for (dst_blk_y = 0; dst_blk_y < compptr->height_in_blocks;
		     dst_blk_y += compptr->v_samp_factor)
		{
			pStatus->Progress(nStep++, nTotalSteps);
			if (pStatus->QueryCancel()) return;

			dst_buffer = (*srcinfo->mem->access_virt_barray)
			((j_common_ptr)srcinfo, dst_coef_arrays[ci], dst_blk_y,
			 static_cast<JDIMENSION>(compptr->v_samp_factor), TRUE);
			src_buffer = (*srcinfo->mem->access_virt_barray)
			((j_common_ptr)srcinfo, src_coef_arrays[ci],
			 dst_blk_y + y_crop_blocks,
			 static_cast<JDIMENSION>(compptr->v_samp_factor), FALSE);
			for (offset_y = 0; offset_y < compptr->v_samp_factor; offset_y++)
			{
				dst_row_ptr = dst_buffer[offset_y];
				src_row_ptr = src_buffer[offset_y];
				for (dst_blk_x = 0; dst_blk_x < compptr->width_in_blocks; dst_blk_x++)
				{
					if (x_crop_blocks + dst_blk_x < comp_width)
					{
						/* Do the mirrorable blocks */
						dst_ptr = dst_row_ptr[dst_blk_x];
						src_ptr = src_row_ptr[comp_width - x_crop_blocks - dst_blk_x - 1];
						/* this unrolled loop doesn't need to know which row it's on... */
						for (k = 0; k < DCTSIZE2; k += 2)
						{
							*dst_ptr++ = *src_ptr++; /* copy even column */
							*dst_ptr++ = -*src_ptr++; /* copy odd column with sign change */
						}
					}
					else
					{
						/* Copy last partial block(s) verbatim */
						jcopy_block_row(src_row_ptr + dst_blk_x + x_crop_blocks,
						                dst_row_ptr + dst_blk_x,
						                1);
					}
				}
			}
		}
	}
}


LOCAL(void)
do_flip_v(j_decompress_ptr srcinfo, j_compress_ptr dstinfo,
          JDIMENSION x_crop_offset, JDIMENSION y_crop_offset,
          jvirt_barray_ptr* src_coef_arrays,
          jvirt_barray_ptr* dst_coef_arrays,
          IW::IStatus* pStatus)
/* Vertical flip */
{
	JDIMENSION MCU_rows, comp_height, dst_blk_x, dst_blk_y;
	JDIMENSION x_crop_blocks, y_crop_blocks;
	int ci, i, j, offset_y;
	JBLOCKARRAY src_buffer, dst_buffer;
	JBLOCKROW src_row_ptr, dst_row_ptr;
	JCOEFPTR src_ptr, dst_ptr;
	jpeg_component_info* compptr;
	int nStep = 0, nTotalSteps = CountSteps(dstinfo);

	/* We output into a separate array because we can't touch different
	* rows of the source virtual array simultaneously.  Otherwise, this
	* is a pretty straightforward analog of horizontal flip.
	* Within a DCT block, vertical mirroring is done by changing the signs
	* of odd-numbered rows.
	* Partial iMCUs at the bottom edge are copied verbatim.
	*/
	MCU_rows = srcinfo->image_height / (dstinfo->max_v_samp_factor * DCTSIZE);

	for (ci = 0; ci < dstinfo->num_components; ci++)
	{
		compptr = dstinfo->comp_info + ci;
		comp_height = MCU_rows * compptr->v_samp_factor;
		x_crop_blocks = x_crop_offset * compptr->h_samp_factor;
		y_crop_blocks = y_crop_offset * compptr->v_samp_factor;
		for (dst_blk_y = 0; dst_blk_y < compptr->height_in_blocks;
		     dst_blk_y += compptr->v_samp_factor)
		{
			pStatus->Progress(nStep++, nTotalSteps);
			if (pStatus->QueryCancel()) return;

			dst_buffer = (*srcinfo->mem->access_virt_barray)
			((j_common_ptr)srcinfo, dst_coef_arrays[ci], dst_blk_y,
			 static_cast<JDIMENSION>(compptr->v_samp_factor), TRUE);
			if (y_crop_blocks + dst_blk_y < comp_height)
			{
				/* Row is within the mirrorable area. */
				src_buffer = (*srcinfo->mem->access_virt_barray)
				((j_common_ptr)srcinfo, src_coef_arrays[ci],
				 comp_height - y_crop_blocks - dst_blk_y -
				 static_cast<JDIMENSION>(compptr->v_samp_factor),
				 static_cast<JDIMENSION>(compptr->v_samp_factor), FALSE);
			}
			else
			{
				/* Bottom-edge blocks will be copied verbatim. */
				src_buffer = (*srcinfo->mem->access_virt_barray)
				((j_common_ptr)srcinfo, src_coef_arrays[ci],
				 dst_blk_y + y_crop_blocks,
				 static_cast<JDIMENSION>(compptr->v_samp_factor), FALSE);
			}
			for (offset_y = 0; offset_y < compptr->v_samp_factor; offset_y++)
			{
				if (y_crop_blocks + dst_blk_y < comp_height)
				{
					/* Row is within the mirrorable area. */
					dst_row_ptr = dst_buffer[offset_y];
					src_row_ptr = src_buffer[compptr->v_samp_factor - offset_y - 1];
					src_row_ptr += x_crop_blocks;
					for (dst_blk_x = 0; dst_blk_x < compptr->width_in_blocks;
					     dst_blk_x++)
					{
						dst_ptr = dst_row_ptr[dst_blk_x];
						src_ptr = src_row_ptr[dst_blk_x];
						for (i = 0; i < DCTSIZE; i += 2)
						{
							/* copy even row */
							for (j = 0; j < DCTSIZE; j++)
								*dst_ptr++ = *src_ptr++;
							/* copy odd row with sign change */
							for (j = 0; j < DCTSIZE; j++)
								*dst_ptr++ = -*src_ptr++;
						}
					}
				}
				else
				{
					/* Just copy row verbatim. */
					jcopy_block_row(src_buffer[offset_y] + x_crop_blocks,
					                dst_buffer[offset_y],
					                compptr->width_in_blocks);
				}
			}
		}
	}
}


LOCAL(void)
do_transpose(j_decompress_ptr srcinfo, j_compress_ptr dstinfo,
             JDIMENSION x_crop_offset, JDIMENSION y_crop_offset,
             jvirt_barray_ptr* src_coef_arrays,
             jvirt_barray_ptr* dst_coef_arrays,
             IW::IStatus* pStatus)
/* Transpose source into destination */
{
	JDIMENSION dst_blk_x, dst_blk_y, x_crop_blocks, y_crop_blocks;
	int ci, i, j, offset_x, offset_y;
	JBLOCKARRAY src_buffer, dst_buffer;
	JCOEFPTR src_ptr, dst_ptr;
	jpeg_component_info* compptr;
	int nStep = 0, nTotalSteps = CountSteps(dstinfo);

	/* Transposing pixels within a block just requires transposing the
	* DCT coefficients.
	* Partial iMCUs at the edges require no special treatment; we simply
	* process all the available DCT blocks for every component.
	*/
	for (ci = 0; ci < dstinfo->num_components; ci++)
	{
		compptr = dstinfo->comp_info + ci;
		x_crop_blocks = x_crop_offset * compptr->h_samp_factor;
		y_crop_blocks = y_crop_offset * compptr->v_samp_factor;
		for (dst_blk_y = 0; dst_blk_y < compptr->height_in_blocks;
		     dst_blk_y += compptr->v_samp_factor)
		{
			pStatus->Progress(nStep++, nTotalSteps);
			if (pStatus->QueryCancel()) return;

			dst_buffer = (*srcinfo->mem->access_virt_barray)
			((j_common_ptr)srcinfo, dst_coef_arrays[ci], dst_blk_y,
			 static_cast<JDIMENSION>(compptr->v_samp_factor), TRUE);
			for (offset_y = 0; offset_y < compptr->v_samp_factor; offset_y++)
			{
				for (dst_blk_x = 0; dst_blk_x < compptr->width_in_blocks;
				     dst_blk_x += compptr->h_samp_factor)
				{
					src_buffer = (*srcinfo->mem->access_virt_barray)
					((j_common_ptr)srcinfo, src_coef_arrays[ci],
					 dst_blk_x + x_crop_blocks,
					 static_cast<JDIMENSION>(compptr->h_samp_factor), FALSE);
					for (offset_x = 0; offset_x < compptr->h_samp_factor; offset_x++)
					{
						dst_ptr = dst_buffer[offset_y][dst_blk_x + offset_x];
						src_ptr = src_buffer[offset_x][dst_blk_y + offset_y + y_crop_blocks];
						for (i = 0; i < DCTSIZE; i++)
							for (j = 0; j < DCTSIZE; j++)
								dst_ptr[j * DCTSIZE + i] = src_ptr[i * DCTSIZE + j];
					}
				}
			}
		}
	}
}


LOCAL(void)
do_rot_90(j_decompress_ptr srcinfo, j_compress_ptr dstinfo,
          JDIMENSION x_crop_offset, JDIMENSION y_crop_offset,
          jvirt_barray_ptr* src_coef_arrays,
          jvirt_barray_ptr* dst_coef_arrays,
          IW::IStatus* pStatus)
/* 90 degree rotation is equivalent to
*   1. Transposing the image;
*   2. Horizontal mirroring.
* These two steps are merged into a single processing routine.
*/
{
	JDIMENSION MCU_cols, comp_width, dst_blk_x, dst_blk_y;
	JDIMENSION x_crop_blocks, y_crop_blocks;
	int ci, i, j, offset_x, offset_y;
	JBLOCKARRAY src_buffer, dst_buffer;
	JCOEFPTR src_ptr, dst_ptr;
	jpeg_component_info* compptr;
	int nStep = 0, nTotalSteps = CountSteps(dstinfo);

	/* Because of the horizontal mirror step, we can't process partial iMCUs
	* at the (output) right edge properly.  They just get transposed and
	* not mirrored.
	*/
	MCU_cols = srcinfo->image_height / (dstinfo->max_h_samp_factor * DCTSIZE);

	for (ci = 0; ci < dstinfo->num_components; ci++)
	{
		compptr = dstinfo->comp_info + ci;
		comp_width = MCU_cols * compptr->h_samp_factor;
		x_crop_blocks = x_crop_offset * compptr->h_samp_factor;
		y_crop_blocks = y_crop_offset * compptr->v_samp_factor;
		for (dst_blk_y = 0; dst_blk_y < compptr->height_in_blocks;
		     dst_blk_y += compptr->v_samp_factor)
		{
			pStatus->Progress(nStep++, nTotalSteps);
			if (pStatus->QueryCancel()) return;

			dst_buffer = (*srcinfo->mem->access_virt_barray)
			((j_common_ptr)srcinfo, dst_coef_arrays[ci], dst_blk_y,
			 static_cast<JDIMENSION>(compptr->v_samp_factor), TRUE);
			for (offset_y = 0; offset_y < compptr->v_samp_factor; offset_y++)
			{
				for (dst_blk_x = 0; dst_blk_x < compptr->width_in_blocks;
				     dst_blk_x += compptr->h_samp_factor)
				{
					if (x_crop_blocks + dst_blk_x < comp_width)
					{
						/* Block is within the mirrorable area. */
						src_buffer = (*srcinfo->mem->access_virt_barray)
						((j_common_ptr)srcinfo, src_coef_arrays[ci],
						 comp_width - x_crop_blocks - dst_blk_x -
						 static_cast<JDIMENSION>(compptr->h_samp_factor),
						 static_cast<JDIMENSION>(compptr->h_samp_factor), FALSE);
					}
					else
					{
						/* Edge blocks are transposed but not mirrored. */
						src_buffer = (*srcinfo->mem->access_virt_barray)
						((j_common_ptr)srcinfo, src_coef_arrays[ci],
						 dst_blk_x + x_crop_blocks,
						 static_cast<JDIMENSION>(compptr->h_samp_factor), FALSE);
					}
					for (offset_x = 0; offset_x < compptr->h_samp_factor; offset_x++)
					{
						dst_ptr = dst_buffer[offset_y][dst_blk_x + offset_x];
						if (x_crop_blocks + dst_blk_x < comp_width)
						{
							/* Block is within the mirrorable area. */
							src_ptr = src_buffer[compptr->h_samp_factor - offset_x - 1]
								[dst_blk_y + offset_y + y_crop_blocks];
							for (i = 0; i < DCTSIZE; i++)
							{
								for (j = 0; j < DCTSIZE; j++)
									dst_ptr[j * DCTSIZE + i] = src_ptr[i * DCTSIZE + j];
								i++;
								for (j = 0; j < DCTSIZE; j++)
									dst_ptr[j * DCTSIZE + i] = -src_ptr[i * DCTSIZE + j];
							}
						}
						else
						{
							/* Edge blocks are transposed but not mirrored. */
							src_ptr = src_buffer[offset_x]
								[dst_blk_y + offset_y + y_crop_blocks];
							for (i = 0; i < DCTSIZE; i++)
								for (j = 0; j < DCTSIZE; j++)
									dst_ptr[j * DCTSIZE + i] = src_ptr[i * DCTSIZE + j];
						}
					}
				}
			}
		}
	}
}


LOCAL(void)
do_rot_270(j_decompress_ptr srcinfo, j_compress_ptr dstinfo,
           JDIMENSION x_crop_offset, JDIMENSION y_crop_offset,
           jvirt_barray_ptr* src_coef_arrays,
           jvirt_barray_ptr* dst_coef_arrays,
           IW::IStatus* pStatus)
/* 270 degree rotation is equivalent to
*   1. Horizontal mirroring;
*   2. Transposing the image.
* These two steps are merged into a single processing routine.
*/
{
	JDIMENSION MCU_rows, comp_height, dst_blk_x, dst_blk_y;
	JDIMENSION x_crop_blocks, y_crop_blocks;
	int ci, i, j, offset_x, offset_y;
	JBLOCKARRAY src_buffer, dst_buffer;
	JCOEFPTR src_ptr, dst_ptr;
	jpeg_component_info* compptr;
	int nStep = 0, nTotalSteps = CountSteps(dstinfo);

	/* Because of the horizontal mirror step, we can't process partial iMCUs
	* at the (output) bottom edge properly.  They just get transposed and
	* not mirrored.
	*/
	MCU_rows = srcinfo->image_width / (dstinfo->max_v_samp_factor * DCTSIZE);

	for (ci = 0; ci < dstinfo->num_components; ci++)
	{
		compptr = dstinfo->comp_info + ci;
		comp_height = MCU_rows * compptr->v_samp_factor;
		x_crop_blocks = x_crop_offset * compptr->h_samp_factor;
		y_crop_blocks = y_crop_offset * compptr->v_samp_factor;
		for (dst_blk_y = 0; dst_blk_y < compptr->height_in_blocks;
		     dst_blk_y += compptr->v_samp_factor)
		{
			pStatus->Progress(nStep++, nTotalSteps);
			if (pStatus->QueryCancel()) return;

			dst_buffer = (*srcinfo->mem->access_virt_barray)
			((j_common_ptr)srcinfo, dst_coef_arrays[ci], dst_blk_y,
			 static_cast<JDIMENSION>(compptr->v_samp_factor), TRUE);
			for (offset_y = 0; offset_y < compptr->v_samp_factor; offset_y++)
			{
				for (dst_blk_x = 0; dst_blk_x < compptr->width_in_blocks;
				     dst_blk_x += compptr->h_samp_factor)
				{
					src_buffer = (*srcinfo->mem->access_virt_barray)
					((j_common_ptr)srcinfo, src_coef_arrays[ci],
					 dst_blk_x + x_crop_blocks,
					 static_cast<JDIMENSION>(compptr->h_samp_factor), FALSE);
					for (offset_x = 0; offset_x < compptr->h_samp_factor; offset_x++)
					{
						dst_ptr = dst_buffer[offset_y][dst_blk_x + offset_x];
						if (y_crop_blocks + dst_blk_y < comp_height)
						{
							/* Block is within the mirrorable area. */
							src_ptr = src_buffer[offset_x]
								[comp_height - y_crop_blocks - dst_blk_y - offset_y - 1];
							for (i = 0; i < DCTSIZE; i++)
							{
								for (j = 0; j < DCTSIZE; j++)
								{
									dst_ptr[j * DCTSIZE + i] = src_ptr[i * DCTSIZE + j];
									j++;
									dst_ptr[j * DCTSIZE + i] = -src_ptr[i * DCTSIZE + j];
								}
							}
						}
						else
						{
							/* Edge blocks are transposed but not mirrored. */
							src_ptr = src_buffer[offset_x]
								[dst_blk_y + offset_y + y_crop_blocks];
							for (i = 0; i < DCTSIZE; i++)
								for (j = 0; j < DCTSIZE; j++)
									dst_ptr[j * DCTSIZE + i] = src_ptr[i * DCTSIZE + j];
						}
					}
				}
			}
		}
	}
}


LOCAL(void)
do_rot_180(j_decompress_ptr srcinfo, j_compress_ptr dstinfo,
           JDIMENSION x_crop_offset, JDIMENSION y_crop_offset,
           jvirt_barray_ptr* src_coef_arrays,
           jvirt_barray_ptr* dst_coef_arrays,
           IW::IStatus* pStatus)
/* 180 degree rotation is equivalent to
*   1. Vertical mirroring;
*   2. Horizontal mirroring.
* These two steps are merged into a single processing routine.
*/
{
	JDIMENSION MCU_cols, MCU_rows, comp_width, comp_height, dst_blk_x, dst_blk_y;
	JDIMENSION x_crop_blocks, y_crop_blocks;
	int ci, i, j, offset_y;
	JBLOCKARRAY src_buffer, dst_buffer;
	JBLOCKROW src_row_ptr, dst_row_ptr;
	JCOEFPTR src_ptr, dst_ptr;
	jpeg_component_info* compptr;
	int nStep = 0, nTotalSteps = CountSteps(dstinfo);

	MCU_cols = srcinfo->image_width / (dstinfo->max_h_samp_factor * DCTSIZE);
	MCU_rows = srcinfo->image_height / (dstinfo->max_v_samp_factor * DCTSIZE);

	for (ci = 0; ci < dstinfo->num_components; ci++)
	{
		compptr = dstinfo->comp_info + ci;
		comp_width = MCU_cols * compptr->h_samp_factor;
		comp_height = MCU_rows * compptr->v_samp_factor;
		x_crop_blocks = x_crop_offset * compptr->h_samp_factor;
		y_crop_blocks = y_crop_offset * compptr->v_samp_factor;
		for (dst_blk_y = 0; dst_blk_y < compptr->height_in_blocks;
		     dst_blk_y += compptr->v_samp_factor)
		{
			pStatus->Progress(nStep++, nTotalSteps);
			if (pStatus->QueryCancel()) return;

			dst_buffer = (*srcinfo->mem->access_virt_barray)
			((j_common_ptr)srcinfo, dst_coef_arrays[ci], dst_blk_y,
			 static_cast<JDIMENSION>(compptr->v_samp_factor), TRUE);
			if (y_crop_blocks + dst_blk_y < comp_height)
			{
				/* Row is within the vertically mirrorable area. */
				src_buffer = (*srcinfo->mem->access_virt_barray)
				((j_common_ptr)srcinfo, src_coef_arrays[ci],
				 comp_height - y_crop_blocks - dst_blk_y -
				 static_cast<JDIMENSION>(compptr->v_samp_factor),
				 static_cast<JDIMENSION>(compptr->v_samp_factor), FALSE);
			}
			else
			{
				/* Bottom-edge rows are only mirrored horizontally. */
				src_buffer = (*srcinfo->mem->access_virt_barray)
				((j_common_ptr)srcinfo, src_coef_arrays[ci],
				 dst_blk_y + y_crop_blocks,
				 static_cast<JDIMENSION>(compptr->v_samp_factor), FALSE);
			}
			for (offset_y = 0; offset_y < compptr->v_samp_factor; offset_y++)
			{
				dst_row_ptr = dst_buffer[offset_y];
				if (y_crop_blocks + dst_blk_y < comp_height)
				{
					/* Row is within the mirrorable area. */
					src_row_ptr = src_buffer[compptr->v_samp_factor - offset_y - 1];
					for (dst_blk_x = 0; dst_blk_x < compptr->width_in_blocks; dst_blk_x++)
					{
						dst_ptr = dst_row_ptr[dst_blk_x];
						if (x_crop_blocks + dst_blk_x < comp_width)
						{
							/* Process the blocks that can be mirrored both ways. */
							src_ptr = src_row_ptr[comp_width - x_crop_blocks - dst_blk_x - 1];
							for (i = 0; i < DCTSIZE; i += 2)
							{
								/* For even row, negate every odd column. */
								for (j = 0; j < DCTSIZE; j += 2)
								{
									*dst_ptr++ = *src_ptr++;
									*dst_ptr++ = -*src_ptr++;
								}
								/* For odd row, negate every even column. */
								for (j = 0; j < DCTSIZE; j += 2)
								{
									*dst_ptr++ = -*src_ptr++;
									*dst_ptr++ = *src_ptr++;
								}
							}
						}
						else
						{
							/* Any remaining right-edge blocks are only mirrored vertically. */
							src_ptr = src_row_ptr[x_crop_blocks + dst_blk_x];
							for (i = 0; i < DCTSIZE; i += 2)
							{
								for (j = 0; j < DCTSIZE; j++)
									*dst_ptr++ = *src_ptr++;
								for (j = 0; j < DCTSIZE; j++)
									*dst_ptr++ = -*src_ptr++;
							}
						}
					}
				}
				else
				{
					/* Remaining rows are just mirrored horizontally. */
					src_row_ptr = src_buffer[offset_y];
					for (dst_blk_x = 0; dst_blk_x < compptr->width_in_blocks; dst_blk_x++)
					{
						if (x_crop_blocks + dst_blk_x < comp_width)
						{
							/* Process the blocks that can be mirrored. */
							dst_ptr = dst_row_ptr[dst_blk_x];
							src_ptr = src_row_ptr[comp_width - x_crop_blocks - dst_blk_x - 1];
							for (i = 0; i < DCTSIZE2; i += 2)
							{
								*dst_ptr++ = *src_ptr++;
								*dst_ptr++ = -*src_ptr++;
							}
						}
						else
						{
							/* Any remaining right-edge blocks are only copied. */
							jcopy_block_row(src_row_ptr + dst_blk_x + x_crop_blocks,
							                dst_row_ptr + dst_blk_x,
							                1);
						}
					}
				}
			}
		}
	}
}


LOCAL(void)
do_transverse(j_decompress_ptr srcinfo, j_compress_ptr dstinfo,
              JDIMENSION x_crop_offset, JDIMENSION y_crop_offset,
              jvirt_barray_ptr* src_coef_arrays,
              jvirt_barray_ptr* dst_coef_arrays,
              IW::IStatus* pStatus)
/* Transverse transpose is equivalent to
*   1. 180 degree rotation;
*   2. Transposition;
* or
*   1. Horizontal mirroring;
*   2. Transposition;
*   3. Horizontal mirroring.
* These steps are merged into a single processing routine.
*/
{
	JDIMENSION MCU_cols, MCU_rows, comp_width, comp_height, dst_blk_x, dst_blk_y;
	JDIMENSION x_crop_blocks, y_crop_blocks;
	int ci, i, j, offset_x, offset_y;
	JBLOCKARRAY src_buffer, dst_buffer;
	JCOEFPTR src_ptr, dst_ptr;
	jpeg_component_info* compptr;
	int nStep = 0, nTotalSteps = CountSteps(dstinfo);

	MCU_cols = srcinfo->image_height / (dstinfo->max_h_samp_factor * DCTSIZE);
	MCU_rows = srcinfo->image_width / (dstinfo->max_v_samp_factor * DCTSIZE);

	for (ci = 0; ci < dstinfo->num_components; ci++)
	{
		compptr = dstinfo->comp_info + ci;
		comp_width = MCU_cols * compptr->h_samp_factor;
		comp_height = MCU_rows * compptr->v_samp_factor;
		x_crop_blocks = x_crop_offset * compptr->h_samp_factor;
		y_crop_blocks = y_crop_offset * compptr->v_samp_factor;
		for (dst_blk_y = 0; dst_blk_y < compptr->height_in_blocks;
		     dst_blk_y += compptr->v_samp_factor)
		{
			pStatus->Progress(nStep++, nTotalSteps);
			if (pStatus->QueryCancel()) return;

			dst_buffer = (*srcinfo->mem->access_virt_barray)
			((j_common_ptr)srcinfo, dst_coef_arrays[ci], dst_blk_y,
			 static_cast<JDIMENSION>(compptr->v_samp_factor), TRUE);
			for (offset_y = 0; offset_y < compptr->v_samp_factor; offset_y++)
			{
				for (dst_blk_x = 0; dst_blk_x < compptr->width_in_blocks;
				     dst_blk_x += compptr->h_samp_factor)
				{
					if (x_crop_blocks + dst_blk_x < comp_width)
					{
						/* Block is within the mirrorable area. */
						src_buffer = (*srcinfo->mem->access_virt_barray)
						((j_common_ptr)srcinfo, src_coef_arrays[ci],
						 comp_width - x_crop_blocks - dst_blk_x -
						 static_cast<JDIMENSION>(compptr->h_samp_factor),
						 static_cast<JDIMENSION>(compptr->h_samp_factor), FALSE);
					}
					else
					{
						src_buffer = (*srcinfo->mem->access_virt_barray)
						((j_common_ptr)srcinfo, src_coef_arrays[ci],
						 dst_blk_x + x_crop_blocks,
						 static_cast<JDIMENSION>(compptr->h_samp_factor), FALSE);
					}
					for (offset_x = 0; offset_x < compptr->h_samp_factor; offset_x++)
					{
						dst_ptr = dst_buffer[offset_y][dst_blk_x + offset_x];
						if (y_crop_blocks + dst_blk_y < comp_height)
						{
							if (x_crop_blocks + dst_blk_x < comp_width)
							{
								/* Block is within the mirrorable area. */
								src_ptr = src_buffer[compptr->h_samp_factor - offset_x - 1]
									[comp_height - y_crop_blocks - dst_blk_y - offset_y - 1];
								for (i = 0; i < DCTSIZE; i++)
								{
									for (j = 0; j < DCTSIZE; j++)
									{
										dst_ptr[j * DCTSIZE + i] = src_ptr[i * DCTSIZE + j];
										j++;
										dst_ptr[j * DCTSIZE + i] = -src_ptr[i * DCTSIZE + j];
									}
									i++;
									for (j = 0; j < DCTSIZE; j++)
									{
										dst_ptr[j * DCTSIZE + i] = -src_ptr[i * DCTSIZE + j];
										j++;
										dst_ptr[j * DCTSIZE + i] = src_ptr[i * DCTSIZE + j];
									}
								}
							}
							else
							{
								/* Right-edge blocks are mirrored in y only */
								src_ptr = src_buffer[offset_x]
									[comp_height - y_crop_blocks - dst_blk_y - offset_y - 1];
								for (i = 0; i < DCTSIZE; i++)
								{
									for (j = 0; j < DCTSIZE; j++)
									{
										dst_ptr[j * DCTSIZE + i] = src_ptr[i * DCTSIZE + j];
										j++;
										dst_ptr[j * DCTSIZE + i] = -src_ptr[i * DCTSIZE + j];
									}
								}
							}
						}
						else
						{
							if (x_crop_blocks + dst_blk_x < comp_width)
							{
								/* Bottom-edge blocks are mirrored in x only */
								src_ptr = src_buffer[compptr->h_samp_factor - offset_x - 1]
									[dst_blk_y + offset_y + y_crop_blocks];
								for (i = 0; i < DCTSIZE; i++)
								{
									for (j = 0; j < DCTSIZE; j++)
										dst_ptr[j * DCTSIZE + i] = src_ptr[i * DCTSIZE + j];
									i++;
									for (j = 0; j < DCTSIZE; j++)
										dst_ptr[j * DCTSIZE + i] = -src_ptr[i * DCTSIZE + j];
								}
							}
							else
							{
								/* At lower right corner, just transpose, no mirroring */
								src_ptr = src_buffer[offset_x]
									[dst_blk_y + offset_y + y_crop_blocks];
								for (i = 0; i < DCTSIZE; i++)
									for (j = 0; j < DCTSIZE; j++)
										dst_ptr[j * DCTSIZE + i] = src_ptr[i * DCTSIZE + j];
							}
						}
					}
				}
			}
		}
	}
}


/* Parse an unsigned integer: subroutine for jtransform_parse_crop_spec.
* Returns TRUE if valid integer found, FALSE if not.
* *strptr is advanced over the digit string, and *result is set to its value.
*/

LOCAL(boolean)
jt_read_integer(const char** strptr, JDIMENSION* result)
{
	const char* ptr = *strptr;
	JDIMENSION val = 0;

	for (; isdigit(*ptr); ptr++)
	{
		val = val * 10 + static_cast<JDIMENSION>(*ptr - '0');
	}
	*result = val;
	if (ptr == *strptr)
		return FALSE; /* oops, no digits */
	*strptr = ptr;
	return TRUE;
}


/* Parse a crop specification (written in X11 geometry style).
* The routine returns TRUE if the spec string is valid, FALSE if not.
*
* The crop spec string should have the format
*	<width>x<height>{+-}<xoffset>{+-}<yoffset>
* where width, height, xoffset, and yoffset are unsigned integers.
* Each of the elements can be omitted to indicate a default value.
* (A weakness of this style is that it is not possible to omit xoffset
* while specifying yoffset, since they look alike.)
*
* This code is loosely based on XParseGeometry from the X11 distribution.
*/

GLOBAL(boolean)
jtransform_parse_crop_spec(jpeg_transform_info* info, const char* spec)
{
	info->crop = FALSE;
	info->crop_width_set = JCROP_UNSET;
	info->crop_height_set = JCROP_UNSET;
	info->crop_xoffset_set = JCROP_UNSET;
	info->crop_yoffset_set = JCROP_UNSET;

	if (isdigit(*spec))
	{
		/* fetch width */
		if (!jt_read_integer(&spec, &info->crop_width))
			return FALSE;
		info->crop_width_set = JCROP_POS;
	}
	if (*spec == 'x' || *spec == 'X')
	{
		/* fetch height */
		spec++;
		if (!jt_read_integer(&spec, &info->crop_height))
			return FALSE;
		info->crop_height_set = JCROP_POS;
	}
	if (*spec == '+' || *spec == '-')
	{
		/* fetch xoffset */
		info->crop_xoffset_set = (*spec == '-') ? JCROP_NEG : JCROP_POS;
		spec++;
		if (!jt_read_integer(&spec, &info->crop_xoffset))
			return FALSE;
	}
	if (*spec == '+' || *spec == '-')
	{
		/* fetch yoffset */
		info->crop_yoffset_set = (*spec == '-') ? JCROP_NEG : JCROP_POS;
		spec++;
		if (!jt_read_integer(&spec, &info->crop_yoffset))
			return FALSE;
	}
	/* We had better have gotten to the end of the string. */
	if (*spec != '\0')
		return FALSE;
	info->crop = TRUE;
	return TRUE;
}


/* Trim off any partial iMCUs on the indicated destination edge */

LOCAL(void)
trim_right_edge(jpeg_transform_info* info, JDIMENSION full_width)
{
	JDIMENSION MCU_cols;

	MCU_cols = info->output_width / (info->max_h_samp_factor * DCTSIZE);
	if (MCU_cols > 0 && info->x_crop_offset + MCU_cols ==
		full_width / (info->max_h_samp_factor * DCTSIZE))
		info->output_width = MCU_cols * (info->max_h_samp_factor * DCTSIZE);
}

LOCAL(void)
trim_bottom_edge(jpeg_transform_info* info, JDIMENSION full_height)
{
	JDIMENSION MCU_rows;

	MCU_rows = info->output_height / (info->max_v_samp_factor * DCTSIZE);
	if (MCU_rows > 0 && info->y_crop_offset + MCU_rows ==
		full_height / (info->max_v_samp_factor * DCTSIZE))
		info->output_height = MCU_rows * (info->max_v_samp_factor * DCTSIZE);
}


/* Request any required workspace.
*
* This routine figures out the size that the output image will be
* (which implies that all the transform parameters must be set before
* it is called).
*
* We allocate the workspace virtual arrays from the source decompression
* object, so that all the arrays (both the original data and the workspace)
* will be taken into account while making memory management decisions.
* Hence, this routine must be called after jpeg_read_header (which reads
* the image dimensions) and before jpeg_read_coefficients (which realizes
* the source's virtual arrays).
*/

GLOBAL(void)
jtransform_request_workspace(j_decompress_ptr srcinfo,
                             jpeg_transform_info* info)
{
	jvirt_barray_ptr* coef_arrays = nullptr;
	boolean need_workspace, transpose_it;
	jpeg_component_info* compptr;
	JDIMENSION xoffset, yoffset, width_in_iMCUs, height_in_iMCUs;
	JDIMENSION width_in_blocks, height_in_blocks;
	int ci, h_samp_factor, v_samp_factor;

	/* Determine number of components in output image */
	if (info->force_grayscale &&
		srcinfo->jpeg_color_space == JCS_YCbCr &&
		srcinfo->num_components == 3)
	{
		/* We'll only process the first component */
		info->num_components = 1;
	}
	else
	{
		/* Process all the components */
		info->num_components = srcinfo->num_components;
	}
	/* If there is only one output component, force the iMCU size to be 1;
	* else use the source iMCU size.  (This allows us to do the right thing
	* when reducing color to grayscale, and also provides a handy way of
	* cleaning up "funny" grayscale images whose sampling factors are not 1x1.)
	*/

	switch (info->transform)
	{
	case JXFORM_TRANSPOSE:
	case JXFORM_TRANSVERSE:
	case JXFORM_ROT_90:
	case JXFORM_ROT_270:
		info->output_width = srcinfo->image_height;
		info->output_height = srcinfo->image_width;
		if (info->num_components == 1)
		{
			info->max_h_samp_factor = 1;
			info->max_v_samp_factor = 1;
		}
		else
		{
			info->max_h_samp_factor = srcinfo->max_v_samp_factor;
			info->max_v_samp_factor = srcinfo->max_h_samp_factor;
		}
		break;
	default:
		info->output_width = srcinfo->image_width;
		info->output_height = srcinfo->image_height;
		if (info->num_components == 1)
		{
			info->max_h_samp_factor = 1;
			info->max_v_samp_factor = 1;
		}
		else
		{
			info->max_h_samp_factor = srcinfo->max_h_samp_factor;
			info->max_v_samp_factor = srcinfo->max_v_samp_factor;
		}
		break;
	}

	/* If cropping has been requested, compute the crop area's position and
	* dimensions, ensuring that its upper left corner falls at an iMCU boundary.
	*/
	if (info->crop)
	{
		/* Insert default values for unset crop parameters */
		if (info->crop_xoffset_set == JCROP_UNSET)
			info->crop_xoffset = 0; /* default to +0 */
		if (info->crop_yoffset_set == JCROP_UNSET)
			info->crop_yoffset = 0; /* default to +0 */
		if (info->crop_xoffset >= info->output_width ||
			info->crop_yoffset >= info->output_height)
			ERREXIT(srcinfo, JERR_BAD_CROP_SPEC);
		if (info->crop_width_set == JCROP_UNSET)
			info->crop_width = info->output_width - info->crop_xoffset;
		if (info->crop_height_set == JCROP_UNSET)
			info->crop_height = info->output_height - info->crop_yoffset;
		/* Ensure parameters are valid */
		if (info->crop_width <= 0 || info->crop_width > info->output_width ||
			info->crop_height <= 0 || info->crop_height > info->output_height ||
			info->crop_xoffset > info->output_width - info->crop_width ||
			info->crop_yoffset > info->output_height - info->crop_height)
			ERREXIT(srcinfo, JERR_BAD_CROP_SPEC);
		/* Convert negative crop offsets into regular offsets */
		if (info->crop_xoffset_set == JCROP_NEG)
			xoffset = info->output_width - info->crop_width - info->crop_xoffset;
		else
			xoffset = info->crop_xoffset;
		if (info->crop_yoffset_set == JCROP_NEG)
			yoffset = info->output_height - info->crop_height - info->crop_yoffset;
		else
			yoffset = info->crop_yoffset;
		/* Now adjust so that upper left corner falls at an iMCU boundary */
		info->output_width =
			info->crop_width + (xoffset % (info->max_h_samp_factor * DCTSIZE));
		info->output_height =
			info->crop_height + (yoffset % (info->max_v_samp_factor * DCTSIZE));
		/* Save x/y offsets measured in iMCUs */
		info->x_crop_offset = xoffset / (info->max_h_samp_factor * DCTSIZE);
		info->y_crop_offset = yoffset / (info->max_v_samp_factor * DCTSIZE);
	}
	else
	{
		info->x_crop_offset = 0;
		info->y_crop_offset = 0;
	}

	/* Figure out whether we need workspace arrays,
	* and if so whether they are transposed relative to the source.
	*/
	need_workspace = FALSE;
	transpose_it = FALSE;
	switch (info->transform)
	{
	case JXFORM_NONE:
		if (info->x_crop_offset != 0 || info->y_crop_offset != 0)
			need_workspace = TRUE;
		/* No workspace needed if neither cropping nor transforming */
		break;
	case JXFORM_FLIP_H:
		if (info->trim)
			trim_right_edge(info, srcinfo->image_width);
		if (info->y_crop_offset != 0)
			need_workspace = TRUE;
		/* do_flip_h_no_crop doesn't need a workspace array */
		break;
	case JXFORM_FLIP_V:
		if (info->trim)
			trim_bottom_edge(info, srcinfo->image_height);
		/* Need workspace arrays having same dimensions as source image. */
		need_workspace = TRUE;
		break;
	case JXFORM_TRANSPOSE:
		/* transpose does NOT have to trim anything */
		/* Need workspace arrays having transposed dimensions. */
		need_workspace = TRUE;
		transpose_it = TRUE;
		break;
	case JXFORM_TRANSVERSE:
		if (info->trim)
		{
			trim_right_edge(info, srcinfo->image_height);
			trim_bottom_edge(info, srcinfo->image_width);
		}
		/* Need workspace arrays having transposed dimensions. */
		need_workspace = TRUE;
		transpose_it = TRUE;
		break;
	case JXFORM_ROT_90:
		if (info->trim)
			trim_right_edge(info, srcinfo->image_height);
		/* Need workspace arrays having transposed dimensions. */
		need_workspace = TRUE;
		transpose_it = TRUE;
		break;
	case JXFORM_ROT_180:
		if (info->trim)
		{
			trim_right_edge(info, srcinfo->image_width);
			trim_bottom_edge(info, srcinfo->image_height);
		}
		/* Need workspace arrays having same dimensions as source image. */
		need_workspace = TRUE;
		break;
	case JXFORM_ROT_270:
		if (info->trim)
			trim_bottom_edge(info, srcinfo->image_width);
		/* Need workspace arrays having transposed dimensions. */
		need_workspace = TRUE;
		transpose_it = TRUE;
		break;
	}

	/* Allocate workspace if needed.
	* Note that we allocate arrays padded out to the next iMCU boundary,
	* so that transform routines need not worry about missing edge blocks.
	*/
	if (need_workspace)
	{
		coef_arrays = static_cast<jvirt_barray_ptr*>((*srcinfo->mem->alloc_small)((j_common_ptr)srcinfo, JPOOL_IMAGE,
			SIZEOF(jvirt_barray_ptr) * info->num_components));
		width_in_iMCUs = static_cast<JDIMENSION>(jdiv_round_up((long)info->output_width,
		                                                       (long)(info->max_h_samp_factor * DCTSIZE)));
		height_in_iMCUs = static_cast<JDIMENSION>(jdiv_round_up((long)info->output_height,
		                                                        (long)(info->max_v_samp_factor * DCTSIZE)));
		for (ci = 0; ci < info->num_components; ci++)
		{
			compptr = srcinfo->comp_info + ci;
			if (info->num_components == 1)
			{
				/* we're going to force samp factors to 1x1 in this case */
				h_samp_factor = v_samp_factor = 1;
			}
			else if (transpose_it)
			{
				h_samp_factor = compptr->v_samp_factor;
				v_samp_factor = compptr->h_samp_factor;
			}
			else
			{
				h_samp_factor = compptr->h_samp_factor;
				v_samp_factor = compptr->v_samp_factor;
			}
			width_in_blocks = width_in_iMCUs * h_samp_factor;
			height_in_blocks = height_in_iMCUs * v_samp_factor;
			coef_arrays[ci] = (*srcinfo->mem->request_virt_barray)
			((j_common_ptr)srcinfo, JPOOL_IMAGE, FALSE,
			 width_in_blocks, height_in_blocks, static_cast<JDIMENSION>(v_samp_factor));
		}
	}

	info->workspace_coef_arrays = coef_arrays;
}


/* Transpose destination image parameters */

LOCAL(void)
transpose_critical_parameters(j_compress_ptr dstinfo)
{
	int tblno, i, j, ci, itemp;
	jpeg_component_info* compptr;
	JQUANT_TBL* qtblptr;
	UINT16 qtemp;

	/* Transpose sampling factors */
	for (ci = 0; ci < dstinfo->num_components; ci++)
	{
		compptr = dstinfo->comp_info + ci;
		itemp = compptr->h_samp_factor;
		compptr->h_samp_factor = compptr->v_samp_factor;
		compptr->v_samp_factor = itemp;
	}

	/* Transpose quantization tables */
	for (tblno = 0; tblno < NUM_QUANT_TBLS; tblno++)
	{
		qtblptr = dstinfo->quant_tbl_ptrs[tblno];
		if (qtblptr != nullptr)
		{
			for (i = 0; i < DCTSIZE; i++)
			{
				for (j = 0; j < i; j++)
				{
					qtemp = qtblptr->quantval[i * DCTSIZE + j];
					qtblptr->quantval[i * DCTSIZE + j] = qtblptr->quantval[j * DCTSIZE + i];
					qtblptr->quantval[j * DCTSIZE + i] = qtemp;
				}
			}
		}
	}
}


/* Adjust Exif image parameters.
*
* We try to adjust the Tags ExifImageWidth and ExifImageHeight if possible.
*/

GLOBAL(void)
adjust_exif_parameters(JOCTET FAR * data, unsigned int length,
                       JDIMENSION new_width, JDIMENSION new_height)
{
	boolean is_motorola; /* Flag for byte order */
	unsigned int number_of_tags, tagnum;
	unsigned int firstoffset, offset;
	JDIMENSION new_value;

	if (length < 12) return; /* Length of an IFD entry */

	/* Discover byte order */
	if (GETJOCTET(data[0]) == 0x49 && GETJOCTET(data[1]) == 0x49)
		is_motorola = FALSE;
	else if (GETJOCTET(data[0]) == 0x4D && GETJOCTET(data[1]) == 0x4D)
		is_motorola = TRUE;
	else
		return;

	/* Check Tag Mark */
	if (is_motorola)
	{
		if (GETJOCTET(data[2]) != 0) return;
		if (GETJOCTET(data[3]) != 0x2A) return;
	}
	else
	{
		if (GETJOCTET(data[3]) != 0) return;
		if (GETJOCTET(data[2]) != 0x2A) return;
	}

	/* Get first IFD offset (offset to IFD0) */
	if (is_motorola)
	{
		if (GETJOCTET(data[4]) != 0) return;
		if (GETJOCTET(data[5]) != 0) return;
		firstoffset = GETJOCTET(data[6]);
		firstoffset <<= 8;
		firstoffset += GETJOCTET(data[7]);
	}
	else
	{
		if (GETJOCTET(data[7]) != 0) return;
		if (GETJOCTET(data[6]) != 0) return;
		firstoffset = GETJOCTET(data[5]);
		firstoffset <<= 8;
		firstoffset += GETJOCTET(data[4]);
	}
	if (firstoffset > length - 2) return; /* check end of data segment */

	/* Get the number of directory entries contained in this IFD */
	if (is_motorola)
	{
		number_of_tags = GETJOCTET(data[firstoffset]);
		number_of_tags <<= 8;
		number_of_tags += GETJOCTET(data[firstoffset+1]);
	}
	else
	{
		number_of_tags = GETJOCTET(data[firstoffset+1]);
		number_of_tags <<= 8;
		number_of_tags += GETJOCTET(data[firstoffset]);
	}
	if (number_of_tags == 0) return;
	firstoffset += 2;

	/* Search for ExifSubIFD offset Tag in IFD0 */
	for (;;)
	{
		if (firstoffset > length - 12) return; /* check end of data segment */
		/* Get Tag number */
		if (is_motorola)
		{
			tagnum = GETJOCTET(data[firstoffset]);
			tagnum <<= 8;
			tagnum += GETJOCTET(data[firstoffset+1]);
		}
		else
		{
			tagnum = GETJOCTET(data[firstoffset+1]);
			tagnum <<= 8;
			tagnum += GETJOCTET(data[firstoffset]);
		}
		if (tagnum == 0x8769) break; /* found ExifSubIFD offset Tag */
		if (--number_of_tags == 0) return;
		firstoffset += 12;
	}

	/* Get the ExifSubIFD offset */
	if (is_motorola)
	{
		if (GETJOCTET(data[firstoffset+8]) != 0) return;
		if (GETJOCTET(data[firstoffset+9]) != 0) return;
		offset = GETJOCTET(data[firstoffset+10]);
		offset <<= 8;
		offset += GETJOCTET(data[firstoffset+11]);
	}
	else
	{
		if (GETJOCTET(data[firstoffset+11]) != 0) return;
		if (GETJOCTET(data[firstoffset+10]) != 0) return;
		offset = GETJOCTET(data[firstoffset+9]);
		offset <<= 8;
		offset += GETJOCTET(data[firstoffset+8]);
	}
	if (offset > length - 2) return; /* check end of data segment */

	/* Get the number of directory entries contained in this SubIFD */
	if (is_motorola)
	{
		number_of_tags = GETJOCTET(data[offset]);
		number_of_tags <<= 8;
		number_of_tags += GETJOCTET(data[offset+1]);
	}
	else
	{
		number_of_tags = GETJOCTET(data[offset+1]);
		number_of_tags <<= 8;
		number_of_tags += GETJOCTET(data[offset]);
	}
	if (number_of_tags < 2) return;
	offset += 2;

	/* Search for ExifImageWidth and ExifImageHeight Tags in this SubIFD */
	do
	{
		if (offset > length - 12) return; /* check end of data segment */
		/* Get Tag number */
		if (is_motorola)
		{
			tagnum = GETJOCTET(data[offset]);
			tagnum <<= 8;
			tagnum += GETJOCTET(data[offset+1]);
		}
		else
		{
			tagnum = GETJOCTET(data[offset+1]);
			tagnum <<= 8;
			tagnum += GETJOCTET(data[offset]);
		}
		if (tagnum == 0xA002 || tagnum == 0xA003 || tagnum == 0x0112)
		{
			if (tagnum == 0xA002)
				new_value = new_width; /* ExifImageWidth Tag */
			else if (tagnum == 0xA003)
				new_value = new_height; /* ExifImageHeight Tag */
			else
				new_value = 1; /* ExifImageHeight Tag */

			if (is_motorola)
			{
				data[offset + 2] = 0; /* Format = unsigned long (4 octets) */
				data[offset + 3] = 4;
				data[offset + 4] = 0; /* Number Of Components = 1 */
				data[offset + 5] = 0;
				data[offset + 6] = 0;
				data[offset + 7] = 1;
				data[offset + 8] = 0;
				data[offset + 9] = 0;
				data[offset + 10] = static_cast<JOCTET>((new_value >> 8) & 0xFF);
				data[offset + 11] = static_cast<JOCTET>(new_value & 0xFF);
			}
			else
			{
				data[offset + 2] = 4; /* Format = unsigned long (4 octets) */
				data[offset + 3] = 0;
				data[offset + 4] = 1; /* Number Of Components = 1 */
				data[offset + 5] = 0;
				data[offset + 6] = 0;
				data[offset + 7] = 0;
				data[offset + 8] = static_cast<JOCTET>(new_value & 0xFF);
				data[offset + 9] = static_cast<JOCTET>((new_value >> 8) & 0xFF);
				data[offset + 10] = 0;
				data[offset + 11] = 0;
			}
		}
		offset += 12;
	}
	while (--number_of_tags);
}


/* Adjust output image parameters as needed.
*
* This must be called after jpeg_copy_critical_parameters()
* and before jpeg_write_coefficients().
*
* The return value is the set of virtual coefficient arrays to be written
* (either the ones allocated by jtransform_request_workspace, or the
* original source data arrays).  The caller will need to pass this value
* to jpeg_write_coefficients().
*/

GLOBAL(jvirt_barray_ptr *)
jtransform_adjust_parameters(j_decompress_ptr srcinfo,
                             j_compress_ptr dstinfo,
                             jvirt_barray_ptr* src_coef_arrays,
                             jpeg_transform_info* info)
{
	/* If force-to-grayscale is requested, adjust destination parameters */
	if (info->force_grayscale)
	{
		/* First, ensure we have YCbCr or grayscale data, and that the source's
		* Y channel is full resolution.  (No reasonable person would make Y
		* be less than full resolution, so actually coping with that case
		* isn't worth extra code space.  But we check it to avoid crashing.)
		*/
		if (((dstinfo->jpeg_color_space == JCS_YCbCr &&
					dstinfo->num_components == 3) ||
				(dstinfo->jpeg_color_space == JCS_GRAYSCALE &&
					dstinfo->num_components == 1)) &&
			srcinfo->comp_info[0].h_samp_factor == srcinfo->max_h_samp_factor &&
			srcinfo->comp_info[0].v_samp_factor == srcinfo->max_v_samp_factor)
		{
			/* We use jpeg_set_colorspace to make sure subsidiary settings get fixed
			* properly.  Among other things, it sets the target h_samp_factor &
			* v_samp_factor to 1, which typically won't match the source.
			* We have to preserve the source's quantization table number, however.
			*/
			int sv_quant_tbl_no = dstinfo->comp_info[0].quant_tbl_no;
			jpeg_set_colorspace(dstinfo, JCS_GRAYSCALE);
			dstinfo->comp_info[0].quant_tbl_no = sv_quant_tbl_no;
		}
		else
		{
			/* Sorry, can't do it */
			ERREXIT(dstinfo, JERR_CONVERSION_NOTIMPL);
		}
	}
	else if (info->num_components == 1)
	{
		/* For a single-component source, we force the destination sampling factors
		* to 1x1, with or without force_grayscale.  This is useful because some
		* decoders choke on grayscale images with other sampling factors.
		*/
		dstinfo->comp_info[0].h_samp_factor = 1;
		dstinfo->comp_info[0].v_samp_factor = 1;
	}

	/* Correct the destination's image dimensions as necessary
	* for crop and rotate/flip operations.
	*/
	dstinfo->image_width = info->output_width;
	dstinfo->image_height = info->output_height;

	/* Transpose destination image parameters */
	switch (info->transform)
	{
	case JXFORM_TRANSPOSE:
	case JXFORM_TRANSVERSE:
	case JXFORM_ROT_90:
	case JXFORM_ROT_270:
		transpose_critical_parameters(dstinfo);
		break;
	}

	/* Adjust Exif properties */
	if (srcinfo->marker_list != nullptr &&
		srcinfo->marker_list->marker == JPEG_APP0 + 1 &&
		srcinfo->marker_list->data_length >= 6 &&
		GETJOCTET(srcinfo->marker_list->data[0]) == 0x45 &&
		GETJOCTET(srcinfo->marker_list->data[1]) == 0x78 &&
		GETJOCTET(srcinfo->marker_list->data[2]) == 0x69 &&
		GETJOCTET(srcinfo->marker_list->data[3]) == 0x66 &&
		GETJOCTET(srcinfo->marker_list->data[4]) == 0 &&
		GETJOCTET(srcinfo->marker_list->data[5]) == 0)
	{
		/* Suppress output of JFIF marker */
		dstinfo->write_JFIF_header = FALSE;
		/* Adjust the recorded pixel dimensions. The orientation tag is not touched
		 * here: callers that mean to reorient the pixels say so themselves through
		 * jtransform_normalise_exif_orientation.
		 */
		if (dstinfo->image_width != srcinfo->image_width ||
			dstinfo->image_height != srcinfo->image_height)
			// Exif::FixExif tests for the Exif\0\0 header itself, so pass the whole marker.
			Exif::FixExif(srcinfo->marker_list->data,
			              srcinfo->marker_list->data_length,
			              dstinfo->image_width, dstinfo->image_height);
	}

	/* Return the appropriate output data set */
	if (info->workspace_coef_arrays != nullptr)
		return info->workspace_coef_arrays;

	return src_coef_arrays;
}


/* Execute the actual transformation, if any.
*
* This must be called *after* jpeg_write_coefficients, because it depends
* on jpeg_write_coefficients to have computed subsidiary values such as
* the per-component width and height fields in the destination object.
*
* Note that some transformations will modify the source data arrays!
*/

GLOBAL(void)
jtransform_execute_transform(j_decompress_ptr srcinfo,
                             j_compress_ptr dstinfo,
                             jvirt_barray_ptr* src_coef_arrays,
                             jpeg_transform_info* info,
                             IW::IStatus* pStatus)
{
	jvirt_barray_ptr* dst_coef_arrays = info->workspace_coef_arrays;

	/* Note: conditions tested here should match those in switch statement
	* in jtransform_request_workspace()
	*/
	switch (info->transform)
	{
	case JXFORM_NONE:
		if (info->x_crop_offset != 0 || info->y_crop_offset != 0)
			do_crop(srcinfo, dstinfo, info->x_crop_offset, info->y_crop_offset,
			        src_coef_arrays, dst_coef_arrays, pStatus);
		break;
	case JXFORM_FLIP_H:
		if (info->y_crop_offset != 0)
			do_flip_h(srcinfo, dstinfo, info->x_crop_offset, info->y_crop_offset,
			          src_coef_arrays, dst_coef_arrays, pStatus);
		else
			do_flip_h_no_crop(srcinfo, dstinfo, info->x_crop_offset,
			                  src_coef_arrays, pStatus);
		break;
	case JXFORM_FLIP_V:
		do_flip_v(srcinfo, dstinfo, info->x_crop_offset, info->y_crop_offset,
		          src_coef_arrays, dst_coef_arrays, pStatus);
		break;
	case JXFORM_TRANSPOSE:
		do_transpose(srcinfo, dstinfo, info->x_crop_offset, info->y_crop_offset,
		             src_coef_arrays, dst_coef_arrays, pStatus);
		break;
	case JXFORM_TRANSVERSE:
		do_transverse(srcinfo, dstinfo, info->x_crop_offset, info->y_crop_offset,
		              src_coef_arrays, dst_coef_arrays, pStatus);
		break;
	case JXFORM_ROT_90:
		do_rot_90(srcinfo, dstinfo, info->x_crop_offset, info->y_crop_offset,
		          src_coef_arrays, dst_coef_arrays, pStatus);
		break;
	case JXFORM_ROT_180:
		do_rot_180(srcinfo, dstinfo, info->x_crop_offset, info->y_crop_offset,
		           src_coef_arrays, dst_coef_arrays, pStatus);
		break;
	case JXFORM_ROT_270:
		do_rot_270(srcinfo, dstinfo, info->x_crop_offset, info->y_crop_offset,
		           src_coef_arrays, dst_coef_arrays, pStatus);
		break;
	}
}


#endif /* TRANSFORMS_SUPPORTED */


/* Setup decompression object to save desired markers in memory.
* This must be called before jpeg_read_header() to have the desired effect.
*/

GLOBAL(void)
jcopy_markers_setup(j_decompress_ptr srcinfo, JCOPY_OPTION option)
{
	int m;

	/* Save comments except under NONE option */
	if (option != JCOPYOPT_NONE)
	{
		jpeg_save_markers(srcinfo, JPEG_COM, 0xFFFF);
	}
	/* Save all types of APPn markers iff ALL option */
	if (option == JCOPYOPT_ALL)
	{
		for (m = 0; m < 16; m++)
			jpeg_save_markers(srcinfo, JPEG_APP0 + m, 0xFFFF);
	}
}

/* Copy markers saved in the given source object to the destination object.
* This should be called just after jpeg_start_compress() or
* jpeg_write_coefficients().
* Note that those routines will have written the SOI, and also the
* JFIF APP0 or Adobe APP14 markers if selected.
*/

GLOBAL(void)
jcopy_markers_execute(j_decompress_ptr srcinfo, j_compress_ptr dstinfo, JCOPY_OPTION option)
{
	jpeg_saved_marker_ptr marker;

	// In the current implementation, we don't actually need to examine the
	// option flag here; we just copy everything that got saved.
	// But to avoid confusion, we do not output JFIF and Adobe APP14 markers
	// if the encoder library already wrote one.
	for (marker = srcinfo->marker_list; marker != nullptr; marker = marker->next)
	{
		if (option == JCOPYOPT_ALL_EXCEPT_PROFILES &&
			(IsExifBlob(marker) || IsXmpBlob(marker) || IsIccBlob(marker) || IsIptcBlob(marker)))
			continue; // the caller rewrites these from the image itself

		if (dstinfo->write_JFIF_header &&
			marker->marker == JPEG_APP0 &&
			marker->data_length >= 5 &&
			GETJOCTET(marker->data[0]) == 0x4A &&
			GETJOCTET(marker->data[1]) == 0x46 &&
			GETJOCTET(marker->data[2]) == 0x49 &&
			GETJOCTET(marker->data[3]) == 0x46 &&
			GETJOCTET(marker->data[4]) == 0)
			continue; // reject duplicate JFIF 

		if (dstinfo->write_Adobe_marker &&
			marker->marker == JPEG_APP0 + 14 &&
			marker->data_length >= 5 &&
			GETJOCTET(marker->data[0]) == 0x41 &&
			GETJOCTET(marker->data[1]) == 0x64 &&
			GETJOCTET(marker->data[2]) == 0x6F &&
			GETJOCTET(marker->data[3]) == 0x62 &&
			GETJOCTET(marker->data[4]) == 0x65)
			continue; // reject duplicate Adobe

		jpeg_write_marker(dstinfo, marker->marker,
		                  marker->data, marker->data_length);
	}
}

static char* xmp_signature = "http://ns.adobe.com/xap/1.0/";
static char* iptc_signature = "Photoshop 3.0 8BIM\04\04\0\0\0\0";
static const char photoshop_id[] = "Photoshop 3.0";
static char* exif_signature = "Exif\0\0";
static const char icc_signature[] = "ICC_PROFILE";

static const unsigned xmp_signature_length = static_cast<unsigned int>(strlen(xmp_signature) + 1);
static constexpr unsigned iptc_signature_length = 24;
// "Photoshop 3.0" and its terminator. Every APP13 that belongs to Photoshop
// starts with it, and a reader concatenates what follows it across markers.
static constexpr unsigned photoshop_id_length = sizeof(photoshop_id);
static constexpr unsigned exif_signature_length = 6;
// Signature, then a one-based chunk number and the chunk count.
static constexpr unsigned ICC_PREAMBLE_LENGTH = sizeof(icc_signature) + 2;


bool IsIptcBlob(jpeg_saved_marker_ptr marker)
{
	if (marker->marker != IPTC_MARKER || marker->data_length <= iptc_signature_length)
		return false;

	// The APP13 identifier is "Photoshop 3.0\0"; iptc_signature holds a space at
	// index 13 that WriteIptcBlob patches to NUL, so compare the 13 chars and the
	// terminator separately.
	return memcmp(iptc_signature, marker->data, 13) == 0 && marker->data[13] == 0;
}

bool IsXmpBlob(jpeg_saved_marker_ptr marker)
{
	if (marker->marker != XMP_EXIF_MARKER || marker->data_length <= xmp_signature_length)
		return false;

	return memcmp(xmp_signature, marker->data, xmp_signature_length) == 0;
}

bool IsIccBlob(jpeg_saved_marker_ptr marker)
{
	// APP2 is shared with FPXR, MPF and others; only ICC_PROFILE\0 is an ICC profile.
	return marker->marker == ICC_MARKER &&
		marker->data_length > ICC_PREAMBLE_LENGTH &&
		memcmp(icc_signature, marker->data, sizeof(icc_signature)) == 0;
}

bool IsExifBlob(jpeg_saved_marker_ptr marker)
{
	if (marker->marker != XMP_EXIF_MARKER || marker->data_length <= exif_signature_length)
		return false;

	return memcmp(exif_signature, marker->data, exif_signature_length) == 0;
}


IW::MetaData LoadIptcProfile(jpeg_saved_marker_ptr markers)
{
	// Photoshop splits a large APP13 across markers, each repeating the
	// identifier, and a reader is expected to join what follows it. Taking one
	// marker leaves an 8BIM chain cut off part way through a resource.
	std::vector<BYTE> segment;

	for (jpeg_saved_marker_ptr marker = markers; marker != nullptr; marker = marker->next)
	{
		if (!IsIptcBlob(marker))
			continue;

		if (segment.empty())
			segment.insert(segment.end(), marker->data, marker->data + photoshop_id_length);

		segment.insert(segment.end(),
		               marker->data + photoshop_id_length,
		               marker->data + marker->data_length);
	}

	if (segment.empty())
		return IW::MetaData(IW::MetaDataTypes::PROFILE_IPTC);

	// The whole segment, not the IIM stream inside it. Slicing at the first 0x1C
	// threw away every other Photoshop resource -- clipping paths, the Photoshop
	// thumbnail, the caption digest, the embedded XMP -- and IIM record 1, where
	// the coded character set that makes a UTF-8 caption readable lives.
	// MetadataIPTC finds the IIM stream inside this; nothing else has to.
	return IW::MetaData(IW::MetaDataTypes::PROFILE_IPTC, &segment[0],
	                    static_cast<unsigned>(segment.size()));
}

IW::MetaData LoadXmpBlob(jpeg_saved_marker_ptr marker)
{
	int nLength = marker->data_length;
	LPBYTE data = marker->data;
	return IW::MetaData(IW::MetaDataTypes::PROFILE_XMP, data + xmp_signature_length, nLength - xmp_signature_length);
}

IW::MetaData LoadIccProfile(jpeg_saved_marker_ptr markers)
{
	// Chunks may arrive in any order and each repeats the preamble, so they are
	// gathered by sequence number and concatenated once they are all in.
	std::map<int, std::pair<LPCBYTE, unsigned>> chunks;

	for (jpeg_saved_marker_ptr marker = markers; marker != nullptr; marker = marker->next)
	{
		if (!IsIccBlob(marker))
			continue;

		const int nSequence = marker->data[sizeof(icc_signature)];
		chunks[nSequence] = std::make_pair(marker->data + ICC_PREAMBLE_LENGTH,
		                                   marker->data_length - ICC_PREAMBLE_LENGTH);
	}

	unsigned nTotal = 0;

	for (auto i = chunks.begin(); i != chunks.end(); ++i)
		nTotal += i->second.second;

	if (nTotal == 0)
		return IW::MetaData(IW::MetaDataTypes::PROFILE_ICC);

	std::vector<BYTE> profile(nTotal);
	unsigned nOffset = 0;

	for (auto i = chunks.begin(); i != chunks.end(); ++i)
	{
		IW::MemCopy(&profile[nOffset], i->second.first, i->second.second);
		nOffset += i->second.second;
	}

	return IW::MetaData(IW::MetaDataTypes::PROFILE_ICC, &profile[0], nTotal);
}

IW::MetaData LoadExifBlob(jpeg_saved_marker_ptr marker)
{
	return IW::MetaData(IW::MetaDataTypes::PROFILE_EXIF, marker->data, marker->data_length);
}

void WriteIptcBlob(j_compress_ptr dstinfo, const IW::MetaData& blob)
{
	const unsigned size = blob.GetDataSize();

	if (size == 0)
		return;

	LPCBYTE p = blob.GetData();

	// A blob that came from a JPEG is the APP13 segment as it was read and goes
	// back byte for byte. Rebuilding it from the one resource this app understands
	// is what used to destroy everything else Photoshop had put in there.
	if (size >= photoshop_id_length &&
		memcmp(photoshop_id, p, photoshop_id_length) == 0)
	{
		const unsigned nMaxPayload = 65500u - photoshop_id_length;
		const unsigned nPayload = size - photoshop_id_length;
		unsigned i = 0;

		do
		{
			const unsigned nLength = IW::Min(nPayload - i, nMaxPayload);
			IW::CBuffer<BYTE> pProfile(nLength + photoshop_id_length);

			IW::MemCopy(pProfile, photoshop_id, photoshop_id_length);
			IW::MemCopy(pProfile + photoshop_id_length, p + photoshop_id_length + i, nLength);

			jpeg_write_marker(dstinfo, IPTC_MARKER, pProfile, nLength + photoshop_id_length);
			i += nMaxPayload;
		}
		while (i < nPayload);

		return;
	}

	// A bare IIM stream, from a format that stores one (TIFF, PSD). It never had a
	// container, so the smallest one that can carry it loses nothing.
	constexpr int nTagLength = 26;

	for (unsigned i = 0; i < size; i += 65500u)
	{
		unsigned nLength = IW::Min(size - i, 65500u);
		unsigned nRoundup = (nLength & 0x01); // round up for Photoshop
		unsigned nWriteSize = nLength + nRoundup + nTagLength;

		IW::CBuffer<BYTE> pProfile(nWriteSize);

		IW::MemCopy(pProfile, iptc_signature, iptc_signature_length);
		pProfile[13] = 0x00;
		pProfile[24] = static_cast<BYTE>(nLength >> 8);
		pProfile[25] = nLength & 0xff;

		IW::MemCopy(pProfile + nTagLength, p + i, nLength);

		if (nRoundup)
			pProfile[nLength + nTagLength] = 0;

		jpeg_write_marker(dstinfo, IPTC_MARKER, pProfile, nWriteSize);
	}
}

void WriteXmpBlob(j_compress_ptr dstinfo, const IW::MetaData& blob)
{
	unsigned size = blob.GetDataSize();

	if (size > 0)
	{
		LPCBYTE p = blob.GetData();
		unsigned nWriteSize = size + xmp_signature_length;

		IW::CBuffer<BYTE> pProfile(nWriteSize);
		IW::MemCopy(pProfile, xmp_signature, xmp_signature_length);
		IW::MemCopy(pProfile + xmp_signature_length, p, size);

		jpeg_write_marker(dstinfo, XMP_EXIF_MARKER, pProfile, nWriteSize);
	}
}

void WriteIccBlob(j_compress_ptr dstinfo, const IW::MetaData& blob)
{
	const unsigned size = blob.GetDataSize();

	if (size == 0)
		return;

	// jpeg_write_marker rejects anything over 65533 bytes, so a profile larger
	// than one marker is split and numbered.
	constexpr unsigned nMaxChunk = 65533u - ICC_PREAMBLE_LENGTH;

	LPCBYTE p = blob.GetData();
	const unsigned nChunks = (size + nMaxChunk - 1) / nMaxChunk;

	if (nChunks > 255) // the sequence number is a single byte
		return;

	IW::CBuffer<BYTE> pProfile(nMaxChunk + ICC_PREAMBLE_LENGTH);

	for (unsigned i = 0; i < nChunks; i++)
	{
		const unsigned nOffset = i * nMaxChunk;
		const unsigned nLength = ((size - nOffset) < nMaxChunk) ? (size - nOffset) : nMaxChunk;

		IW::MemCopy(pProfile, icc_signature, sizeof(icc_signature));
		pProfile[sizeof(icc_signature)] = static_cast<BYTE>(i + 1);
		pProfile[sizeof(icc_signature) + 1] = static_cast<BYTE>(nChunks);
		IW::MemCopy(pProfile + ICC_PREAMBLE_LENGTH, p + nOffset, nLength);

		jpeg_write_marker(dstinfo, ICC_MARKER, pProfile, nLength + ICC_PREAMBLE_LENGTH);
	}
}

void WriteExifBlob(j_compress_ptr dstinfo, const IW::MetaData& blob, int nOrientation)
{
	unsigned size = blob.GetDataSize();

	if (size > 0)
	{
		LPCBYTE p = blob.GetData();
		IW::CBuffer<BYTE> pProfile(size);
		IW::MemCopy(pProfile, p, size);
		Exif::FixExif(pProfile, size, dstinfo->image_width, dstinfo->image_height);

		// The tag says how the pixels are stored, so it has to be whatever the image
		// being written actually is. Writing TopLeft unconditionally -- which is what
		// this used to do -- left every save made with EXIF auto rotate switched off
		// permanently on its side.
		Exif::SetOrientation(pProfile, size, nOrientation);

		jpeg_write_marker(dstinfo, XMP_EXIF_MARKER, pProfile, size);
	}
}

// The eight ways a picture can sit in a file are the symmetries of the square.
// An element is written R^rot . F^flip -- the mirror first -- with R a quarter
// turn clockwise and F a mirror about the vertical axis.
namespace
{
	struct Dihedral
	{
		int rot;
		int flip;
	};

	// EXIF orientation 1..8: how the stored pixels have to be turned to be seen.
	const Dihedral s_orientationToDisplay[] =
	{
		{0, 0}, // unused, the tag is one based
		{0, 0}, // 1 TopLeft
		{0, 1}, // 2 TopRight     mirrored
		{2, 0}, // 3 BottomRight  half turn
		{2, 1}, // 4 BottomLeft   mirrored, half turn
		{3, 1}, // 5 LeftTop      transposed
		{1, 0}, // 6 RightTop     quarter turn clockwise
		{1, 1}, // 7 RightBottom  transverse
		{3, 0}, // 8 LeftBottom   quarter turn anticlockwise
	};

	Dihedral FromJxform(JXFORM_CODE code)
	{
		switch (code)
		{
		case JXFORM_FLIP_H: return {0, 1};
		case JXFORM_FLIP_V: return {2, 1};
		case JXFORM_TRANSPOSE: return {3, 1};
		case JXFORM_TRANSVERSE: return {1, 1};
		case JXFORM_ROT_90: return {1, 0};
		case JXFORM_ROT_180: return {2, 0};
		case JXFORM_ROT_270: return {3, 0};
		default: return {0, 0};
		}
	}

	JXFORM_CODE ToJxform(Dihedral d)
	{
		if (d.flip == 0)
		{
			switch (d.rot)
			{
			case 1: return JXFORM_ROT_90;
			case 2: return JXFORM_ROT_180;
			case 3: return JXFORM_ROT_270;
			default: return JXFORM_NONE;
			}
		}

		switch (d.rot)
		{
		case 1: return JXFORM_TRANSVERSE;
		case 2: return JXFORM_FLIP_V;
		case 3: return JXFORM_TRANSPOSE;
		default: return JXFORM_FLIP_H;
		}
	}

	// b first, then a. F.R = R'.F is what puts the inverse rotation in.
	Dihedral Compose(Dihedral a, Dihedral b)
	{
		Dihedral out;
		out.rot = (a.rot + (a.flip ? (4 - b.rot) % 4 : b.rot)) % 4;
		out.flip = (a.flip + b.flip) & 1;
		return out;
	}
}

JXFORM_CODE jtransform_compose_exif_orientation(JXFORM_CODE code, int nOrientation)
{
	if (nOrientation < IW::Orientation::TopLeft || nOrientation > IW::Orientation::LeftBottom)
		return code;

	return ToJxform(Compose(FromJxform(code), s_orientationToDisplay[nOrientation]));
}

void jtransform_normalise_exif_orientation(j_decompress_ptr srcinfo, jpeg_transform_info* info)
{
	for (jpeg_saved_marker_ptr marker = srcinfo->marker_list; marker != nullptr; marker = marker->next)
	{
		if (!IsExifBlob(marker))
			continue;

		const int nOrientation = Exif::ReadOrientation(marker->data, marker->data_length);

		if (nOrientation == IW::Orientation::TopLeft)
			return;

		// Turning what the user can see means undoing the camera's orientation on the
		// way. Any other way leaves the tag and the pixels each describing part of
		// the turn, and a viewer that honours the tag applies it twice.
		info->transform = jtransform_compose_exif_orientation(info->transform, nOrientation);
		Exif::SetOrientation(marker->data, marker->data_length, IW::Orientation::TopLeft);
		return;
	}
}
