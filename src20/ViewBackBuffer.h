// ImageWalker by Zac Walker
//
// Purpose: The cross-fade between two images, used when the viewer changes
//          picture.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#pragma once


class BackBuffer
{
private:

	CDC			  _dc;
	CDCHandle     _dcTarget;          // Owner DC
	CBitmap       _bitmap;      // Offscreen bitmap
	CBitmapHandle _hOldBitmap;  // Originally selected bitmap
	CRect         _rc;          // Rectangle of drawing area


public:

	BackBuffer()
	{
	}

	BackBuffer(HDC hDC, LPRECT pRect = 0)
	{
		Init(hDC, pRect);
	}

	~BackBuffer()
	{
		_dc.SelectBitmap(_hOldBitmap);
	}

	void Init(HDC hDC, LPRECT pRect = 0)
	{
		ATLASSERT(hDC!=NULL);
		_dcTarget = hDC;
		if( pRect!=NULL ) _rc = *pRect; else _dcTarget.GetClipBox(&_rc);

		if( _dc.CreateCompatibleDC(_dcTarget)==NULL ) return;
		_dcTarget.LPtoDP(_rc);
		if( _bitmap.CreateCompatibleBitmap(_dcTarget, _rc.Width(), _rc.Height())==NULL ) return;
		_hOldBitmap = _dc.SelectBitmap(_bitmap);
		_dc.SetWindowOrg(_rc.TopLeft());
	}

	bool IsRectEmpty() const
	{
		return _rc.IsRectEmpty() != 0;
	}

	HDC GetDC()
	{
		return _dc;
	}

	void Capture(HWND hWnd)
	{
		::SendMessage(hWnd, WM_PRINTCLIENT, (WPARAM)(HDC)_dc, PRF_CLIENT | PRF_CHILDREN);
	}

	// The pixels that are on screen, rather than a fresh render of them.
	void CaptureFromWindow(HWND hWnd)
	{
		CClientDC dc(hWnd);
		_dc.BitBlt(_rc.left, _rc.top, _rc.Width(), _rc.Height(), dc, _rc.left, _rc.top, SRCCOPY);
	}

	void Flip()
	{
		Draw(_dcTarget);
	}

	void Draw(CDCHandle dcTarget)
	{
		// Copy the offscreen bitmap onto the screen.
		dcTarget.BitBlt(_rc.left, _rc.top, _rc.Width(), _rc.Height(), _dc, _rc.left, _rc.top, SRCCOPY);
	}

	void Blend(int rate)
	{
		Blend(_dcTarget, _rc, rate);
	}

	void Blend(CDCHandle dcTarget, const CRect &rc, int rate)
	{
		BLENDFUNCTION bf = {0};
		bf.BlendOp = AC_SRC_OVER; 
		bf.SourceConstantAlpha = static_cast<BYTE>(IW::Clamp(rate, 0, 255));

		dcTarget.AlphaBlend(rc.left, rc.top, rc.Width(), rc.Height(),
			_dc, rc.left, rc.top, rc.Width(), rc.Height(), bf);
	}
};

// A cross fade that does not block.
//
// Capture() takes what is on screen at that moment; every paint after it
// composites that frame over the new content at a falling alpha, and the
// window's existing frame timer advances it. Capturing again part way through
// grabs the *blended* frame, so a picture that arrives mid-fade interrupts and
// the fade continues from what the user can actually see.
//
// It replaces a version that pumped its own twenty frame loop with Sleep in it,
// which meant every transition held the message queue for a third of a second
// and two of them ran back to back for one image.
class FadeOverlay
{
private:

	std::unique_ptr<BackBuffer> _pBackBuffer;
	CRect _rect;
	int _alpha;
	int _step;

public:

	enum
	{
		// Timer ticks to fade over, at the frame timer's 20Hz.
		defaultSteps = 5,

		// A picture replacing its own thumbnail is the same picture coming into
		// focus, not a change of subject, and the transition before it has only
		// just started. Dragging it out is what made one image change look like
		// the previous picture hanging around.
		quickSteps = 2
	};

	FadeOverlay() : _rect(0, 0, 0, 0), _alpha(0), _step(0)
	{
	}

	bool IsActive() const
	{
		return _alpha > 0;
	}

	void Clear()
	{
		_alpha = 0;
	}

	void Capture(HWND hWnd, int nSteps = defaultSteps)
	{
		if (hWnd == nullptr || !::IsWindow(hWnd) || !::IsWindowVisible(hWnd))
			return;

		CRect rc;
		::GetClientRect(hWnd, rc);

		if (rc.IsRectEmpty())
			return;

		if (IsActive() && _pBackBuffer && rc == _rect)
		{
			// Mid-fade the composite on screen is the only truth. Re-rendering
			// would compose at whatever _alpha has ticked to since the last
			// paint, which is not what the user is looking at, and the new fade
			// would start from a frame nobody saw.
			//
			// Reading the screen needs no second buffer either, which matters:
			// stepping quickly through a folder would otherwise build and throw
			// away a pane-sized bitmap per image.
			//
			// The paint for the previous change may still be queued, and the
			// screen would then be a frame behind the alpha this is meant to
			// capture, so settle it first.
			::UpdateWindow(hWnd);
			_pBackBuffer->CaptureFromWindow(hWnd);
		}
		else
		{
			CClientDC dc(hWnd);

			std::unique_ptr<BackBuffer> pNew(new BackBuffer);
			pNew->Init(dc, rc);

			if (pNew->IsRectEmpty())
				return;

			// Nothing is composited here, so the window's own paint is the
			// screen -- and unlike a blit it does not care whether the window is
			// obscured. It must not become the live buffer until it has run,
			// because that paint reads the old one.
			pNew->Capture(hWnd);

			_pBackBuffer = std::move(pNew);
			_rect = rc;
		}

		_alpha = 255;
		_step = IW::LowerLimit<1>(255 / IW::LowerLimit<1>(nSteps));
	}

	// Composited at the end of the window's own painting.
	void Draw(HWND hWnd, CDCHandle dc)
	{
		if (_alpha <= 0 || !_pBackBuffer)
			return;

		CRect rc;
		::GetClientRect(hWnd, rc);

		// A resize part way through leaves a capture of the wrong shape.
		if (rc != _rect)
		{
			_alpha = 0;
			return;
		}

		_pBackBuffer->Blend(dc, _rect, _alpha);
	}

	// One tick of the frame timer. True when the window needs repainting.
	bool Animate()
	{
		if (_alpha <= 0)
			return false;

		_alpha -= _step;

		if (_alpha <= 0)
		{
			_alpha = 0;
			_pBackBuffer.reset();
		}

		return true;
	}
};
