// ImageWalker by Zac Walker
//
// Purpose: CPU feature detection for the blitters - checks that the OS has
//          agreed to save YMM state before reporting AVX2.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#include <libiw/PixelOps.h>
#include "LegacyPch.h"

IW::SimdLevel IW::GetSimdLevel()
{
	return iw::imaging::has_avx2() ? SimdLevel::Avx2 : SimdLevel::Sse2;
}
