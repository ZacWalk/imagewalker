// ImageWalker by Zac Walker
//
// Purpose: Both scalers: the fast blitter path used for thumbnails, and the
//          windowed resampling kernels used by the resize filter.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

//
// IW::CImage: implementation
//
//////////////////////////////////////////////////////////////////////

#include "LegacyPch.h"
#include "ImagingLock.h"
#include "ImagingStreams.h"
#include "ImagingBlitter.h"
#include "RenderImage.h"

#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES  1
#endif
#include <Math.h>

// A resampling kernel is a weighted average, and averaging straight RGBA lets
// the colour of a fully transparent pixel into its neighbours. Same reason
// SampleWarp in ImagingEdits.cpp works premultiplied.
static inline COLORREF PremultiplyPixel(COLORREF c)
{
	const unsigned a = IW::GetA(c);

	if (a == 255)
		return c;

	return IW::RGBA(static_cast<int>((IW::GetR(c) * a + 127) / 255),
	                static_cast<int>((IW::GetG(c) * a + 127) / 255),
	                static_cast<int>((IW::GetB(c) * a + 127) / 255),
	                static_cast<int>(a));
}

static inline COLORREF UnpremultiplyPixel(COLORREF c)
{
	const unsigned a = IW::GetA(c);

	if (a == 0)
		return 0;

	if (a == 255)
		return c;

	// 255 / a in 16.16. A premultiplied channel never exceeds its own alpha.
	const unsigned k = (255u << 16) / a;

	return IW::RGBA(static_cast<int>(IW::Min(static_cast<int>((IW::GetR(c) * k + 0x8000) >> 16), 255)),
	                static_cast<int>(IW::Min(static_cast<int>((IW::GetG(c) * k + 0x8000) >> 16), 255)),
	                static_cast<int>(IW::Min(static_cast<int>((IW::GetB(c) * k + 0x8000) >> 16), 255)),
	                static_cast<int>(a));
}

//////////////////////////////////////////////////////////////////////
// Filter Functions!!


static double J1(double x)
{
	double p, q;
	long i;
	static constexpr double Pone[] =
	                        {
		                        0.581199354001606143928050809e+21,
		                        -0.6672106568924916298020941484e+20,
		                        0.2316433580634002297931815435e+19,
		                        -0.3588817569910106050743641413e+17,
		                        0.2908795263834775409737601689e+15,
		                        -0.1322983480332126453125473247e+13,
		                        0.3413234182301700539091292655e+10,
		                        -0.4695753530642995859767162166e+7,
		                        0.270112271089232341485679099e+4
	                        },
	                        Qone[] = {
		                        0.11623987080032122878585294e+22,
		                        0.1185770712190320999837113348e+20,
		                        0.6092061398917521746105196863e+17,
		                        0.2081661221307607351240184229e+15,
		                        0.5243710262167649715406728642e+12,
		                        0.1013863514358673989967045588e+10,
		                        0.1501793594998585505921097578e+7,
		                        0.1606931573481487801970916749e+4,
		                        0.1e+1
	                        };

	p = Pone[8];
	q = Qone[8];
	for (i = 7; i >= 0; i--)
	{
		p = p * x * x + Pone[i];
		q = q * x * x + Qone[i];
	}
	return (p / q);
}

static double P1(double x)
{
	double p, q;
	long i;

	static constexpr double Pone[] =
	                        {
		                        0.352246649133679798341724373e+5,
		                        0.62758845247161281269005675e+5,
		                        0.313539631109159574238669888e+5,
		                        0.49854832060594338434500455e+4,
		                        0.2111529182853962382105718e+3,
		                        0.12571716929145341558495e+1
	                        },
	                        Qone[] = {
		                        0.352246649133679798068390431e+5,
		                        0.626943469593560511888833731e+5,
		                        0.312404063819041039923015703e+5,
		                        0.4930396490181088979386097e+4,
		                        0.2030775189134759322293574e+3,
		                        0.1e+1
	                        };

	p = Pone[5];
	q = Qone[5];
	for (i = 4; i >= 0; i--)
	{
		p = p * (8.0 / x) * (8.0 / x) + Pone[i];
		q = q * (8.0 / x) * (8.0 / x) + Qone[i];
	}
	return (p / q);
}

