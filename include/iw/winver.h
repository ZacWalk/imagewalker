///////////////////////////////////////////////////////////////////////
//
// This file is part of the ImageWalker code base.
// For more information on ImageWalker see www.ImageWalker.com
//
// Copyright (C) 1998-2001 Zac Walker.  All rights reserved.
//
///////////////////////////////////////////////////////////////////////


#pragma once

// We use #define _WIN32_IE 0x400 but I also
// need a few defines from _WIN32_IE 0x500

#ifndef BTNS_AUTOSIZE
#define BTNS_BUTTON     TBSTYLE_BUTTON      // 0x0000
#define BTNS_SEP        TBSTYLE_SEP         // 0x0001
#define BTNS_AUTOSIZE   TBSTYLE_AUTOSIZE    // 0x0010; automatically calculate the cx of the button
#define BTNS_DROPDOWN   TBSTYLE_DROPDOWN    // 0x0008
#define BTNS_WHOLEDROPDOWN  0x0080          // draw drop-down arrow, but without split arrow section
#endif

#ifndef TBN_INITCUSTOMIZE
#define TBN_RESTORE             (TBN_FIRST - 21)
#define TBN_SAVE                (TBN_FIRST - 22)
#define TBN_INITCUSTOMIZE       (TBN_FIRST - 23)
#define    TBNRF_HIDEHELP       0x00000001
#define    TBNRF_ENDCUSTOMIZE   0x00000002
#endif // TBN_INITCUSTOMIZE

#ifndef RBBS_USECHEVRON
#define RBBS_USECHEVRON     0x00000200  // display drop-down button for this band if it's sized smaller than ideal width
#endif // TBN_INITCUSTOMIZE

#ifndef TTS_BALLOON
#define TTS_BALLOON             0x40
#endif //TTS_BALLOON


#ifndef WM_XBUTTONUP

#define WM_XBUTTONDOWN                  0x020B
#define WM_XBUTTONUP                    0x020C
#define WM_XBUTTONDBLCLK                0x020D

#define GET_KEYSTATE_WPARAM(wParam)     (LOWORD(wParam))
#define GET_NCHITTEST_WPARAM(wParam)    ((short)LOWORD(wParam))
#define GET_XBUTTON_WPARAM(wParam)      (HIWORD(wParam))

// XButton values are WORD flags
#define XBUTTON1      0x0001
#define XBUTTON2      0x0002

#undef WM_MOUSELAST
#define WM_MOUSELAST                    0x020D

#endif // WM_XBUTTONUP
