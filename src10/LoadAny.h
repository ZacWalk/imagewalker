// LoadAny.h: interface for the CLoadAny class.
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_LOADANY_H__18A4AB61_00B2_11D3_A6DF_90064CC10000__INCLUDED_)
#define AFX_LOADANY_H__18A4AB61_00B2_11D3_A6DF_90064CC10000__INCLUDED_

class CLoadJpg;
class CLoadTiff;
class CLoadGif;
class CLoadPcx;
class CLoadPng;
class CLoadBmp;
class CDib;
class CStatus;



#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

class CLoadAny  
{
protected:
	CLoadJpg *m_pLoadJpg;
	CLoadTiff *m_pLoadTiff;
	CLoadGif *m_pLoadGif;
	CLoadPcx *m_pLoadPcx;
	CLoadPng *m_pLoadPng;
   CLoadBmp *m_pLoadBmp;


public:
	BOOL Load(CDib *pDib, int nID, CStatus *pStatus, BOOL bThumb);
	BOOL Load(CDib *pDib, LPCSTR szFileName, CStatus *pStatus, BOOL bThumb);
	BOOL Load(CDib *pDib, LPBYTE pByte, DWORD nSize, CStatus *pStatus, BOOL bThumb);

	CLoadAny();
	virtual ~CLoadAny();

};

#endif // !defined(AFX_LOADANY_H__18A4AB61_00B2_11D3_A6DF_90064CC10000__INCLUDED_)