static double Q1(double x)
{
	double p, q;
	long i;

	static constexpr double Pone[] =
	                        {
		                        0.3511751914303552822533318e+3,
		                        0.7210391804904475039280863e+3,
		                        0.4259873011654442389886993e+3,
		                        0.831898957673850827325226e+2,
		                        0.45681716295512267064405e+1,
		                        0.3532840052740123642735e-1
	                        },
	                        Qone[] = {
		                        0.74917374171809127714519505e+4,
		                        0.154141773392650970499848051e+5,
		                        0.91522317015169922705904727e+4,
		                        0.18111867005523513506724158e+4,
		                        0.1038187585462133728776636e+3,
		                        0.1e+1
	                        };

	p = Pone[5];
	q = Qone[5];
	for (i = 4; i >= 0; i--)
	{
		p = p * (8.0 / x) * (8.0 / x) + Pone[i];
		q = q * (8.0 / x) * (8.0 / x) + Qone[i];
	}
	return (p / q);
}

static double BesselOrderOne(double x)
{
	double p, q;

	if (x == 0.0)
		return (0.0);
	p = x;
	if (x < 0.0)
		x = (-x);
	if (x < 8.0)
		return (p * J1(x));
	q = sqrt(2.0 / (M_PI * x)) * (P1(x) * (1.0 / sqrt(2.0) * (sin(x) - cos(x))) - 8.0 / x * Q1(x) *
		(-1.0 / sqrt(2.0) * (sin(x) + cos(x))));
	if (p < 0.0)
		q = (-q);
	return (q);
}

static double Bessel(const double x, const double support)
{
	if (x == 0.0)
		return (M_PI / 4.0);
	return (BesselOrderOne(M_PI * x) / (2.0 * x));
}

static double Sinc(const double x, const double support)
{
	if (x == 0.0)
		return (1.0);
	return (sin(M_PI * x) / (M_PI * x));
}

static double Blackman(const double x, const double support)
{
	return (0.42 + 0.5 * cos(M_PI * x) + 0.08 * cos(2 * M_PI * x));
}

static double BlackmanBessel(const double x, const double support)
{
	return (Blackman(x / support, support) * Bessel(x, support));
}

static double BlackmanSinc(const double x, const double support)
{
	return (Blackman(x / support, support) * Sinc(x, support));
}

static double Box(const double x, const double support)
{
	if (x < -0.5)
		return (0.0);
	if (x < 0.5)
		return (1.0);
	return (0.0);
}

static double Catrom(const double x, const double support)
{
	if (x < -2.0)
		return (0.0);
	if (x < -1.0)
		return (0.5 * (4.0 + x * (8.0 + x * (5.0 + x))));
	if (x < 0.0)
		return (0.5 * (2.0 + x * x * (-5.0 - 3.0 * x)));
	if (x < 1.0)
		return (0.5 * (2.0 + x * x * (-5.0 + 3.0 * x)));
	if (x < 2.0)
		return (0.5 * (4.0 + x * (-8.0 + x * (5.0 - x))));
	return (0.0);
}

static double Cubic(const double x, const double support)
{
	if (x < -2.0)
		return (0.0);
	if (x < -1.0)
		return ((2.0 + x) * (2.0 + x) * (2.0 + x) / 6.0);
	if (x < 0.0)
		return ((4.0 + x * x * (-6.0 - 3.0 * x)) / 6.0);
	if (x < 1.0)
		return ((4.0 + x * x * (-6.0 + 3.0 * x)) / 6.0);
	if (x < 2.0)
		return ((2.0 - x) * (2.0 - x) * (2.0 - x) / 6.0);
	return (0.0);
}

static double Gaussian(const double x, const double support)
{
	return (exp(-2.0 * x * x) * sqrt(2.0 / M_PI));
}

static double Hanning(const double x, const double support)
{
	return (0.5 + 0.5 * cos(M_PI * x));
}

static double Hamming(const double x, const double support)
{
	return (0.54 + 0.46 * cos(M_PI * x));
}

