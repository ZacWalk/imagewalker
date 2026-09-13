// ImageWalker by Zac Walker
//
// Purpose: Wu colour quantization - reduces a true colour image to a
//          palette.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

///////////////////////////////////////////////////////////////////////
//	    C Implementation of Wu's Color Quantizer (v. 2)
//	    (see Graphics Gems vol. II, pp. 126-133)
//
// Author:	Xiaolin Wu
// Dept. of Computer Science
// Univ. of Western Ontario
// London, Ontario N6A 5B7
// wu@csd.uwo.ca
// 
// Algorithm: Greedy orthogonal bipartition of RGB space for variance
// 	   minimization aided by inclusion-exclusion tricks.
// 	   For speed no nearest neighbor search is done. Slightly
// 	   better performance can be expected by more sophisticated
// 	   but more expensive versions.
// 
// The author thanks Tom Lane at Tom_Lane@render.GP.CS.CMU.EDU for much of
// additional documentation and a cure to a previous bug.
// 
// IW::Free to distribute, comments and suggestions are appreciated.
///////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////
// History
// -------
// July 2000: C++ Implementation of Wu's Color Quantizer
//            and adaptation for the IW::FreeImage 2 Library
//            Author: Herv� Drolon (drolon@iut.univ-lehavre.fr)
///////////////////////////////////////////////////////////////////////

#include "stdafx.h"

using Box = struct
{
	int r0; // min value, exclusive
	int r1; // max value, inclusive
	int g0;
	int g1;
	int b0;
	int b1;
	int vol;
};


class CQuantizer
{
protected:
	double* m_pGM2;
	IW::Int64 *m_pnWT, *m_pnMR, *m_pnMG, *m_pnMB;
	IW::UInt16* m_pnQadd;

	// DIB data
	int m_nWidth, m_nHeight;
	IW::Page m_page;

	void Hist3D(IW::Int64* vwt, IW::Int64* vmr, IW::Int64* vmg, IW::Int64* vmb, double* m2);
	void M3D(IW::Int64* vwt, IW::Int64* vmr, IW::Int64* vmg, IW::Int64* vmb, double* m2);
	IW::Int64 Vol(Box* cube, IW::Int64* mmt);
	IW::Int64 Bottom(Box* cube, IW::UInt8 dir, IW::Int64* mmt);
	IW::Int64 Top(Box* cube, IW::UInt8 dir, int pos, IW::Int64* mmt);
	double Var(Box* cube);
	double Maximize(Box* cube, IW::UInt8 dir, int first, int last, int* cut,
	               IW::Int64 whole_r, IW::Int64 whole_g, IW::Int64 whole_b, IW::Int64 whole_w);
	bool Cut(Box* set1, Box* set2);
	void Mark(Box* cube, int label, IW::UInt8* tag);

public:
	// Constructor - Input parameter: DIB 24-bit to be quantized
	CQuantizer(const IW::Page& page);
	// Destructor
	~CQuantizer();
	// Quantizer - Return value: quantized 8-bit (color palette) DIB
	bool Quantize(IW::Image& imageOut);
};

///////////////////////////////////////////////////////////////////////

// 3D array indexation

#define INDEX(r, g, b)	((r << 10) + (r << 6) + r + (g << 5) + g + b)

#define MAXCOLOR	256
#define	RED		2
#define	GREEN	1
#define BLUE	0


// Constructor / Destructor
CQuantizer::CQuantizer(const IW::Page& page)
{
	m_page = page;
	m_nWidth = page.GetWidth();
	m_nHeight = page.GetHeight();


	m_pGM2 = nullptr;
	m_pnWT = m_pnMR = m_pnMG = m_pnMB = nullptr;
	m_pnQadd = nullptr;

	// Allocate 3D arrays
	m_pGM2 = static_cast<double*>(IW::Alloc(33 * 33 * 33 * sizeof(double)));
	m_pnWT = static_cast<IW::Int64*>(IW::Alloc(33 * 33 * 33 * sizeof(IW::Int64)));
	m_pnMR = static_cast<IW::Int64*>(IW::Alloc(33 * 33 * 33 * sizeof(IW::Int64)));
	m_pnMG = static_cast<IW::Int64*>(IW::Alloc(33 * 33 * 33 * sizeof(IW::Int64)));
	m_pnMB = static_cast<IW::Int64*>(IW::Alloc(33 * 33 * 33 * sizeof(IW::Int64)));

	// Allocate m_pnQadd
	m_pnQadd = static_cast<IW::UInt16*>(IW::Alloc(sizeof(IW::UInt16) * m_nWidth * m_nHeight));

	IW::MemZero(m_pGM2, 35937 * sizeof(double));
	IW::MemZero(m_pnWT, 35937 * sizeof(IW::Int64));
	IW::MemZero(m_pnMR, 35937 * sizeof(IW::Int64));
	IW::MemZero(m_pnMG, 35937 * sizeof(IW::Int64));
	IW::MemZero(m_pnMB, 35937 * sizeof(IW::Int64));
	IW::MemZero(m_pnQadd, sizeof(IW::UInt16) * m_nWidth * m_nHeight);
}

