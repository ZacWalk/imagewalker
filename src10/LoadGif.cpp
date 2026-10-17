// LoadGif.cpp: implementation of the CLoadGif class.
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "artmate.h"
#include "LoadGif.h"
#include "dibthumb.h"

#ifdef _DEBUG
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

#define EXTENSION     0x21
#define IMAGESEP      0x2c
#define TRAILER       0x3b
#define INTERLACEMASK 0x40
#define COLORMAPMASK  0x80

char* CLoadGif::id87 = "GIF87a";
char* CLoadGif::id89 = "GIF89a";

#pragma pack(1)
using GifHeader = struct
{
	char szSignature[3];
	char szVersion[3];

	// Logical Screen Descriptor Stuff

	unsigned short uLogicalWidth;
	unsigned short uLogicalHeight;
	BYTE bPackedFields;
	BYTE bBackGroundColour;
	BYTE bAspectRatio;
};

using GifImageHeader = struct
{
	unsigned short LeftOfs, TopOfs; /* image offset */
	unsigned short Width, Height; /* image dimensions */
	BYTE Misc; /* miscellaneous bits (Interlace, local cmap)*/
};
#pragma pack()


//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CLoadGif::CLoadGif()
{
}

CLoadGif::~CLoadGif()
{
}


BOOL CLoadGif::LoadPalette(RGBQUAD* pRGB, int nSize, int nBitSize, BOOL bSwapRB)
{
	if (nBitSize == 32)
	{
		for (int i = 0; i < nSize; i++)
		{
			pRGB[i].rgbRed = *m_pPointer++;
			pRGB[i].rgbGreen = *m_pPointer++;
			pRGB[i].rgbBlue = *m_pPointer++;
			pRGB[i].rgbReserved = *m_pPointer++;
		}
	}
	else if (nBitSize == 24)
	{
		for (int i = 0; i < nSize; i++)
		{
			pRGB[i].rgbRed = *m_pPointer++;
			pRGB[i].rgbGreen = *m_pPointer++;
			pRGB[i].rgbBlue = *m_pPointer++;
			pRGB[i].rgbReserved = 0;
		}
	}
	else if (nBitSize == 16)
	{
		for (int i = 0; i < nSize; i++)
		{
			unsigned short nColor = *((unsigned short*)Read(2));

			pRGB[i].rgbRed = static_cast<BYTE>(((nColor >> 10) & 31) << 3);
			pRGB[i].rgbGreen = static_cast<BYTE>(((nColor >> 5) & 31) << 3);
			pRGB[i].rgbBlue = static_cast<BYTE>((nColor & 31) << 3);
		}
	}
	else
	{
		ASSERT(0);
		TRACE("Unsupported Palette Size\n");
	}

	if (bSwapRB)
	{
		for (int i = 0; i < nSize; i++)
		{
			BYTE r = pRGB[i].rgbRed;
			pRGB[i].rgbRed = pRGB[i].rgbBlue;
			pRGB[i].rgbBlue = r;
		}
	}

	return TRUE;
}

class CIteratorGif
{
public:
	CIteratorGif(CLoadGif* pIn)
	{
		m_pIn = pIn;
		m_pLineIn = new BYTE[Width()];
		m_pRGB = pIn->m_pRgb;
	}

	~CIteratorGif()
	{
		delete[] m_pLineIn;
	}

	LPBYTE m_pRGB;
	LPBYTE m_pLineIn;
	CLoadGif* m_pIn;

	UINT Height() { return m_pIn->m_nSrcHeight; };
	UINT Width() { return m_pIn->m_nSrcWidth; };

	LPBYTE ScanLine()
	{
		m_pIn->ScanLine(m_pLineIn);

		return m_pLineIn;
	}