static double Hermite(const double x, const double support)
{
	if (x < -1.0)
		return (0.0);
	if (x < 0.0)
		return ((2.0 * (-x) - 3.0) * (-x) * (-x) + 1.0);
	if (x < 1.0)
		return ((2.0 * x - 3.0) * x * x + 1.0);
	return (0.0);
}

static double Lanczos(const double x, const double support)
{
	if (x < -3.0)
		return (0.0);
	if (x < 0.0)
		return (Sinc(-x, support) * Sinc(-x / 3.0, support));
	if (x < 3.0)
		return (Sinc(x, support) * Sinc(x / 3.0, support));
	return (0.0);
}

static double Mitchell(const double x, const double support)
{
	constexpr double B = (1.0 / 3.0);
	constexpr double C = (1.0 / 3.0);
	constexpr double P0 = ((6.0 - 2.0 * B) / 6.0);
	constexpr double P2 = ((-18.0 + 12.0 * B + 6.0 * C) / 6.0);
	constexpr double P3 = ((12.0 - 9.0 * B - 6.0 * C) / 6.0);
	constexpr double Q0 = ((8.0 * B + 24.0 * C) / 6.0);
	constexpr double Q1 = ((-12.0 * B - 48.0 * C) / 6.0);
	constexpr double Q2 = ((6.0 * B + 30.0 * C) / 6.0);
	constexpr double Q3 = ((-1.0 * B - 6.0 * C) / 6.0);

	if (x < -2.0)
		return (0.0);
	if (x < -1.0)
		return (Q0 - x * (Q1 - x * (Q2 - x * Q3)));
	if (x < 0.0)
		return (P0 + x * x * (P2 - x * P3));
	if (x < 1.0)
		return (P0 + x * x * (P2 + x * P3));
	if (x < 2.0)
		return (Q0 + x * (Q1 + x * (Q2 + x * Q3)));
	return (0.0);
}

static double Quadratic(const double x, const double support)
{
	if (x < -1.5)
		return (0.0);
	if (x < -0.5)
		return (0.5 * (x + 1.5) * (x + 1.5));
	if (x < 0.5)
		return (0.75 - x * x);
	if (x < 1.5)
		return (0.5 * (x - 1.5) * (x - 1.5));
	return (0.0);
}

static double Triangle(const double x, const double support)
{
	if (x < -1.0)
		return (0.0);
	if (x < 0.0)
		return (1.0 + x);
	if (x < 1.0)
		return (1.0 - x);
	return (0.0);
}

///////////////////////////////////////////////////////////////////////////////////

using FilterInfo = struct _FilterInfo
{
	double (*function)(double, double),
	       support;
};

static const FilterInfo filters[] =
{
	{Box, 0.0},
	{Box, 1.0},
	{Box, 0.5},
	{Triangle, 1.0},
	{Hermite, 1.0},
	{Hanning, 1.0},
	{Hamming, 1.0},
	{Blackman, 1.0},
	{Gaussian, 1.25},
	{Quadratic, 1.5},
	{Cubic, 2.0},
	{Catrom, 2.0},
	{Mitchell, 2.0},
	{Lanczos, 3.0},
	{BlackmanBessel, 3.2383},
	{BlackmanSinc, 4.0}
};

//	image rescaling routine

using ImageContrib = struct tagImageContrib
{
	int nPixel;
	int dWeight;
};

////////////////////////////////////////////////////////////////////////////////////////

static bool ScaleBufferCount(int width, int height, size_t itemSize, size_t& count)
{
	if (width <= 0 || height <= 0) return false;
	const auto size = static_cast<std::uint64_t>(width) * height;
	if (size > static_cast<std::uint64_t>(IW::kMaxPageBytes) / itemSize) return false;
	count = static_cast<size_t>(size);
	return true;
}

