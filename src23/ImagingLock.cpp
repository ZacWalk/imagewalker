// ImageWalker by Zac Walker
//
// Purpose: The one runtime pixel-format switch, mapping a page onto the
//          concrete lock that reads it. Everything else lives in ImagingLock.h.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#include "stdafx.h"
#include "ImagingLock.h"

const IW::IImageSurfaceLock* IW::Page::GetSurfaceLock() const
{
	return const_cast<Page*>(this)->GetSurfaceLock();
};

IW::IImageSurfaceLock* IW::Page::GetSurfaceLock()
{
	switch (GetPixelFormat()._pf)
	{
	case PixelFormat::PF1:
		return new RefObj<SurfaceLockRef<PixelFormat::PF1>>(*this);
	case PixelFormat::PF2:
		return new RefObj<SurfaceLockRef<PixelFormat::PF2>>(*this);
	case PixelFormat::PF4:
		return new RefObj<SurfaceLockRef<PixelFormat::PF4>>(*this);
	case PixelFormat::PF8:
		return new RefObj<SurfaceLockRef<PixelFormat::PF8>>(*this);
	case PixelFormat::PF8Alpha:
		return new RefObj<SurfaceLockRef<PixelFormat::PF8Alpha>>(*this);
	case PixelFormat::PF8GrayScale:
		return new RefObj<SurfaceLockRef<PixelFormat::PF8GrayScale>>(*this);
	case PixelFormat::PF555:
		return new RefObj<SurfaceLockRef<PixelFormat::PF555>>(*this);
	case PixelFormat::PF565:
		return new RefObj<SurfaceLockRef<PixelFormat::PF565>>(*this);
	case PixelFormat::PF24:
		return new RefObj<SurfaceLockRef<PixelFormat::PF24>>(*this);
	case PixelFormat::PF32:
		return new RefObj<SurfaceLockRef<PixelFormat::PF32>>(*this);
	case PixelFormat::PF32Alpha:
		return new RefObj<SurfaceLockRef<PixelFormat::PF32Alpha>>(*this);
	}

	return nullptr;
};