	void GetLine(UINT& nWidthSrc, UINT& nWidthDst,
	             LPRGBSUM pSum, LPRGBSUM pSumEnd,
	             LPBYTE& pBytes)
	{
		UINT x = 0;

		while (pSum < pSumEnd)
		{
			x += nWidthSrc;

			while (x >= nWidthDst)
			{
				int n = *pBytes++;
				LPBYTE p = m_pRGB + n * 3;

				pSum->r += p[2];
				pSum->g += p[1];
				pSum->b += p[0];

				if (m_pIn->m_nTransparentPixel == n)
				{
					pSum->a += 0;
				}
				else
				{
					pSum->a += 0xff;
				}

				pSum->s++;


				x -= nWidthDst;
			}

			pSum++;
		}
	};
};


BOOL CLoadGif::ReadImage(BOOL bThumb)
{
	// initialize variables 
	m_uBitOffset = m_uOutCount = 0;

	auto pHeader = (GifImageHeader*)Read(sizeof(GifImageHeader));

	// read in values from the image descriptor 


	m_nSrcWidth = pHeader->Width;
	m_nSrcHeight = pHeader->Height;


	if (pHeader->Misc & 0x80)
	{
		m_nColorMapSize = (1 << ((pHeader->Misc & 7) + 1));
		m_pRgb = Read(m_nColorMapSize * 3);
	}


	m_bInterlace = ((pHeader->Misc & INTERLACEMASK) ? TRUE : FALSE);


	/* Start reading the raster data. First we get the initial code size
	 * and compute decompressor constant values, based on this code size.
	 */

	m_uCodeSize = *m_pPointer++;

	m_uClearCode = (1 << m_uCodeSize);
	m_uEOFCode = m_uClearCode + 1;
	m_uFreeCode = m_uFirstFree = m_uClearCode + 2;

	/* The GIF spec has it that the code size is the code size used to
	 * compute the above values is the code size given in the file, but the
	 * code size used in compression/decompression is the code size given in
	 * the file plus one. (thus the ++).
	 */

	m_uCodeSize++;
	m_uInitCodeSize = m_uCodeSize;
	m_MaxCode = (1 << m_uCodeSize);
	m_uReadMask = m_MaxCode - 1;

	// Init block buffer


	m_nBlockOffset = 0;
	m_nBlockSize = 0;


	/* Decompress the file, continuing until you see the GIF EOF code.
	 * One obvious enhancement is to add checking for corrupt files here.
	 */

	m_uCode = ReadCode();

	if (!bThumb ||
		(IMAGE_X >= m_nSrcWidth && IMAGE_Y >= m_nSrcHeight))
	{
		Load(m_pDib);
	}
	else
	{
		UINT nDstWidth = (m_nSrcWidth * IMAGE_Y) / m_nSrcHeight;
		UINT nDstHeight = (m_nSrcHeight * IMAGE_X) / m_nSrcWidth;

		if (nDstWidth > IMAGE_X)
		{
			nDstWidth = IMAGE_X;
		}
		else
		{
			nDstHeight = IMAGE_Y;
		}

		if (!m_bInterlace)
		{
			CIteratorGif gif(this);
			CDibThumb<CIteratorGif> Scale;
			Scale.Scale(*m_pDib, gif);
		}
		else
		{
			CDib dib;

			Load(&dib);

			CIteratorDib<CDibIterator8> d(&dib);
			CDibThumb<CIteratorDib<CDibIterator8>> Scale;
			Scale.Scale(*m_pDib, d);
		}
	}

	return TRUE;
}


void CLoadGif::AspectExtension()
{
	int sbsize, blocksize, aspnum, aspden;
	double normaspect;

	blocksize = *m_pPointer++;
	if (blocksize == 2)
	{
		aspnum = *m_pPointer++;
		aspden = *m_pPointer++;

		if (aspden > 0 && aspnum > 0)
		{
			normaspect = static_cast<double>(aspnum) / static_cast<double>(aspden);
		}
		else
		{
			normaspect = 1.0;
			aspnum = aspden = 1;
		}
	}
	else
	{
		Read(blocksize);
	}

	while ((sbsize = *m_pPointer++) > 0)
	{
		/* eat any following data subblocks */
		Read(sbsize);
	}
}


