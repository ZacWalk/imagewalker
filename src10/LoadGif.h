// LoadGif.h: interface for the CLoadGif class.
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_LOADGIF_H__1C956303_0322_11D3_A6DF_E0164CC10000__INCLUDED_)
#define AFX_LOADGIF_H__1C956303_0322_11D3_A6DF_E0164CC10000__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include "Load.h"

class CLoadGif : public CLoad  
{
public:
	void Load(CDib *pDib);
	void ScanLine(LPBYTE p);
	virtual BOOL Load(CDib *pDib, LPBYTE pByte, DWORD nSize, CStatus *pStatus, BOOL bThumb);
	CLoadGif();
	virtual ~CLoadGif();

	CDib *m_pDib;
	BYTE *m_pRgb;

	UINT m_nSrcHeight;
	UINT m_nSrcWidth;
	UINT m_uCode;			/* Value returned by ReadCode */
	BOOL m_bInterlace;
	UINT m_nColorMapSize;
   int m_nTransparentPixel;

   void IgnoreSubBlocks();

protected:

	static char *id87;
	static char *id89;

	
	void PlainTextExtension();
	void AspectExtension();
	void CommentExtension();
	void ApplicationExtension();

	BOOL ReadImage(BOOL bThumb);

   inline UINT ReadCode()
   {
	   // Fetch the next code from the raster data stream.  The codes can be
	   // any length from 3 to 12 bits, packed into 8-bit bytes, so we have to
	   // maintain our location in the Raster array as a BIT Offset.  We compute
	   // the byte Offset into the raster array by dividing this by 8, pick up
	   // three bytes, compute the bit Offset into our 24-bit chunk, shift to
	   // bring the desired code to the bottom, then mask it off and return it. 


     UINT RawCode = 0;
     UINT ByteOffset = m_uBitOffset / 8;
     UINT BytesToLoad = (m_uCodeSize + (m_uBitOffset%8))/8 + 1;

     for(UINT i = 0; i < BytesToLoad; i++)
     {
	     if (m_nBlockOffset + m_nBlockSize <= ByteOffset)
	     {
		   m_nBlockOffset += m_nBlockSize;		
		   m_nBlockSize = *m_pPointer++;
		   m_Block = Read(m_nBlockSize);
	     }

		   RawCode += m_Block[ByteOffset - m_nBlockOffset] << (i * 8);
		   ByteOffset += 1;
     }

     RawCode >>= (m_uBitOffset % 8);
     m_uBitOffset += m_uCodeSize;

     return(RawCode & m_uReadMask);
   }


	// An output array used by the decompressor 
	int m_OutCode[4097];

	// The hash table used by the decompressor 
	int m_Prefix[4096];
	int m_Suffix[4096];

	// Buffers involved in image/color loading

	BYTE *m_Block;
	UINT m_nBlockOffset;
	BYTE m_nBlockSize;

	UINT m_uBitMask;	// AND mask for data size
	UINT m_uReadMask;	// Code AND mask for current code size

	BYTE *m_pPointer;
	BYTE *m_pEndData;

	UINT m_uBitOffset;		/* Bit Offset of next code */
    UINT  m_uOutCount;		/* Decompressor output 'stack count' */
    UINT m_uCodeSize;			/* Code size, read from GIF header */
    UINT m_uInitCodeSize;		/* Starting code size, used during Clear */
    UINT m_MaxCode;			/* limiting value for current code size */
    UINT m_uClearCode;			/* GIF clear code */
    UINT m_uEOFCode;			/* GIF end-of-information code */
    UINT m_uCurCode, m_uOldCode, m_uInCode;	/* Decompressor variables */
    UINT m_uFirstFree;			/* First free code, generated per GIF spec */
    UINT m_uFreeCode;			/* Decompressor,next free slot in hash table */
    UINT m_uFinChar;			/* Decompressor variable */



	BOOL LoadPalette(RGBQUAD *pRGB, int nSize, int nBitSize = 24, BOOL bSwapRB = FALSE);

	BYTE *Read(unsigned nSize)
	{
		// Subtractive, so an underflowed nSize cannot wrap the pointer past
		// the end and come back looking small.
		if (m_pPointer < m_pEndData &&
		    nSize <= static_cast<size_t>(m_pEndData - m_pPointer))
		{
			BYTE *p = m_pPointer;
			m_pPointer += nSize;
			return p;
		}

		IW::Throw("bad gif file");
		return NULL;
	}

};

#endif // !defined(AFX_LOADGIF_H__1C956303_0322_11D3_A6DF_E0164CC10000__INCLUDED_)
