// LoadTiff.h: interface for the CLoadTiff class.
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_LOADTIFF_H__862C87E4_FE80_11D2_A6DF_E0F24BC10000__INCLUDED_)
#define AFX_LOADTIFF_H__862C87E4_FE80_11D2_A6DF_E0F24BC10000__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include "Load.h"

#include "tiffio.h" 

#define HDIB HANDLE
#define IS_WIN30_DIB(lpbi)  ((*(LPDWORD)(lpbi)) == sizeof(BITMAPINFOHEADER))
#define CVT(x)      (((x) * 255L) / ((1L<<16)-1))


class CLoadTiff : public CLoad  
{
protected:

	static void MyTiffWarningHandler(const char* module, const char* fmt, va_list ap);
	static void MyTiffErrorHandler(const char* module, const char* fmt, va_list ap);
	static void TiffDiagnostic(LPCTSTR szKind, const char* module, const char* fmt, va_list ap);
	static int Checkcmap(int n, uint16* r, uint16* g, uint16* b);


	TIFF          *tif;

	uint16  m_BitsPerSample;
	uint16	m_SamplePerPixel;
	int32	m_LineSize;
	int16	m_PhotometricInterpretation;
	uint32	m_row;
	LPBYTE	m_pBits;
	UINT	m_nBitsPerPixel;
	UINT	m_nLineNum;
	UINT	m_PlanarConfig;

	static tsize_t MyTiffReadProc(thandle_t fd, tdata_t buf, tsize_t size);
	static tsize_t MyTiffWriteProc(thandle_t fd, tdata_t buf, tsize_t size);
	static toff_t  MyTiffSeekProc(thandle_t fd, toff_t off, int whence);
	static int MyTiffCloseProc(thandle_t fd);
	static toff_t MyTiffSizeProc(thandle_t fd);
	static int MyTiffDummyMapProc(thandle_t fd, tdata_t* pbase, toff_t* psize);
	static void MyTiffDummyUnmapProc(thandle_t fd, tdata_t base, toff_t size);

	LPBYTE  m_pData;
	UINT    m_nPos;
	UINT    m_nSize;

public:
	virtual BOOL  Load(CDib *pDib, LPBYTE pByte, DWORD nSize, CStatus *pStatus, BOOL bThumb);
	CLoadTiff();
	virtual ~CLoadTiff();
};

#endif // !defined(AFX_LOADTIFF_H__862C87E4_FE80_11D2_A6DF_E0F24BC10000__INCLUDED_)