void CLoadGif::CommentExtension()
{
	if (!m_pDib->m_strInfo.IsEmpty())
		m_pDib->m_strInfo += "\n";

	for (int nLineLength = *m_pPointer++; nLineLength != 0; nLineLength = *m_pPointer++)
	{
		int nCurrentCommentLength = m_pDib->m_strInfo.GetLength();
		const int nTotal = nCurrentCommentLength + nLineLength;

		LPSTR pStr = m_pDib->m_strInfo.GetBuffer(nTotal);
		CopyMemory(pStr + nCurrentCommentLength, Read(nLineLength), nLineLength);
		// The copied bytes are not NUL terminated, so ReleaseBuffer needs the length
		m_pDib->m_strInfo.ReleaseBuffer(nTotal);
	}
}

void CLoadGif::PlainTextExtension()
{
	int sbsize;
	int tgLeft, tgTop, tgWidth, tgHeight, cWidth, cHeight, fg, bg;


	sbsize = *m_pPointer++;
	tgLeft = *m_pPointer++;
	tgLeft += (*m_pPointer++) << 8;
	tgTop = *m_pPointer++;
	tgTop += (*m_pPointer++) << 8;
	tgWidth = *m_pPointer++;
	tgWidth += (*m_pPointer++) << 8;
	tgHeight = *m_pPointer++;
	tgHeight += (*m_pPointer++) << 8;
	cWidth = *m_pPointer++;
	cHeight = *m_pPointer++;
	fg = *m_pPointer++;
	bg = *m_pPointer++;

	if (sbsize < 12)
		IW::Throw("bad gif file");

	Read(sbsize - 12);

	IgnoreSubBlocks();
}

void CLoadGif::ApplicationExtension()
{
	IgnoreSubBlocks();
}

void CLoadGif::IgnoreSubBlocks()
{
	int sbsize;

	/* read (and ignore) data sub-blocks */
	do
	{
		sbsize = *m_pPointer++;
		Read(sbsize);
	}
	while (sbsize);
}


