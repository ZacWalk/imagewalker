// ImageWalker by Zac Walker
//
// Purpose: ImageEdits: the non-destructive edit stack - rotation,
//          straighten, crop and colour, as values rather than pixels.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

//
// ImageEdits: the non-destructive edit stack the edit mode presents.
//
//////////////////////////////////////////////////////////////////////

#pragma once

// One value object for everything the edit panel can change, modelled on
// Diffractor's image_edits. Nothing here touches pixels until Apply runs, so
// the panel can rebuild a preview from the source on every slider move and the
// only undo state that exists is this struct.
class ImageEdits
{
public:

	enum
	{
		// Slider extents. Everything is an int so the panel can drive it with a
		// trackbar and the ini can round-trip it.
		ColorMin = -100,
		ColorMax = 100,
		StraightenMin = -450,	// tenths of a degree
		StraightenMax = 450,
		PerspectiveMin = -100,
		PerspectiveMax = 100
	};

	// Geometry
	int _rotate = 0;			// quarter turns clockwise, 0..3
	int _straighten = 0;		// tenths of a degree
	int _perspectiveH = 0;
	int _perspectiveV = 0;
	CRect _crop;				// in transformed pixels; empty means the whole image

	// Tone and colour, 0 is neutral throughout
	int _brightness = 0;
	int _contrast = 0;
	int _darks = 0;
	int _midtones = 0;
	int _lights = 0;
	int _saturation = 0;
	int _vibrance = 0;
	int _temperature = 0;
	int _tint = 0;

	ImageEdits()
	{
		_crop.SetRectEmpty();
	}

	friend bool operator==(const ImageEdits &lhs, const ImageEdits &rhs)
	{
		return lhs._rotate == rhs._rotate
			&& lhs._straighten == rhs._straighten
			&& lhs._perspectiveH == rhs._perspectiveH
			&& lhs._perspectiveV == rhs._perspectiveV
			&& lhs._crop == rhs._crop
			&& lhs._brightness == rhs._brightness
			&& lhs._contrast == rhs._contrast
			&& lhs._darks == rhs._darks
			&& lhs._midtones == rhs._midtones
			&& lhs._lights == rhs._lights
			&& lhs._saturation == rhs._saturation
			&& lhs._vibrance == rhs._vibrance
			&& lhs._temperature == rhs._temperature
			&& lhs._tint == rhs._tint;
	}

	friend bool operator!=(const ImageEdits &lhs, const ImageEdits &rhs)
	{
		return !(lhs == rhs);
	}

	void ResetGeometry()
	{
		_rotate = 0;
		_straighten = 0;
		_perspectiveH = 0;
		_perspectiveV = 0;
		_crop.SetRectEmpty();
	}

	void ResetColor()
	{
		_brightness = 0;
		_contrast = 0;
		_darks = 0;
		_midtones = 0;
		_lights = 0;
		_saturation = 0;
		_vibrance = 0;
		_temperature = 0;
		_tint = 0;
	}

	void Reset()
	{
		ResetGeometry();
		ResetColor();
	}

	bool HasCrop() const
	{
		return !_crop.IsRectEmpty();
	}

	bool HasWarp() const
	{
		return _straighten != 0 || _perspectiveH != 0 || _perspectiveV != 0;
	}

	bool HasGeometry() const
	{
		return _rotate != 0 || HasWarp() || HasCrop();
	}

	bool HasColor() const
	{
		return _brightness != 0 || _contrast != 0 || _darks != 0 || _midtones != 0 ||
			_lights != 0 || _saturation != 0 || _vibrance != 0 ||
			_temperature != 0 || _tint != 0;
	}

	bool IsEmpty() const
	{
		return !HasGeometry() && !HasColor();
	}

	// The size Apply will produce for a source of this size, crop excluded --
	// what the crop rectangle is expressed in.
	CSize TransformedSize(CSize sizeIn) const;

	// Where a crop is allowed to be. Straightening turns the picture inside a
	// frame of the same size, so the corners of the frame are empty; this is the
	// largest upright rectangle that still lands entirely on the picture.
	CRect CropBounds(CSize sizeIn) const;
};

namespace IW
{
	// Aspect-preserving fit of sizeIn into rc, centred, never magnified past
	// 100%. scaleOut is what one source pixel is worth on screen.
	//
	// Written out in doubles on purpose. Routing the two ratios through
	// IW::Min, which takes ints, truncated every ratio below 1 to 0 -- so any
	// photo larger than the pane fitted into a rectangle one pixel across and
	// the edit view showed an empty canvas.
	inline CRect FitToRect(CSize sizeIn, const CRect &rc, double &scaleOut)
	{
		scaleOut = 1.0;

		if (sizeIn.cx < 1 || sizeIn.cy < 1 || rc.Width() < 1 || rc.Height() < 1)
			return CRect(0, 0, 0, 0);

		const double fitX = static_cast<double>(rc.Width()) / sizeIn.cx;
		const double fitY = static_cast<double>(rc.Height()) / sizeIn.cy;
		const double fit = fitX < fitY ? fitX : fitY;

		scaleOut = fit < 1.0 ? fit : 1.0;

		const int cx = static_cast<int>(sizeIn.cx * scaleOut + 0.5);
		const int cy = static_cast<int>(sizeIn.cy * scaleOut + 0.5);

		CRect rcOut(0, 0, cx < 1 ? 1 : cx, cy < 1 ? 1 : cy);

		rcOut.OffsetRect(rc.left + (rc.Width() - rcOut.Width()) / 2,
		                 rc.top + (rc.Height() - rcOut.Height()) / 2);

		return rcOut;
	}

	// Runs the whole stack: rotate, warp, crop, then tone and colour. imageOut
	// is a fresh image; metadata is carried across.
	bool ApplyEdits(const Image &imageIn, Image &imageOut, const ImageEdits &edits, IStatus *pStatus);

	// Geometry only, in the coordinates the crop rectangle uses. The edit view
	// draws its crop handles over the result of this.
	bool ApplyEditGeometry(const Image &imageIn, Image &imageOut, const ImageEdits &edits, IStatus *pStatus);

	// Tone and colour only, in place-equivalent.
	bool ApplyEditColor(const Image &imageIn, Image &imageOut, const ImageEdits &edits, IStatus *pStatus);

	// Picks brightness, contrast and temperature from the image histogram, the
	// way Diffractor's auto_color does. Leaves geometry alone.
	void AutoColor(const Image &imageIn, ImageEdits &edits);

	// Picks _straighten from the dominant near-horizontal/vertical edge angle.
	void AutoStraighten(const Image &imageIn, ImageEdits &edits);
}
