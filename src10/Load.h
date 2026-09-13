// Load.h: interface for the CLoad class.
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_LOAD_H__63018B61_FA7C_11D2_A6DF_A00B4CC10000__INCLUDED_)
#define AFX_LOAD_H__63018B61_FA7C_11D2_A6DF_A00B4CC10000__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

class CDib;
class CStatus;




class CLoad  
{
protected:

	void SwapRB(CDib *pDib);
	LPBYTE GetBuffer(int nSize);

	int m_nBufferSize;
	LPBYTE m_pBuffer;
   HWND m_hwndStatus;

public:

   virtual BOOL Load(CDib *pDib, LPBYTE pByte, DWORD nSize, CStatus *pStatus, BOOL bThumb) = 0;

	CLoad();
	virtual ~CLoad();


private:

};

#endif // !defined(AFX_LOAD_H__63018B61_FA7C_11D2_A6DF_A00B4CC10000__INCLUDED_)