BOOL CLoadGif::Load(CDib* pDib, LPBYTE pByte, DWORD nSize, CStatus* pStatus, BOOL bThumb)
{
	try
		{
			static BYTE EGApalette[] = {
				0, 0, 0, 0, 0, 128, 0, 128, 0, 0, 128, 128,
				128, 0, 0, 128, 0, 128, 128, 128, 0, 200, 200, 200,
				100, 100, 100, 100, 100, 255, 100, 255, 100, 100, 255, 255,
				255, 100, 100, 255, 100, 255, 255, 255, 100, 255, 255, 255
			};

			m_pDib = pDib;
			m_pRgb = EGApalette;
			m_nColorMapSize = 16;
			m_nTransparentPixel = -1;

			m_pPointer = pByte;
			m_pEndData = pByte + nSize;


			auto pHeader = (GifHeader*)Read(sizeof(GifHeader));

			BOOL bGotImage = FALSE;
			BOOL bGif89;

			/* Get variables from the GIF screen descriptor */

			if (strncmp(pHeader->szSignature, id87, 6) == 0)
				bGif89 = FALSE;
			else if (strncmp(pHeader->szSignature, id89, 6) == 0)
				bGif89 = TRUE;
			else
			{
				TRACE("not a GIF file\n");
				return FALSE;
			}

			// Read in global colormap. 

			if (pHeader->bPackedFields & COLORMAPMASK)
			{
				m_nColorMapSize = 1 << ((pHeader->bPackedFields & 7) + 1);
				m_uBitMask = m_nColorMapSize - 1;
				m_pRgb = Read(m_nColorMapSize * 3);
			}
			else
			{
				// No global table: m_pRgb is still the 16-entry EGA fallback, so the
				// size must stay 16 or the palette loop over-reads it.
				m_uBitMask = m_nColorMapSize - 1;
			}


			/* possible things at this point are:
			*   an application extension block
			*   a comment extension block
			*   an (optional) graphic control extension block
			*       followed by either an image
			*	   or a plaintext extension
			*/

			BYTE block = *m_pPointer++;

			while (TRUE)
			{
				if (block == EXTENSION)
				{
					/* parse extension blocks */

					/* read extension block */
					switch (*m_pPointer++)
					{
					case 'R':
						/* GIF87 aspect extension */
						AspectExtension();
						break;
					case 0xFE:
						/* Comment Extension */
						CommentExtension();
						break;
					case 0x01:
						/* PlainText Extension */
						PlainTextExtension();
						break;
					case 0xFF:
						/* Application Extension */
						ApplicationExtension();
						break;
					case 0xF9:
						{
							/* Graphic Control Extension */
							int sbsize = *m_pPointer++;
							LPBYTE p = Read(sbsize);

							if (*p & 0x1)
								m_nTransparentPixel = *(p + 3);

							//disposal_method = (((*p) >> 2) & 0x7);
							//delay_time = ((short*)(p + 1)) * 10;

							IgnoreSubBlocks();
						}
						break;
					default: // Unknown
						TRACE("Unknown extension in GIF file.  Ignored.\n");
						IgnoreSubBlocks();
					}
				}
				else if (block == IMAGESEP)
				{
					if (bGotImage)
					{
						/* just skip over remaining images */

						/* skip image header */
						Read(8);
						int misc = *m_pPointer++; /* misc. bits */

						if (misc & 0x80)
						{
							/* image has local colormap.  skip it */
							Read((1 << ((misc & 7) + 1)) * 3);
						}

						*m_pPointer++; /* minimum code size */

						/* skip image data sub-blocks */
						IgnoreSubBlocks();
					}
					else
					{
						bGotImage = ReadImage(bThumb);
					}
				}
				else if (block == TRAILER)
				{
					/* stop reading blocks */
					break;
				}
				else
				{
					/* unknown block type */

					/* don't mention bad block if file was trunc'd, as it's all bogus */
					//if ((dataptr - origptr) < filesize) 
					{
						TRACE("Unknown block type (0x%02x)\n", block);
					}
					break;
				}

				block = *m_pPointer++;
			}

			// Gif info message
			CString str(m_pDib->m_strInfo);

			m_pDib->m_strInfo.Format("%dx%dx%d GIF%s %sInterlaced%s%s",
			                         m_nSrcWidth, m_nSrcHeight, 8,
			                         (bGif89) ? "89" : "87",
			                         m_bInterlace ? "" : "Non-",
			                         (m_pDib->m_strInfo.IsEmpty()) ? "" : "\n", static_cast<LPCSTR>(str));
		}
	catch (...)
		{
			if (!m_pDib->m_strInfo.IsEmpty())
				m_pDib->m_strInfo += "\n";

			m_pDib->m_strInfo += "Error In Gif File!";
			return FALSE;
		}


	return m_pDib->IsOpen();
}