CQuantizer::~CQuantizer()
{
	if (m_pGM2) IW::Free(m_pGM2);
	if (m_pnWT) IW::Free(m_pnWT);
	if (m_pnMR) IW::Free(m_pnMR);
	if (m_pnMG) IW::Free(m_pnMG);
	if (m_pnMB) IW::Free(m_pnMB);
	if (m_pnQadd) IW::Free(m_pnQadd);
}


// Histogram is in elements 1..HISTSIZE along each axis,
// element 0 is for base or marginal value
// NB: these must start out 0!

// Build 3-D color histogram of counts, r/g/b, c^2
void CQuantizer::Hist3D(IW::Int64* vwt, IW::Int64* vmr, IW::Int64* vmg, IW::Int64* vmb, double* m2)
{
	int ind;
	int inr, ing, inb, table[256];
	int i;
	int y, x;

	for (i = 0; i < 256; i++)
		table[i] = i * i;


	IW::ConstIImageSurfaceLockPtr pLock = m_page.GetSurfaceLock();
	COLORREF rr, gg, bb, c;
	const DWORD* p;
	IW::CBuffer<DWORD> pLine(m_nWidth);

	for (y = 0; y < m_nHeight; y++)
	{
		pLock->RenderLine(pLine, y, 0, m_nWidth);
		p = pLine;

		for (x = 0; x < m_nWidth; x++)
		{
			c = *p++;

			rr = IW::GetR(c);
			gg = IW::GetG(c);
			bb = IW::GetB(c);

			inr = (bb >> 3) + 1;
			ing = (gg >> 3) + 1;
			inb = (rr >> 3) + 1;

			// Each component is in [1, 32], so INDEX is at most 35936.
			ind = INDEX(inr, ing, inb);
			m_pnQadd[static_cast<size_t>(y) * m_nWidth + x] = static_cast<IW::UInt16>(ind);

			// [inr][ing][inb]
			vwt[ind]++;
			vmr[ind] += bb;
			vmg[ind] += gg;
			vmb[ind] += rr;

			m2[ind] += static_cast<double>(table[bb] +
				table[gg] +
				table[rr]);
		}
	}
}


// At conclusion of the histogram step, we can interpret
// m_pnWT[r][g][b] = sum over voxel of P(c)
// m_pnMR[r][g][b] = sum over voxel of r*P(c)  ,  similarly for m_pnMG, m_pnMB
// m2[r][g][b] = sum over voxel of c^2*P(c)
// Actually each of these should be divided by 'ImageSize' to give the usual
// interpretation of P() as ranging from 0 to 1, but we needn't do that here.


// We now convert histogram into moments so that we can rapidly calculate
// the sums of the above quantities over any desired box.

// Compute cumulative moments
void CQuantizer::M3D(IW::Int64* vwt, IW::Int64* vmr, IW::Int64* vmg, IW::Int64* vmb, double* m2)
{
	IW::UInt16 ind1, ind2;
	IW::UInt8 i, r, g, b;
	IW::Int64 line, line_r, line_g, line_b;
	IW::Int64 area[33], area_r[33], area_g[33], area_b[33];
	double line2, area2[33];

	for (r = 1; r <= 32; r++)
	{
		for (i = 0; i <= 32; i++)
		{
			area2[i] = 0;
			area[i] = area_r[i] = area_g[i] = area_b[i] = 0;
		}
		for (g = 1; g <= 32; g++)
		{
			line2 = 0;
			line = line_r = line_g = line_b = 0;
			for (b = 1; b <= 32; b++)
			{
				ind1 = INDEX(r, g, b); // [r][g][b]
				line += vwt[ind1];
				line_r += vmr[ind1];
				line_g += vmg[ind1];
				line_b += vmb[ind1];
				line2 += m2[ind1];
				area[b] += line;
				area_r[b] += line_r;
				area_g[b] += line_g;
				area_b[b] += line_b;
				area2[b] += line2;
				ind2 = ind1 - 1089; // [r-1][g][b]
				vwt[ind1] = vwt[ind2] + area[b];
				vmr[ind1] = vmr[ind2] + area_r[b];
				vmg[ind1] = vmg[ind2] + area_g[b];
				vmb[ind1] = vmb[ind2] + area_b[b];
				m2[ind1] = m2[ind2] + area2[b];
			}
		}
	}
}

