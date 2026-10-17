// ImageWalker by Zac Walker
//
// Purpose: CFilterResize implementation.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

//
// IW::Image: implementation
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "ViewModelState.h"
#include "ImagingFilter.h"
#include "FileJpeg.h"
#include "ViewDialogs.h"
#include "ViewResizeDlg.h"

bool CFilterResize::ApplyFilter(const IW::Image& imageIn, IW::Image& imageOut, IW::IStatus* pStatus)
{
	CSize s(IW::Max(1, m_nWidth), IW::Max(1, m_nHeight));

	if (m_nType == 1)
	{
		s.cx *= IW::MeterToInch(m_dwXPelsPerMeter);
		s.cy *= IW::MeterToInch(m_dwYPelsPerMeter);
	}
	else if (m_nType == 2)
	{
		s.cx *= IW::MeterToCM(m_dwXPelsPerMeter);
		s.cy *= IW::MeterToCM(m_dwYPelsPerMeter);
	}

	if (m_bKeepAspect)
	{
		const CRect rc = imageIn.GetBoundingRect();

		int cxImage = rc.Width();
		int cyImage = rc.Height();

		// If we need to adjust
		constexpr int nDiv = 0x8000;

		int sy = MulDiv(s.cy, nDiv, cyImage);
		int sx = MulDiv(s.cx, nDiv, cxImage);

		if (sy < sx)
		{
			s.cx = MulDiv(s.cy, cxImage, cyImage);
		}
		else
		{
			s.cy = MulDiv(s.cx, cyImage, cxImage);
		}
	}

	// "Only shrink, never enlarge": the checkbox was read into the settings and
	// written to the ini, and nothing ever looked at it.
	if (m_bScaleDown)
	{
		const CRect rc = imageIn.GetBoundingRect();

		if (s.cx >= rc.Width() && s.cy >= rc.Height())
		{
			s.cx = rc.Width();
			s.cy = rc.Height();
		}
	}

	// Copy Image Meta Data
	imageOut.SetXPelsPerMeter(m_dwXPelsPerMeter);
	imageOut.SetYPelsPerMeter(m_dwYPelsPerMeter);

	if (m_nFilter != 0)
	{
		return Scale(s, m_nFilter - 1, imageIn, imageOut, pStatus);
	}

	return Scale(imageIn, imageOut, s, pStatus);
}

bool CFilterResize::DisplaySettingsDialog(const IW::Image& image)
{
	if (!image.IsEmpty())
	{
		m_bHasImage = true;
		const CRect rc = image.GetBoundingRect();
		m_nOriginalWidth = rc.Width();
		m_nOriginalHeight = rc.Height();
	}

	CFilterResizeDlg dlg(this, image);
	return IDOK == dlg.DoModal();
}