void CLoadGif::ScanLine(LPBYTE pLine)
{
	UINT x = 0;

	while (x < m_nSrcWidth)
	{
		if (m_uOutCount == 0)
		{
			// Clear code sets everything back to its initial value, then reads the
			// immediately subsequent code as uncompressed data.

			if (m_uCode == m_uClearCode)
			{
				m_uCodeSize = m_uInitCodeSize;
				m_MaxCode = (1 << m_uCodeSize);
				m_uReadMask = m_MaxCode - 1;
				m_uFreeCode = m_uFirstFree;
				m_uCode = ReadCode();
				m_uCurCode = m_uOldCode = m_uCode;
				m_uFinChar = m_uCurCode & m_uBitMask;

				// GIF's literal mask is at most eight bits.
				pLine[x++] = static_cast<BYTE>(m_uFinChar);

				m_uCode = ReadCode();
			}
			else
			{
				// If not a clear code, must be data: save same as m_uCurCode and m_uInCode 
				// if we're at m_MaxCode and didn't get a clear, stop loading 

				if (m_uFreeCode >= 4096)
				{
					IW::Throw("bad gif file");
				}

				m_uCurCode = m_uInCode = m_uCode;

				// If greater or equal to m_uFreeCode, not in the hash table yet;
				// repeat the last character decoded
				//

				if (m_uCurCode >= m_uFreeCode)
				{
					if ((m_uCurCode = m_uOldCode) > 4096)
					{
						IW::Throw("bad gif file");
					}

					m_OutCode[m_uOutCount++] = m_uFinChar;
				}

				// Unless this code is raw data, pursue the chain pointed to by m_uCurCode
				// through the hash table to its end; each code in the chain puts its
				// associated output code on the output queue.

				while (m_uCurCode > m_uBitMask)
				{
					if (m_uOutCount > 4096 || m_uCurCode > 4096)
					{
						IW::Throw("bad gif file");
					}

					m_OutCode[m_uOutCount++] = m_Suffix[m_uCurCode];
					m_uCurCode = m_Prefix[m_uCurCode];
				}

				if (m_uOutCount > 4096)
				{
					IW::Throw("bad gif file");
				}

				/* The last code in the chain is treated as raw data. */

				m_uFinChar = m_uCurCode & m_uBitMask;
				m_OutCode[m_uOutCount++] = m_uFinChar;

				/* Now we put the data out to the Output routine.
				* It's been stacked LIFO, so deal with it that way...
				*/
			}
		}

		if (m_uOutCount > 0)
		{
			m_uOutCount -= 1;
			pLine[x++] = static_cast<BYTE>(m_OutCode[m_uOutCount]);

			if (m_uOutCount == 0)
			{
				/* Build the hash table on-the-fly. No table is stored in the file. */

				m_Prefix[m_uFreeCode] = m_uOldCode;
				m_Suffix[m_uFreeCode] = m_uFinChar;
				m_uOldCode = m_uInCode;

				/* Point to the next slot in the table.  If we exceed the current
				* m_MaxCode value, increment the code size unless it's already 12.  If it
				* is, do nothing: the next code decompressed better be CLEAR
				*/

				if (++m_uFreeCode >= m_MaxCode)
				{
					if (m_uCodeSize < 12)
					{
						m_uCodeSize++;
						m_MaxCode *= 2;
						m_uReadMask = (1 << m_uCodeSize) - 1;
					}
				}

				m_uCode = ReadCode();
			}
		}
	}
}


void CLoadGif::Load(CDib* pDib)
{
	static BYTE InterlaceSteps[4] = {8, 8, 4, 2};
	static BYTE InterlaceReset[4] = {4, 2, 1, 0};

	UINT y = 0;

	if (pDib->Create(m_nSrcWidth, m_nSrcHeight, 8, FALSE))
	{
		LPBYTE p;

		if (m_bInterlace)
		{
			UINT uPass = 0, y2 = 0;


			while (y < m_nSrcHeight && uPass < 4)
			{
				// Belt and braces: the inner loop below already leaves y2 in range
				// or uPass at 4, which the outer condition catches.
				if (y2 >= m_nSrcHeight)
					break;

				p = pDib->GetBitmap(y2);
				ScanLine(p);

				y += 1;

				// deal with the Interlace as described in the GIF
				// spec.  Put the decoded scan line out to the screen if we haven't gone
				// past the bottom of it

				y2 += InterlaceSteps[uPass];
				while (y2 >= m_nSrcHeight && uPass < 4)
				{
					uPass += 1;
					y2 = (uPass < 4) ? InterlaceReset[uPass - 1] : m_nSrcHeight;
				}
			}
		}
		else
		{
			while (y < m_nSrcHeight)
			{
				p = pDib->GetBitmap(y);
				ScanLine(p);

				y += 1;
			}
		}

		LPRGBQUAD pRGB = pDib->GetColor();
		p = m_pRgb;

		for (UINT i = 0; i < m_nColorMapSize; i++)
		{
			pRGB[i].rgbRed = *p++;
			pRGB[i].rgbGreen = *p++;
			pRGB[i].rgbBlue = *p++;
			pRGB[i].rgbReserved = 255;
		}

		// Set the Transparent color
		if (m_nTransparentPixel >= 0)
			pRGB[m_nTransparentPixel].rgbReserved = 0x00;
	}
}
