// LoadBmp.h: interface for the CLoadBmp class.
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_LOADBMP_H__1C956302_0322_11D3_A6DF_E0164CC10000__INCLUDED_)
#define AFX_LOADBMP_H__1C956302_0322_11D3_A6DF_E0164CC10000__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include "Load.h"

class CLoadBmp : public CLoad  
{
public:
	void CopyPalette(RGBQUAD *pDst, RGBQUAD *pSrc, UINT nColorEntries);
	virtual BOOL  Load(CDib *pDib, LPBYTE pByte, DWORD nSize, CStatus *pStatus, BOOL bThumb);
	CLoadBmp();
	virtual ~CLoadBmp();

};

#endif // !defined(AFX_LOADBMP_H__1C956302_0322_11D3_A6DF_E0164CC10000__INCLUDED_)