// Compute sum over a box of any given statistic
IW::Int64 CQuantizer::Vol(Box* cube, IW::Int64* mmt)
{
	return (mmt[INDEX(cube->r1, cube->g1, cube->b1)]
		- mmt[INDEX(cube->r1, cube->g1, cube->b0)]
		- mmt[INDEX(cube->r1, cube->g0, cube->b1)]
		+ mmt[INDEX(cube->r1, cube->g0, cube->b0)]
		- mmt[INDEX(cube->r0, cube->g1, cube->b1)]
		+ mmt[INDEX(cube->r0, cube->g1, cube->b0)]
		+ mmt[INDEX(cube->r0, cube->g0, cube->b1)]
		- mmt[INDEX(cube->r0, cube->g0, cube->b0)]);
}

// The next two routines allow a slightly more efficient calculation
// of Vol() for a proposed subbox of a given box.  The sum of Top()
// and Bottom() is the Vol() of a subbox split in the given direction
// and with the specified new upper bound.


// Compute part of Vol(cube, mmt) that doesn't depend on r1, g1, or b1
// (depending on dir)
IW::Int64 CQuantizer::Bottom(Box* cube, IW::UInt8 dir, IW::Int64* mmt)
{
	switch (dir)
	{
	case RED:
		return (-mmt[INDEX(cube->r0, cube->g1, cube->b1)]
			+ mmt[INDEX(cube->r0, cube->g1, cube->b0)]
			+ mmt[INDEX(cube->r0, cube->g0, cube->b1)]
			- mmt[INDEX(cube->r0, cube->g0, cube->b0)]);
		break;
	case GREEN:
		return (-mmt[INDEX(cube->r1, cube->g0, cube->b1)]
			+ mmt[INDEX(cube->r1, cube->g0, cube->b0)]
			+ mmt[INDEX(cube->r0, cube->g0, cube->b1)]
			- mmt[INDEX(cube->r0, cube->g0, cube->b0)]);
		break;
	case BLUE:
		return (-mmt[INDEX(cube->r1, cube->g1, cube->b0)]
			+ mmt[INDEX(cube->r1, cube->g0, cube->b0)]
			+ mmt[INDEX(cube->r0, cube->g1, cube->b0)]
			- mmt[INDEX(cube->r0, cube->g0, cube->b0)]);
		break;
	}

	return 0;
}


// Compute remainder of Vol(cube, mmt), substituting pos for
// r1, g1, or b1 (depending on dir)
IW::Int64 CQuantizer::Top(Box* cube, IW::UInt8 dir, int pos, IW::Int64* mmt)
{
	switch (dir)
	{
	case RED:
		return (mmt[INDEX(pos, cube->g1, cube->b1)]
			- mmt[INDEX(pos, cube->g1, cube->b0)]
			- mmt[INDEX(pos, cube->g0, cube->b1)]
			+ mmt[INDEX(pos, cube->g0, cube->b0)]);
		break;
	case GREEN:
		return (mmt[INDEX(cube->r1, pos, cube->b1)]
			- mmt[INDEX(cube->r1, pos, cube->b0)]
			- mmt[INDEX(cube->r0, pos, cube->b1)]
			+ mmt[INDEX(cube->r0, pos, cube->b0)]);
		break;
	case BLUE:
		return (mmt[INDEX(cube->r1, cube->g1, pos)]
			- mmt[INDEX(cube->r1, cube->g0, pos)]
			- mmt[INDEX(cube->r0, cube->g1, pos)]
			+ mmt[INDEX(cube->r0, cube->g0, pos)]);
		break;
	}

	return 0;
}

// Compute the weighted variance of a box 
// NB: as with the raw statistics, this is really the variance * ImageSize 
double CQuantizer::Var(Box* cube)
{
	double dr = static_cast<double>(Vol(cube, m_pnMR));
	double dg = static_cast<double>(Vol(cube, m_pnMG));
	double db = static_cast<double>(Vol(cube, m_pnMB));
	double xx = m_pGM2[INDEX(cube->r1, cube->g1, cube->b1)]
		- m_pGM2[INDEX(cube->r1, cube->g1, cube->b0)]
		- m_pGM2[INDEX(cube->r1, cube->g0, cube->b1)]
		+ m_pGM2[INDEX(cube->r1, cube->g0, cube->b0)]
		- m_pGM2[INDEX(cube->r0, cube->g1, cube->b1)]
		+ m_pGM2[INDEX(cube->r0, cube->g1, cube->b0)]
		+ m_pGM2[INDEX(cube->r0, cube->g0, cube->b1)]
		- m_pGM2[INDEX(cube->r0, cube->g0, cube->b0)];

	return (xx - (dr * dr + dg * dg + db * db) / static_cast<double>(Vol(cube, m_pnWT)));
}

