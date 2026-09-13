// LoadPcx.h: interface for the CLoadPcx class.
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_LOADPCX_H__1C956301_0322_11D3_A6DF_E0164CC10000__INCLUDED_)
#define AFX_LOADPCX_H__1C956301_0322_11D3_A6DF_E0164CC10000__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include "Load.h"

class CLoadPcx : public CLoad  
{
public:
	BOOL ScanLine(LPBYTE pLine, LPBYTE & pByte, LPCBYTE pEnd);
	virtual BOOL Load(CDib *pDib, LPBYTE pByte, DWORD nSize, CStatus *pStatus, BOOL bThumb);
	CLoadPcx();
	virtual ~CLoadPcx();


protected:

	UINT m_nRepCount;
	BYTE m_nRepByte;
	UINT m_nSrcWidth;
	UINT m_nSrcHeight;
	
};

#endif // !defined(AFX_LOADPCX_H__1C956301_0322_11D3_A6DF_E0164CC10000__INCLUDED_)
