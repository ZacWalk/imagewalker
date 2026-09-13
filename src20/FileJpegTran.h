// ImageWalker by Zac Walker
//
// Purpose: Lossless JPEG transforms - rotate, flip and crop on the DCT
//          coefficients.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

/*
 * transupp.h
 *
 * Copyright (C) 1997, Thomas G. Lane.
 * This file is part of the Independent JPEG Group's software.
 * For conditions of distribution and use, see the accompanying README file.
 *
 * This file contains declarations for image transformation routines and
 * other utility code used by the jpegtran sample application.  These are
 * NOT part of the core JPEG library.  But we keep these routines separate
 * from jpegtran.c to ease the task of maintaining jpegtran-like programs
 * that have other user interfaces.
 *
 * NOTE: all the routines declared here have very specific requirements
 * about when they are to be executed during the reading and writing of the
 * source and destination files.  See the comments in transupp.c, or see
 * jpegtran.c for an example of correct usage.
 */

#pragma once

#include "FileJpegLib.h"

/* If you happen not to want the image transform support, disable it here */
#ifndef TRANSFORMS_SUPPORTED
#define TRANSFORMS_SUPPORTED 1		/* 0 disables transform code */
#endif 

/* Short forms of external names for systems with brain-damaged linkers. */

#ifdef NEED_SHORT_EXTERNAL_NAMES
#define jtransform_request_workspace		jTrRequest
#define jtransform_adjust_parameters		jTrAdjust
#define jtransform_execute_transform		jTrExec
#define jcopy_markers_setup					jCMrkSetup
#define jcopy_markers_execute				jCMrkExec
#endif /* NEED_SHORT_EXTERNAL_NAMES */


/*
 * Codes for supported types of image transformations.
 */


typedef enum tagJXFORM_CODE {
	JXFORM_NONE,		/* no transformation */
	JXFORM_FLIP_H,		/* horizontal flip */
	JXFORM_FLIP_V,		/* vertical flip */
	JXFORM_TRANSPOSE,	/* transpose across UL-to-LR axis */
	JXFORM_TRANSVERSE,	/* transpose across UR-to-LL axis */
	JXFORM_ROT_90,		/* 90-degree clockwise rotation */
	JXFORM_ROT_180,		/* 180-degree rotation */
	JXFORM_ROT_270		/* 270-degree clockwise (or 90 ccw) */
} JXFORM_CODE;

/*
 * Codes for crop parameters, which can individually be unspecified,
 * positive, or negative.  (Negative width or height makes no sense, though.)
 */

typedef enum tagJCROP_CODE {
	JCROP_UNSET,
	JCROP_POS,
	JCROP_NEG
} JCROP_CODE;

/*
 * Although rotating and flipping data expressed as DCT coefficients is not
 * hard, there is an asymmetry in the JPEG format specification for images
 * whose dimensions aren't multiples of the iMCU size.  The right and bottom
 * image edges are padded out to the next iMCU boundary with junk data; but
 * no padding is possible at the top and left edges.  If we were to flip
 * the whole image including the pad data, then pad garbage would become
 * visible at the top and/or left, and real pixels would disappear into the
 * pad margins --- perhaps permanently, since encoders & decoders may not
 * bother to preserve DCT blocks that appear to be completely outside the
 * nominal image area.  So, we have to exclude any partial iMCUs from the
 * basic transformation.
 *
 * Transpose is the only transformation that can handle partial iMCUs at the
 * right and bottom edges completely cleanly.  flip_h can flip partial iMCUs
 * at the bottom, but leaves any partial iMCUs at the right edge untouched.
 * Similarly flip_v leaves any partial iMCUs at the bottom edge untouched.
 * The other transforms are defined as combinations of these basic transforms
 * and process edge blocks in a way that preserves the equivalence.
 *
 * The "trim" option causes untransformable partial iMCUs to be dropped;
 * this is not strictly lossless, but it usually gives the best-looking
 * result for odd-size images.  Note that when this option is active,
 * the expected mathematical equivalences between the transforms may not hold.
 * (For example, -rot 270 -trim trims only the bottom edge, but -rot 90 -trim
 * followed by -rot 180 -trim trims both edges.)
 *
 * We also offer a "force to grayscale" option, which simply discards the
 * chrominance channels of a YCbCr image.  This is lossless in the sense that
 * the luminance channel is preserved exactly.  It's not the same kind of
 * thing as the rotate/flip transformations, but it's convenient to handle it
 * as part of this package, mainly because the transformation routines have to
 * be aware of the option to know how many components to work on.
 */