// We want to minimize the sum of the variances of two subboxes.
// The sum(c^2) terms can be ignored since their sum over both subboxes
// is the same (the sum for the whole box) no matter where we split.
// The remaining terms have a minus sign in the variance formula,
// so we drop the minus sign and MAXIMIZE the sum of the two terms.
double CQuantizer::Maximize(Box* cube, IW::UInt8 dir, int first, int last, int* cut, IW::Int64 whole_r,
                           IW::Int64 whole_g, IW::Int64 whole_b, IW::Int64 whole_w)
{
	IW::Int64 half_r, half_g, half_b, half_w;
	int i;
	double temp;

	IW::Int64 base_r = Bottom(cube, dir, m_pnMR);
	IW::Int64 base_g = Bottom(cube, dir, m_pnMG);
	IW::Int64 base_b = Bottom(cube, dir, m_pnMB);
	IW::Int64 base_w = Bottom(cube, dir, m_pnWT);

	double max = 0.0;

	*cut = -1;

	for (i = first; i < last; i++)
	{
		half_r = base_r + Top(cube, dir, i, m_pnMR);
		half_g = base_g + Top(cube, dir, i, m_pnMG);
		half_b = base_b + Top(cube, dir, i, m_pnMB);
		half_w = base_w + Top(cube, dir, i, m_pnWT);

		// now half_x is sum over lower half of box, if split at i

		if (half_w == 0)
		{
			// subbox could be empty of pixels!
			continue; // never split into an empty box
		}
		temp = (static_cast<double>(half_r) * static_cast<double>(half_r) +
			static_cast<double>(half_g) * static_cast<double>(half_g) +
			static_cast<double>(half_b) * static_cast<double>(half_b)) / static_cast<double>(half_w);

		half_r = whole_r - half_r;
		half_g = whole_g - half_g;
		half_b = whole_b - half_b;
		half_w = whole_w - half_w;

		if (half_w == 0)
		{
			// subbox could be empty of pixels!
			continue; // never split into an empty box
		}
		temp += (static_cast<double>(half_r) * static_cast<double>(half_r) +
			static_cast<double>(half_g) * static_cast<double>(half_g) +
			static_cast<double>(half_b) * static_cast<double>(half_b)) / static_cast<double>(half_w);

		if (temp > max)
		{
			max = temp;
			*cut = i;
		}
	}

	return max;
}

bool CQuantizer::Cut(Box* set1, Box* set2)
{
	IW::UInt8 dir;
	int cutr, cutg, cutb;

	IW::Int64 whole_r = Vol(set1, m_pnMR);
	IW::Int64 whole_g = Vol(set1, m_pnMG);
	IW::Int64 whole_b = Vol(set1, m_pnMB);
	IW::Int64 whole_w = Vol(set1, m_pnWT);

	double maxr = Maximize(set1, RED, set1->r0 + 1, set1->r1, &cutr, whole_r, whole_g, whole_b, whole_w);
	double maxg = Maximize(set1, GREEN, set1->g0 + 1, set1->g1, &cutg, whole_r, whole_g, whole_b, whole_w);
	double maxb = Maximize(set1, BLUE, set1->b0 + 1, set1->b1, &cutb, whole_r, whole_g, whole_b, whole_w);

	if ((maxr >= maxg) && (maxr >= maxb))
	{
		dir = RED;

		if (cutr < 0)
		{
			return false; // can't split the box
		}
	}
	else if ((maxg >= maxr) && (maxg >= maxb))
	{
		dir = GREEN;
	}
	else
	{
		dir = BLUE;
	}

	set2->r1 = set1->r1;
	set2->g1 = set1->g1;
	set2->b1 = set1->b1;

	switch (dir)
	{
	case RED:
		set2->r0 = set1->r1 = cutr;
		set2->g0 = set1->g0;
		set2->b0 = set1->b0;
		break;

	case GREEN:
		set2->g0 = set1->g1 = cutg;
		set2->r0 = set1->r0;
		set2->b0 = set1->b0;
		break;

	case BLUE:
		set2->b0 = set1->b1 = cutb;
		set2->r0 = set1->r0;
		set2->g0 = set1->g0;
		break;
	}

	set1->vol = (set1->r1 - set1->r0) * (set1->g1 - set1->g0) * (set1->b1 - set1->b0);
	set2->vol = (set2->r1 - set2->r0) * (set2->g1 - set2->g0) * (set2->b1 - set2->b0);

	return true;
}


