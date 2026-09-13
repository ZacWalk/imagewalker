// LoadJpg.h: interface for the CLoadJpg class.
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_LOADJPG_H__63018B62_FA7C_11D2_A6DF_A00B4CC10000__INCLUDED_)
#define AFX_LOADJPG_H__63018B62_FA7C_11D2_A6DF_A00B4CC10000__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include "Load.h"

#include <setjmp.h>

extern "C" {
#include "jpeglib.h"
}

// libjpeg calls error_exit from C frames, which a C++ throw may not unwind. The
// jump target lives with the error manager so the callback can find it from
// cinfo->err, and per decoder instance so two threads do not share one.
struct ima_error_mgr
{
	struct jpeg_error_mgr pub;
	jmp_buf jmpbuf;

	// jpeg_create_decompress and jpeg_destroy_decompress run outside any
	// setjmp frame, so the callback has to know when the target is live.
	BOOL bCanJump;
};

class CLoadJpg : public CLoad  
{
protected:

	struct ima_error_mgr jerr;
	struct jpeg_decompress_struct in;
	struct jpeg_source_mgr src;

	BOOL LoadImage(CDib *pDib, LPBYTE pByte, DWORD nSize, CStatus *pStatus, BOOL bThumb);

public:
	virtual BOOL Load(CDib *pDib, LPBYTE pByte, DWORD nSize, CStatus *pStatus, BOOL bThumb);

	
	CLoadJpg();
	virtual ~CLoadJpg();


};

#endif // !defined(AFX_LOADJPG_H__63018B62_FA7C_11D2_A6DF_A00B4CC10000__INCLUDED_)
