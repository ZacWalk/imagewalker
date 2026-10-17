// ImageWalker by Zac Walker
//
// Purpose: CPU feature detection for the blitters - checks that the OS has
//          agreed to save YMM state before reporting AVX2.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#include "stdafx.h"

// CPU feature detection for the blitters in ImagingBlitterSimd.h. This was a
// CPUID probe written in x86 assembly, which MSVC rejects on x64.

static bool HasAvx2()
{
	int info[4] = {0, 0, 0, 0};
	__cpuid(info, 0);

	if (info[0] < 7)
		return false;

	__cpuid(info, 1);

	constexpr int kOsXSave = 1 << 27;
	constexpr int kAvx = 1 << 28;

	// An AVX2 instruction faults unless the OS has agreed to save YMM state, so
	// the CPUID feature bit on its own is not enough to run one.
	if ((info[2] & (kOsXSave | kAvx)) != (kOsXSave | kAvx))
		return false;

	if ((_xgetbv(0) & 0x6) != 0x6) // XMM and YMM state enabled
		return false;

	__cpuidex(info, 7, 0);

	return (info[1] & (1 << 5)) != 0; // EBX bit 5 is AVX2
}

IW::SimdLevel IW::GetSimdLevel()
{
	// SSE2 is part of the x64 baseline, so Scalar is the reference the other
	// two are tested against rather than a level this ever returns.
	static const SimdLevel level = HasAvx2() ? SimdLevel::Avx2 : SimdLevel::Sse2;
	return level;
}
