/*///////////////////////////////////////////////////////////////////////
//
// This file is part of the ImageWalker code base.
// For more information on ImageWalker see www.ImageWalker.com
//
// Copyright (C) 1998-2001 Zac Walker.  All rights reserved.
//
///////////////////////////////////////////////////////////////////////*/

#ifndef _IW_HELPIDS_H_
#define _IW_HELPIDS_H_

// Context IDs for ImageWalker.chm, shared by the four WTL apps.
//
// One help file serves ArtMate 1.0 and ImageWalker 2.0, 2.2 and 2.3, so these
// numbers must agree across those trees. The historical versions had conflicting
// IDs; this shared table keeps each topic consistent in the current builds.
//
// Help/ImageWalker.hhp pulls this file in through its [MAP] section and pairs
// each name with a topic in its [ALIAS] section, so a name added here without
// a matching [ALIAS] line is reported as a warning by tools/chmbuild.py.
// Numbers are deliberately never reused: an ID baked into an old build should
// resolve to the right topic or to nothing, never to the wrong one.

/* General topics */
#define HELP_ARTMATE          131080
#define HELP_VERSIONS         131081
#define HELP_VIEW_OPTIONS     131082
#define HELP_USER_INTERFACE   131083
#define HELP_PROCESSCATALOGUE 131084
// 131085 retired with wallpaper support.
#define HELP_CONTACT_SUPPORT  131086
#define HELP_DESCRIPTIONS     131087
#define HELP_PRINTING         131089
#define HELP_ACTION           131090
#define HELP_IMAGE_LOADER     131091
#define HELP_FILTER           131092
#define HELP_TOOL             131093
#define HELP_SEARCH           131094
// 131095 retired with scanner acquisition.
#define HELP_FORMATS          131096
#define HELP_MAINFRAME        131181

/* Filters */
#define HELP_FILTER_BLUR                   131200
#define HELP_FILTER_COLOR_ADJUST           131201
#define HELP_FILTER_CONTRAST_STRETCH       131202
#define HELP_FILTER_CROP                   131203
#define HELP_FILTER_CROP_NOLOSS            131204
#define HELP_FILTER_EDGE                   131205
#define HELP_FILTER_EMBOS                  131206
#define HELP_FILTER_FRAME                  131207
#define HELP_FILTER_GRAY_SCALE             131208
#define HELP_FILTER_HISTOGRAM_EQUALIZATION 131209
#define HELP_FILTER_RED_EYE                131210
#define HELP_FILTER_ROTATE                 131211
#define HELP_FILTER_ROTATE_NOLOSS          131212
#define HELP_FILTER_RESIZE                 131213
#define HELP_FILTER_SHADOW                 131214
#define HELP_FILTER_SHARPEN                131215
#define HELP_FILTER_SOFTEN                 131216
#define HELP_FILTER_DITHER                 131218
#define HELP_FILTER_QUANTIZE               131219
#define HELP_FILTER_TEXT_CAPTION           131220
#define HELP_FILTER_NEGATE                 131223

/* Batch tools */
#define HELP_TOOL_RENAME   131250
#define HELP_TOOL_CONVERT  131251
#define HELP_TOOL_GIF_SS   131252
#define HELP_TOOL_FILTER   131253
#define HELP_TOOL_JPEG     131254
#define HELP_TOOL_PROPERTY 131255
#define HELP_TOOL_RESIZE   131257

/* Older spellings, kept so per-version call sites need not change.
   These are the same topics, not new ones. */
#define HELP_FILTER_SCALE        HELP_FILTER_RESIZE
#define HELP_CONVERT             HELP_TOOL_CONVERT
#define HELP_SCALE               HELP_TOOL_RESIZE
#define HELP_RENAME              HELP_TOOL_RENAME
#define HELP_ANI_GIF_SS          HELP_TOOL_GIF_SS
#define HELP_EXPORT_DESCRIPTIONS HELP_DESCRIPTIONS
#define HELP_IMPORT_DESCRIPTIONS HELP_DESCRIPTIONS

#endif // _IW_HELPIDS_H_