void CQuantizer::Mark(Box* cube, int label, IW::UInt8* tag)
{
	for (int r = cube->r0 + 1; r <= cube->r1; r++)
	{
		for (int g = cube->g0 + 1; g <= cube->g1; g++)
		{
			for (int b = cube->b0 + 1; b <= cube->b1; b++)
			{
				// Quantize assigns fewer than MAXCOLOR (256) palette labels.
				tag[INDEX(r, g, b)] = static_cast<IW::UInt8>(label);
			}
		}
	}
}


// Wu Quantization algorithm
bool IW::Quantize(const Image& imageIn, Image& imageOut, IStatus* pStatus)
{
	for (auto pageIn = imageIn.Pages.begin(); pageIn != imageIn.Pages.end(); ++pageIn)
	{
		CQuantizer q(*pageIn);

		if (!q.Quantize(imageOut))
			return false;

		if (pStatus->QueryCancel())
			break;
	}

	imageOut.Normalize();

	return true;
}


bool CQuantizer::Quantize(IW::Image& imageOut)
{
	int PalSize = 254; // Color look-up table size
	Box cube[MAXCOLOR];
	int next;
	int i;
	IW::Int64 weight;
	int k;
	double vv[MAXCOLOR], temp;

	// Compute 3D histogram
	Hist3D(m_pnWT, m_pnMR, m_pnMG, m_pnMB, m_pGM2);

	// Compute moments

	M3D(m_pnWT, m_pnMR, m_pnMG, m_pnMB, m_pGM2);

	cube[0].r0 = cube[0].g0 = cube[0].b0 = 0;
	cube[0].r1 = cube[0].g1 = cube[0].b1 = 32;
	next = 0;

	for (i = 1; i < PalSize; i++)
	{
		if (Cut(&cube[next], &cube[i]))
		{
			// volume test ensures we won't try to cut one-cell box
			vv[next] = (cube[next].vol > 1) ? Var(&cube[next]) : 0;
			vv[i] = (cube[i].vol > 1) ? Var(&cube[i]) : 0;
		}
		else
		{
			vv[next] = 0.0; // don't try to split this box again
			i--; // didn't create box i
		}

		next = 0;
		temp = vv[0];

		for (k = 1; k <= i; k++)
		{
			if (vv[k] > temp)
			{
				temp = vv[k];
				next = k;
			}
		}

		if (temp <= 0.0)
		{
			PalSize = i + 1;

			// Error: "Only got 'PalSize' boxes"

			break;
		}
	}

	// Partition done

	// the space for array m_pGM2 can be IW::Freed now

	IW::Free(m_pGM2);

	m_pGM2 = nullptr;

	// Allocate a new pImage
	IW::Page pageOut = imageOut.CreatePage(m_page.GetPageRect(), IW::PixelFormat::PF8);

	// create an optimized palette	
	auto new_pal = (RGBQUAD*)pageOut.GetPalette();

	IW::CBuffer<IW::UInt8> tag(33 * 33 * 33);

	for (k = 0; k < PalSize; k++)
	{
		Mark(&cube[k], k, tag);
		weight = Vol(&cube[k], m_pnWT);

		if (weight)
		{
			new_pal[k].rgbRed = static_cast<IW::UInt8>(Vol(&cube[k], m_pnMR) / weight);
			new_pal[k].rgbGreen = static_cast<IW::UInt8>(Vol(&cube[k], m_pnMG) / weight);
			new_pal[k].rgbBlue = static_cast<IW::UInt8>(Vol(&cube[k], m_pnMB) / weight);
		}
		else
		{
			// Error: bogus box 'k'

			new_pal[k].rgbRed = new_pal[k].rgbGreen = new_pal[k].rgbBlue = 0;
		}

		// The fourth byte of a palette entry is read as alpha downstream.
			new_pal[k].rgbReserved = 0xFF;
	}


	for (int y = 0; y < m_nHeight; y++)
	{
		IW::UInt8* new_bits = pageOut.GetBitmapLine(y);

		for (int x = 0; x < m_nWidth; x++)
		{
			int n = m_pnQadd[static_cast<size_t>(y) * m_nWidth + x];
			new_bits[x] = tag[n];
		}
	}

	// output 'new_pal' as color look-up table contents,
	// 'new_bits' as the quantized image (array of table addresses).

	return true;
}