typedef struct {
  /* Options: set by caller */
  JXFORM_CODE transform;	/* image transform operator */
  boolean trim;			/* if TRUE, trim partial MCUs as needed */
  boolean force_grayscale;	/* if TRUE, convert color image to grayscale */

  boolean crop;			/* if TRUE, crop source image */

  /* Crop parameters: application need not set these unless crop is TRUE.
   * These can be filled in by jtransform_parse_crop_spec().
   */
  JDIMENSION crop_width;	/* Width of selected region */
  JCROP_CODE crop_width_set;
  JDIMENSION crop_height;	/* Height of selected region */
  JCROP_CODE crop_height_set;
  JDIMENSION crop_xoffset;	/* X offset of selected region */
  JCROP_CODE crop_xoffset_set;	/* (negative measures from right edge) */
  JDIMENSION crop_yoffset;	/* Y offset of selected region */
  JCROP_CODE crop_yoffset_set;	/* (negative measures from bottom edge) */

  /* Internal workspace: caller should not touch these */
  int num_components;		/* # of components in workspace */
  jvirt_barray_ptr * workspace_coef_arrays; /* workspace for transformations */
  JDIMENSION output_width;	/* cropped destination dimensions */
  JDIMENSION output_height;
  JDIMENSION x_crop_offset;	/* destination crop offsets measured in iMCUs */
  JDIMENSION y_crop_offset;
  int max_h_samp_factor;	/* destination iMCU size */
  int max_v_samp_factor;


} jpeg_transform_info;


#if TRANSFORMS_SUPPORTED

/* Parse a crop specification (written in X11 geometry style) */
EXTERN(boolean) jtransform_parse_crop_spec
	JPP((jpeg_transform_info *info, const char *spec));
/* Request any required workspace */
EXTERN(void) jtransform_request_workspace
	JPP((j_decompress_ptr srcinfo, jpeg_transform_info *info));
/* Adjust output image parameters */
EXTERN(jvirt_barray_ptr *) jtransform_adjust_parameters
	JPP((j_decompress_ptr srcinfo, j_compress_ptr dstinfo,
	     jvirt_barray_ptr *src_coef_arrays,
	     jpeg_transform_info *info));
/* Execute the actual transformation, if any */
EXTERN(void) jtransform_execute_transform
	JPP((j_decompress_ptr srcinfo, j_compress_ptr dstinfo,
	     jvirt_barray_ptr *src_coef_arrays,
	     jpeg_transform_info *info,
		 IW::IStatus *pStatus));

EXTERN(void) adjust_exif_parameters 
	JPP((JOCTET FAR * data, unsigned int length,
			JDIMENSION new_width, JDIMENSION new_height));

#endif /* TRANSFORMS_SUPPORTED */


/*
 * Support for copying optional markers from source to destination file.
 */

typedef enum tagJCOPY_OPTION {
	JCOPYOPT_NONE,		/* copy no optional markers */
	JCOPYOPT_COMMENTS,	/* copy only comment (COM) markers */
	JCOPYOPT_ALL,		/* copy all optional markers */
	JCOPYOPT_ALL_EXCEPT_PROFILES	/* all but Exif, XMP, ICC and IPTC */
} JCOPY_OPTION;

#define JCOPYOPT_DEFAULT  JCOPYOPT_COMMENTS	/* recommended default */

/* Setup decompression object to save desired markers in memory */
EXTERN(void) jcopy_markers_setup
	JPP((j_decompress_ptr srcinfo, JCOPY_OPTION option));
/* Copy markers saved in the given source object to the destination object */
EXTERN(void) jcopy_markers_execute
	JPP((j_decompress_ptr srcinfo, j_compress_ptr dstinfo,
	     JCOPY_OPTION option));


// Expanded data source object for ImageWalker stream input

typedef struct 
{
  struct jpeg_source_mgr pub;	/* public fields */

  IW::IStreamIn *pStream;		// source stream
  JOCTET * buffer;		// start of buffer
  boolean start_of_file;	// have we gotten any data yet? 
  IW::IImageStream *pImageOut;
} 
my_source_mgr;

typedef my_source_mgr * my_src_ptr;

// Helpers to setup the data
extern "C" void jpeg_iw_dest (j_compress_ptr cinfo, IW::IStreamOut *outfile);
extern "C" void jpeg_iw_src (j_decompress_ptr cinfo, IW::IStreamIn *infile, IW::IImageStream *pImageOut);


bool IsIptcBlob(jpeg_saved_marker_ptr marker);
bool IsXmpBlob(jpeg_saved_marker_ptr marker);
bool IsIccBlob(jpeg_saved_marker_ptr marker);
bool IsExifBlob(jpeg_saved_marker_ptr marker);

IW::MetaData LoadXmpBlob(jpeg_saved_marker_ptr marker);
IW::MetaData LoadExifBlob(jpeg_saved_marker_ptr marker);

// APP13 and APP2 can each span several markers, so these take the whole list.
IW::MetaData LoadIptcProfile(jpeg_saved_marker_ptr markers);
IW::MetaData LoadIccProfile(jpeg_saved_marker_ptr markers);

void WriteIptcBlob(j_compress_ptr dstinfo, const IW::MetaData &blob);
void WriteXmpBlob(j_compress_ptr dstinfo, const IW::MetaData &blob);
void WriteIccBlob(j_compress_ptr dstinfo, const IW::MetaData &blob);
void WriteExifBlob(j_compress_ptr dstinfo, const IW::MetaData &blob, int nOrientation);

// Folds the file's own EXIF orientation into a transform the user asked for and
// clears the tag, so the result is upright however the camera stored it. Must
// run after jpeg_read_header and before jtransform_request_workspace: the
// composed transform is what decides how much workspace is needed.
void jtransform_normalise_exif_orientation(j_decompress_ptr srcinfo, jpeg_transform_info *info);
JXFORM_CODE jtransform_compose_exif_orientation(JXFORM_CODE code, int nOrientation);
