// Load.cpp: implementation of the CLoad class.
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "artmate.h"
#include "Load.h"

#ifdef _DEBUG
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CLoad::CLoad()
{
	m_nBufferSize = 0;
	m_pBuffer = nullptr;
}

CLoad::~CLoad()
{
	if (m_pBuffer)
		delete[] m_pBuffer;
}

LPBYTE CLoad::GetBuffer(int nSize)
{
	if (m_nBufferSize < nSize)
	{
		if (m_pBuffer)
			delete[] m_pBuffer;

		m_pBuffer = new BYTE[nSize];

		m_nBufferSize = nSize;
	}

	return m_pBuffer;
}

void CLoad::SwapRB(CDib* pDib)
{
	BYTE* pSrc = pDib->GetBitmap(0);
	int nSrcSkip = pDib->m_nStorageWidth;

	int h = pDib->Height();
	int w = pDib->Width();

	const int nBytes = pDib->Bpp() / 8;
	ASSERT(nBytes == 3 || nBytes == 4);
	if (nBytes != 3 && nBytes != 4)
		return;

	// Rows run backwards: this is a bottom-up DIB.
	for (int y = 0; y < h; y++)
	{
		BYTE* p = pSrc;

		for (int x = 0; x < w; x++, p += nBytes)
		{
			const BYTE b = p[0];
			p[0] = p[2];
			p[2] = b;
		}

		pSrc -= nSrcSkip;
	}
}
