// ImageWalker by Zac Walker
//
// Purpose: The skinned frame - the gradient chrome, the custom-drawn toolbar
//          buttons and the window shape.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#pragma once

#define WM_NCMOUSELEAVE                 0x02A2
#define TME_NONCLIENT   0x00000010

namespace IW
{
	namespace Skin
	{

		void DrawGradient(CDCHandle dc, const CRect &r, DWORD c1, DWORD c2);
		void DrawArrow(CDCHandle& dc, const CRect &rc);
		void DrawBitmapDisabled(CDCHandle& dc, HIMAGELIST hImageList, POINT point, int nImage);
		void DrawButton(LPNMTBCUSTOMDRAW lpTBCustomDraw, HIMAGELIST hImageList, const CSize &sizeImage, bool bDrawArrows = true);
		void DrawMenuItem(LPDRAWITEMSTRUCT lpDrawItemStruct);
	}
}