bool IW::Scale(const CSize& sizeIn, int nFilterType, const Image& imageIn, Image& imageOut, IStatus* pStatus)
{
	const CRect rcBounding = imageIn.GetBoundingRect();
	const auto boundingWidth = static_cast<std::int64_t>(rcBounding.right) - rcBounding.left;
	const auto boundingHeight = static_cast<std::int64_t>(rcBounding.bottom) - rcBounding.top;
	size_t outputCount{};
	if (boundingWidth <= 0 || boundingHeight <= 0 || boundingWidth > INT_MAX || boundingHeight > INT_MAX ||
		!ScaleBufferCount(sizeIn.cx, sizeIn.cy, sizeof(COLORREF), outputCount))
	{
		pStatus->SetError(L"Invalid or excessive image scaling dimensions.");
		return false;
	}
	const int cxBounding = static_cast<int>(boundingWidth);
	const int cyBounding = static_cast<int>(boundingHeight);

	// zoom scale factors
	const double dScaleX = static_cast<double>(sizeIn.cx) / static_cast<double>(cxBounding);
	const double dScaleY = static_cast<double>(sizeIn.cy) / static_cast<double>(cyBounding);

	if (nFilterType < 0 || nFilterType >= countof(filters)) nFilterType = 0;

	std::int64_t nProgress = 0;
	std::int64_t nProgressMax = 0;
	for (unsigned nPage = 0; nPage < imageIn.Pages.size(); nPage++)
	{
		int cy = imageIn.Pages[nPage].GetHeight();
		nProgressMax += cy;
		nProgressMax += MulDiv(cy, sizeIn.cy, cyBounding);
	}
	const auto progress = [&]
	{
		pStatus->Progress(static_cast<int>((std::min)(nProgress++, static_cast<std::int64_t>(INT_MAX))),
			static_cast<int>((std::min)(nProgressMax, static_cast<std::int64_t>(INT_MAX))));
	};

	for (unsigned nPage = 0; nPage < imageIn.Pages.size(); nPage++)
	{
		const Page& pageIn = imageIn.GetPage(nPage);
		ConstRefPtr<IImageSurfaceLock> pLockIn = pageIn.GetSurfaceLock();

		double dCenter, dWeight; // Filter calculation variables
		int nLeft, nRight, nTotal;
		double dWidth, dScale; // Filter calculation variables

		const CRect rcIn = pageIn.GetPageRect();

		const CRect rcOut(
			MulDiv(rcIn.left, sizeIn.cx, cxBounding),
			MulDiv(rcIn.top, sizeIn.cy, cyBounding),
			MulDiv(rcIn.right, sizeIn.cx, cxBounding),
			MulDiv(rcIn.bottom, sizeIn.cy, cyBounding));

		const int cxIn = rcIn.right - rcIn.left;
		const int cyIn = rcIn.bottom - rcIn.top;
		const auto outWidth = static_cast<std::int64_t>(rcOut.right) - rcOut.left;
		const auto outHeight = static_cast<std::int64_t>(rcOut.bottom) - rcOut.top;
		if (outWidth <= 0 || outHeight <= 0 || outWidth > INT_MAX || outHeight > INT_MAX)
		{
			pStatus->SetError(L"Invalid image scaling extent.");
			return false;
		}
		const int cxOut = static_cast<int>(outWidth);
		const int cyOut = static_cast<int>(outHeight);
		double (*FilterFunction)(double, double) = filters[nFilterType].function;
		// Box Point declares zero support: without a floor of half a pixel every
		// non-integral centre selects no source samples at all and comes out black.
		const double dFilterSupport = filters[nFilterType].support < 0.5 ? 0.5 : filters[nFilterType].support;

		const double contributionWidth = (std::max)(
			(dScaleX < 1.0 ? dFilterSupport / dScaleX : dFilterSupport) * 2 + 1,
			(dScaleY < 1.0 ? dFilterSupport / dScaleY : dFilterSupport) * 2 + 1);
		if (!std::isfinite(contributionWidth) || contributionWidth >= INT_MAX)
		{
			pStatus->SetError(L"The image scaling filter exceeds the memory limit.");
			return false;
		}
		const int nContribListWidth = static_cast<int>(contributionWidth);
		size_t temporaryCount{}, contributionCountX{}, contributionCountY{};
		if (!ScaleBufferCount(cxOut, cyOut, sizeof(COLORREF), outputCount) ||
			!ScaleBufferCount(cxOut, cyIn, sizeof(DWORD), temporaryCount) ||
			!ScaleBufferCount(cxOut, nContribListWidth, sizeof(ImageContrib), contributionCountX) ||
			!ScaleBufferCount(cyOut, nContribListWidth, sizeof(ImageContrib), contributionCountY))
		{
			pStatus->SetError(L"The image scaling buffers exceed the memory limit.");
			return false;
		}

		IW::CBuffer<DWORD> pTemp(temporaryCount);
		IW::CBuffer<int> pContrCountX(cxOut);
		IW::CBuffer<int> pContrCountY(cyOut);
		IW::CBuffer<ImageContrib> pContrX(contributionCountX);
		IW::CBuffer<ImageContrib> pContrY(contributionCountY);

		// pre-calculate Filter pContributions for a row
		if (dScaleX < 1.0)
		{
			dWidth = dFilterSupport / dScaleX;
			dScale = 1.0 / dScaleX;

			int nRoundFix = (cxOut / cxIn) >> 1;

			for (int i = 0; i < cxOut; ++i)
			{
				pContrCountX[i] = 0;
				dCenter = static_cast<double>(i - nRoundFix) / dScaleX;
				nLeft = static_cast<int>(ceil(dCenter - dWidth));
				nRight = static_cast<int>(floor(dCenter + dWidth));
				nTotal = 0;
				for (int j = nLeft; j <= nRight; ++j)
				{
					dWeight = dCenter - static_cast<double>(j);
					dWeight = (*FilterFunction)(dWeight / dScale, dFilterSupport) / dScale;
					int k = pContrCountX[i]++ + i * nContribListWidth;
					pContrX[k].nPixel = Clamp(j, 0, cxIn - 1);
					nTotal += pContrX[k].dWeight = DoubleToFixed(dWeight);
				}

				if (nTotal == 0) return false;
				// Normalize
				for (int j = 0; j < pContrCountX[i]; ++j)
				{
					int k = j + i * nContribListWidth;
					pContrX[k].dWeight = FixedDiv(pContrX[k].dWeight, nTotal);
				}
			}
		}
		else
		{
			int nRoundFix = (cxOut / cxIn) >> 1;

			for (int i = 0; i < cxOut; ++i)
			{
				pContrCountX[i] = 0;
				dCenter = static_cast<double>(i - nRoundFix) / dScaleX;
				nLeft = static_cast<int>(ceil(dCenter - dFilterSupport));
				nRight = static_cast<int>(floor(dCenter + dFilterSupport));
				nTotal = 0;
				for (int j = nLeft; j <= nRight; ++j)
				{
					dWeight = dCenter - static_cast<double>(j);
					dWeight = (*FilterFunction)(dWeight, dFilterSupport);
					int k = pContrCountX[i]++ + i * nContribListWidth;
					pContrX[k].nPixel = Clamp(j, 0, cxIn - 1);
					nTotal += pContrX[k].dWeight = DoubleToFixed(dWeight);
				}

				if (nTotal == 0) return false;
				// Normalize
				for (int j = 0; j < pContrCountX[i]; ++j)
				{
					int k = j + i * nContribListWidth;
					pContrX[k].dWeight = FixedDiv(pContrX[k].dWeight, nTotal);
				}
			}
		}

		// pre-calculate Filter pContributions for a column
		if (dScaleY < 1.0)
		{
			int nRoundFix = (cyOut / cyIn) >> 1;

			dWidth = dFilterSupport / dScaleY;
			dScale = 1.0 / dScaleY;
			nTotal = 0;
			for (int i = 0; i < cyOut; ++i)
			{
				pContrCountY[i] = 0;
				dCenter = static_cast<double>(i - nRoundFix) / dScaleY;
				nLeft = static_cast<int>(ceil(dCenter - dWidth));
				nRight = static_cast<int>(floor(dCenter + dWidth));
				nTotal = 0;
				for (int j = nLeft; j <= nRight; ++j)
				{
					dWeight = dCenter - static_cast<double>(j);
					dWeight = (*FilterFunction)(dWeight / dScale, dFilterSupport) / dScale;
					int k = pContrCountY[i]++ + i * nContribListWidth;
					pContrY[k].nPixel = Clamp(j, 0, cyIn - 1) * cxOut;
					nTotal += pContrY[k].dWeight = DoubleToFixed(dWeight);
				}

				if (nTotal == 0) return false;
				// Normalize
				for (int j = 0; j < pContrCountY[i]; ++j)
				{
					int k = j + i * nContribListWidth;
					pContrY[k].dWeight = FixedDiv(pContrY[k].dWeight, nTotal);
				}

				if (pStatus->QueryCancel())
				{
					return false;
				}
			}
		}
		else
		{
			int nRoundFix = (cyOut / cyIn) >> 1;

			for (int i = 0; i < cyOut; ++i)
			{
				pContrCountY[i] = 0;
				dCenter = static_cast<double>(i - nRoundFix) / dScaleY;
				nLeft = static_cast<int>(ceil(dCenter - dFilterSupport));
				nRight = static_cast<int>(floor(dCenter + dFilterSupport));
				nTotal = 0;
				for (int j = nLeft; j <= nRight; ++j)
				{
					dWeight = dCenter - static_cast<double>(j);
					dWeight = (*FilterFunction)(dWeight, dFilterSupport);
					int k = pContrCountY[i]++ + i * nContribListWidth;
					pContrY[k].nPixel = Clamp(j, 0, cyIn - 1) * cxOut;
					nTotal += pContrY[k].dWeight = DoubleToFixed(dWeight);
				}

				if (nTotal == 0) return false;
				// Normalize
				for (int j = 0; j < pContrCountY[i]; ++j)
				{
					int k = j + i * nContribListWidth;
					pContrY[k].dWeight = FixedDiv(pContrY[k].dWeight, nTotal);
				}

				if (pStatus->QueryCancel())
				{
					return false;
				}
			}
		}

		// we have to loop for every pane
		if (pStatus->QueryCancel())
		{
			return false;
		}


		const bool bAlpha = HasSourceAlpha(pageIn);
		PixelFormat pf(bAlpha ? PixelFormat::PF32Alpha : PixelFormat::PF24);
		Page pageOut = imageOut.CreatePage(rcOut, pf);

		// Scale the line
		RefPtr<IImageSurfaceLock> pLockOut = pageOut.GetSurfaceLock();
		IW::CBuffer<DWORD> pLineBuffer(IW::Max(cxIn, cxOut));

		for (int y = 0; (y < cyIn); y++)
		{
			LPDWORD pLineOut = pTemp + (cxOut * y);

			if (bAlpha)
			{
				ReadStraightLine(pageIn, pLockIn, pLineBuffer, y, 0, cxIn);

				for (int x = 0; x < cxIn; x++)
					pLineBuffer[x] = PremultiplyPixel(pLineBuffer[x]);
			}
			else
			{
				// Neither call promises an alpha byte for a format that has
				// none, and the accumulator below weights by it.
				pLockIn->RenderLine(pLineBuffer, y, 0, cxIn);

				for (int x = 0; x < cxIn; x++)
					pLineBuffer[x] |= 0xFF000000;
			}

			for (int x = 0; x < cxOut; x++)
			{
				int r = 0, g = 0, b = 0, a = 0;

				for (int i = 0; i < pContrCountX[x]; ++i)
				{
					int k = i + x * nContribListWidth;
					DWORD color = pLineBuffer[pContrX[k].nPixel];
					int weight = pContrX[k].dWeight;

					r += weight * GetB(color);
					g += weight * GetG(color);
					b += weight * GetR(color);
					a += weight * GetA(color);
				}

				// Check for overflow
				assert(pLineOut < pTemp + (cxOut * (y+1)));

				// Place result in destination pixel
				*pLineOut = SaturateRGBA(FixedToInt(b),
				                         FixedToInt(g),
				                         FixedToInt(r),
				                         FixedToInt(a));

				pLineOut++;
			}

			progress();
			if (pStatus->QueryCancel()) return false;
		}

		// apply Filter to zoom vertically from pImage to pDst
		for (int y = 0; (y < cyOut); y++)
		{
			auto pLineOut = (LPCBYTE)pLineBuffer.data();

			for (int x = 0; x < cxOut; x++)
			{
				int r = 0, g = 0, b = 0, a = 0;
				int k = y * nContribListWidth;

				for (int i = 0; i < pContrCountY[y]; ++i)
				{
					DWORD color = pTemp[pContrY[k].nPixel + x];
					int nWeight = pContrY[k].dWeight;

					r += nWeight * GetR(color);
					g += nWeight * GetG(color);
					b += nWeight * GetB(color);
					a += nWeight * (color >> 24);

					k += 1;
				}

				*((LPDWORD)pLineOut) = SaturateRGBA(FixedToInt(r),
				                                    FixedToInt(g),
				                                    FixedToInt(b),
				                                    FixedToInt(a)); // Place result in destination pixel

				if (bAlpha)
					*((LPDWORD)pLineOut) = UnpremultiplyPixel(*((LPDWORD)pLineOut));

				pLineOut += 4;
			}

			pLockOut->SetLine(pLineBuffer, y, 0, cxOut);

			progress();
			if (pStatus->QueryCancel())
				return false;
		}

		// copy over page attributes
		pageOut.CopyExtraInfo(pageIn);
	}

	imageOut.Normalize();
	IterateImageMetaData(imageIn, imageOut, pStatus);

	return true;
};



