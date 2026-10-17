// LoadPng.h: interface for the CLoadPng class.
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_LOADPNG_H__1C956306_0322_11D3_A6DF_E0164CC10000__INCLUDED_)
#define AFX_LOADPNG_H__1C956306_0322_11D3_A6DF_E0164CC10000__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include "Load.h"

extern "C" {
//#pragma include_alias( "zlib.h", "../zlib/zlib.h" )
//#pragma include_alias( "png.h", "../png/png.h" )

#include "png.h"
}


class CLoadPng : public CLoad  
{
   png_structp png_ptr;
   png_infop info_ptr;

   static void read_data(png_struct *png_ptr, png_byte *data, png_size_t length);
   static void user_error_fn(png_structp png_ptr, png_const_charp error_msg);
   static void user_warning_fn(png_structp png_ptr, png_const_charp warning_msg);


public:
	BOOL Load(CDib *pDib, UINT pass);
	virtual BOOL Load(CDib *pDib, LPBYTE pByte, DWORD nSize, CStatus *pStatus, BOOL bThumb);
	static BOOL IsPng(LPBYTE pByte);
	CLoadPng();
	virtual ~CLoadPng();

};

#endif // !defined(AFX_LOADPNG_H__1C956306_0322_11D3_A6DF_E0164CC10000__INCLUDED_)