bool IW::Scale(const Image& imageIn, Image& imageOut, CSize size, IStatus* pStatus)
{
	const CRect rcBounding = imageIn.GetBoundingRect();
	const auto boundingWidth = static_cast<std::int64_t>(rcBounding.right) - rcBounding.left;
	const auto boundingHeight = static_cast<std::int64_t>(rcBounding.bottom) - rcBounding.top;
	size_t outputCount{};
	if (boundingWidth <= 0 || boundingHeight <= 0 || boundingWidth > INT_MAX || boundingHeight > INT_MAX ||
		!ScaleBufferCount(size.cx, size.cy, sizeof(COLORREF), outputCount))
	{
		pStatus->SetError(L"Invalid or excessive image scaling dimensions.");
		return false;
	}
	const int cxBounding = static_cast<int>(boundingWidth);
	const int cyBounding = static_cast<int>(boundingHeight);

	for (auto pageIn = imageIn.Pages.begin(); pageIn != imageIn.Pages.end(); ++pageIn)
	{
		if (pStatus->QueryCancel()) return false;
		CRect rcIn = pageIn->GetPageRect();

		CRect rcOut(
			MulDiv(rcIn.left, size.cx, cxBounding),
			MulDiv(rcIn.top, size.cy, cyBounding),
			MulDiv(rcIn.right, size.cx, cxBounding),
			MulDiv(rcIn.bottom, size.cy, cyBounding));

		PixelFormat pf(HasSourceAlpha(*pageIn) ? PixelFormat::PF32Alpha : PixelFormat::PF24);
		Page& pageOut = imageOut.CreatePage(rcOut, pf);
		const CRect sourceRect(0, 0, pageIn->GetWidth(), pageIn->GetHeight());

		// RenderImage is already a template on its canvas, and only two formats
		// ever reach here, so the canvas is named rather than dispatched.
		WithBlitter([&](auto& blitter)
		{
			typedef std::remove_reference_t<decltype(blitter)> TBlitter;

			if (pf.HasAlpha())
			{
				typedef SurfaceLock<PixelFormat::PF32Alpha> TLock;
				TLock lockOut(pageOut);
				RenderImage<TLock, TBlitter> render(lockOut, blitter);
				render.DrawImage(*pageIn, rcOut, sourceRect);
			}
			else
			{
				typedef SurfaceLock<PixelFormat::PF24> TLock;
				TLock lockOut(pageOut);
				RenderImage<TLock, TBlitter> render(lockOut, blitter);
				render.DrawImage(*pageIn, rcOut, sourceRect);
			}
		});

		pageOut.CopyExtraInfo(*pageIn);
	}

	imageOut.Normalize();
	IterateImageMetaData(imageIn, imageOut, pStatus);

	return true;
};
