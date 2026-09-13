// ImageWalker by Zac Walker
//
// Purpose: GIF implementation - frame disposal, local palettes and the IPTC
//          comment extension.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#include "stdafx.h"
#include "FileGif.h"

#ifdef COMPILE_GIF

CLoadGif::CLoadGif()
{
}

CLoadGif::~CLoadGif()
{
}

int CLoadGif::ReadFunction(GifFileType* pGifFile, GifByteType* pBytesOut, int nAmount)
{
	auto pStreamIn = static_cast<IW::IStreamIn*>(pGifFile->UserData);
	assert(pStreamIn);

	DWORD dwQtyRead = 0;
	pStreamIn->Read(pBytesOut, nAmount, &dwQtyRead);

	return dwQtyRead;
}


// Actions
bool CLoadGif::Read(const CString& str, IW::IStreamIn* pStreamIn, IW::IImageStream* pImageOut, IW::IStatus* pStatus)
{
	GifFileType* pGifFile = nullptr;
	IW::PixelFormat pf = IW::PixelFormat::PF8;
	CString strIptc;
	int nImageCount = 0;

	try
	{
		GifLastError();

		static int InterlacedOffset[] = {0, 4, 2, 1}; /* The way Interlaced image should. */
		static int InterlacedJumps[] = {8, 8, 4, 2}; /* be read - offsets and jumps... */

		if ((pGifFile = DGifOpen(pStreamIn, ReadFunction)) == nullptr)
		{
			return FALSE;
		}


		// We now know the image size
		// so create a primary dib
		int nStorageWidth = IW::CalcStorageWidth(pGifFile->SWidth, IW::PixelFormat::PF8);
		int nCount, i, j;
		int nTransparentPixel = -1;


		GifRecordType RecordType;
		GifByteType* pExtension = nullptr;

		int nRow, nCol, nWidth, nHeight;
		int nExtCode;
		int nDisposalMethod = 2;
		int nDelayTime = 0;

		ColorMapObject* pColorMap = nullptr;
		// SetBitmap copies the page's DWORD-aligned storage width, not SWidth.
		IW::CBuffer<GifPixelType> pLine(IW::CalcStorageWidth(pGifFile->SWidth, IW::PixelFormat::PF8));


		/* Scan the content of the GIF file and load the image(s) in: */
		do
		{
			if (DGifGetRecordType(pGifFile, &RecordType) == GIF_ERROR)
			{
				// If the error is an undefined record we exit
				// out without an error.
				RecordType = TERMINATE_RECORD_TYPE;
			}

			switch (RecordType)
			{
			case IMAGE_DESC_RECORD_TYPE:

				if (DGifGetImageDesc(pGifFile) == GIF_ERROR)
				{
					RecordType = TERMINATE_RECORD_TYPE;
					break;
				}

				nImageCount += 1;

				nRow = pGifFile->Image.Top; // Image Position relative to Screen.
				nCol = pGifFile->Image.Left;
				nWidth = pGifFile->Image.Width;
				nHeight = pGifFile->Image.Height;

				{
					RECT rcPage = {nCol, nRow, nCol + nWidth, nRow + nHeight};
					pImageOut->CreatePage(rcPage, IW::PixelFormat::PF8, pGifFile->Image.Interlace == 0);
				}
				nStorageWidth = IW::CalcStorageWidth(nWidth, IW::PixelFormat::PF8);

				pColorMap = pGifFile->Image.ColorMap;

				if (pColorMap == nullptr)
				{
					pColorMap = pGifFile->SColorMap;
				}

				{
					if (pColorMap)
					{
						// SetPalette copies all 256 entries; ColorCount is usually fewer.
						COLORREF palette[256] = {};
						pf = IW::PixelFormat::FromBpp(pColorMap->BitsPerPixel);
						int nColorMapSize = min(pColorMap->ColorCount, 256);

						for (i = 0; i < nColorMapSize; i++)
						{
							const BYTE b = pColorMap->Colors[i].Blue;
							const BYTE g = pColorMap->Colors[i].Green;
							const BYTE r = pColorMap->Colors[i].Red;

							// Opaque: GIF carries transparency as an index, not in the
							// colour table, and a zero here is read back as alpha.
									palette[i] = RGB(b, g, r) | 0xFF000000;
						}

						pImageOut->SetPalette(palette);
					}
				}

				if (pGifFile->Image.Left + pGifFile->Image.Width > pGifFile->SWidth ||
					pGifFile->Image.Top + pGifFile->Image.Height > pGifFile->SHeight)
				{
					//fprintf(stderr, "Image %d is not confined to screen dimension, aborted.\n");
					RecordType = TERMINATE_RECORD_TYPE;
					break;
				}
				if (pGifFile->Image.Interlace)
				{
					/* Need to perform 4 passes on the images: */
					for (nCount = i = 0; i < 4; i++)
					{
						for (j = InterlacedOffset[i]; j < nHeight;
						     j += InterlacedJumps[i])
						{
							if (DGifGetLine(pGifFile, pLine, nWidth) == GIF_ERROR)
							{
								//PrintGifError();
								RecordType = TERMINATE_RECORD_TYPE;
								break;
							}

							pImageOut->SetBitmap(j, pLine);
						}
					}
				}
				else
				{
					for (i = 0; i < nHeight; i++)
					{
						if (DGifGetLine(pGifFile, pLine, nWidth) == GIF_ERROR)
						{
							//PrintGifError();
							RecordType = TERMINATE_RECORD_TYPE;
							break;
						}

						pImageOut->SetBitmap(i, pLine);
					}
				}

				if (nTransparentPixel != -1)
				{
					pImageOut->SetBackGround(nTransparentPixel);
					pImageOut->SetTransparent(nTransparentPixel);
				}

				pImageOut->SetTimeDelay(nDelayTime);
				pImageOut->SetDisposalMethod(nDisposalMethod);
				pImageOut->Flush();

				nTransparentPixel = -1;
				nDisposalMethod = 2;

				break;
			case EXTENSION_RECORD_TYPE:
				/* Skip any extension blocks in file: */
				nExtCode = 0;
				if (DGifGetExtension(pGifFile, &nExtCode, &pExtension) == GIF_ERROR)
				{
					//PrintGifError();
					//return  FALSE;
					RecordType = TERMINATE_RECORD_TYPE;
					break;
				}

				while (pExtension != nullptr)
				{
					USES_CONVERSION;

					switch (nExtCode)
					{
					case COMMENT_EXT_FUNC_CODE:
						//printf("GIF89 comment");
						strIptc.Append(CA2T((LPCSTR)pExtension + 1), *pExtension);
						break;
					case GRAPHICS_EXT_FUNC_CODE:
						// Graphic Control Extension
						nTransparentPixel = -1;

						if (*(pExtension + 1) & 0x1)
						{
							nTransparentPixel = *(pExtension + 4);
						}

						nDisposalMethod = (((*(pExtension + 1)) >> 2) & 0x7);
						nDelayTime = *((short*)(pExtension + 2)) * 10;

						break;
					case PLAINTEXT_EXT_FUNC_CODE:
						//printf("GIF89 plaintext");
						break;
					case APPLICATION_EXT_FUNC_CODE:
						//printf("GIF89 application block");
						break;
					default:
						//printf("Extension record of unknown type");
						break;
					}

					if (DGifGetExtensionNext(pGifFile, &pExtension) == GIF_ERROR)
					{
						//PrintGifError();
						//return  FALSE;
						RecordType = TERMINATE_RECORD_TYPE;
						break;
					}
				}
				break;
			case TERMINATE_RECORD_TYPE:
				break;
			default: // Unknown block
				break;
			}

			if (pStatus && pStatus->QueryCancel()) break; // Forced Exit?
		}
		while (RecordType != TERMINATE_RECORD_TYPE);
	}
	catch (std::exception& e)
	{
		// Set error
		ATLTRACE("Exception in CLoadGif::Read %s", e.what());
	}

	// DGifOpen can fail, and the catch above lands here with nothing opened.
	if (pGifFile == nullptr)
		return FALSE;

	CString strInformation;

	// Gif info message
	strInformation.Format(_T("%dx%dx%d GIF"), pGifFile->SWidth, pGifFile->SHeight, pf.ToBpp());

	if (pGifFile->Image.Interlace)
	{
		strInformation += g_szSpace;
		strInformation += App.LoadString(IDS_INTERLACED);
	}

	if (nImageCount > 1)
	{
		strInformation += g_szSpace;
		strInformation += App.LoadString(IDS_ANIMATED);
	}

	// Information
	if (!strIptc.IsEmpty())
	{
		USES_CONVERSION;
		LPCSTR szComment = CT2A(strIptc);
		IW::MetaData comment(IW::MetaDataTypes::PROFILE_COMMENT, szComment, static_cast<DWORD>(strlen(szComment)));
		pImageOut->AddBlob(comment);
	}

	pImageOut->SetStatistics(strInformation);
	pImageOut->SetLoaderName(GetKey());
	pImageOut->SetImageFlags(IW::ImageFlags::Animate);

	IW::CameraSettings settings;
	settings.OriginalImageSize.cx = pGifFile->SWidth;
	settings.OriginalImageSize.cy = pGifFile->SHeight;
	settings.OriginalBpp = pf;

	pImageOut->SetCameraSettings(settings);

	if (DGifCloseFile(pGifFile) == GIF_ERROR)
	{
		// DO NOTHING
	}

	// Store the raw image for loss-less processing
	pImageOut->AddImageData(pStreamIn, IW::MetaDataTypes::GIF_IMAGE);

	return TRUE;
}


int CLoadGif::WriteFunction(GifFileType* pGifFile, const GifByteType* pBytesOut, int nAmount)
{
	auto pStreamOut = static_cast<IW::IStreamOut*>(pGifFile->UserData);
	assert(pStreamOut);

	DWORD dwQtyWrite;
	pStreamOut->Write(pBytesOut, nAmount, &dwQtyWrite);

	return dwQtyWrite;
}

bool CLoadGif::Write(IW::IStreamOut* pStreamOut, IW::IStreamIn* pStreamIn, const IW::Image& imageIn,
                     const IW::CodecSettings& settings, IW::IStatus* pStatus)
{
	int i, nExtCode, Row, Col, Width, Height;

	GifRecordType RecordType;
	GifByteType* Extension;

	GifRowType pLineBuffer;
	GifFileType *GifFileIn = nullptr, *GifFileOut = nullptr;


	if ((GifFileIn = DGifOpen(pStreamIn, ReadFunction)) == nullptr)
		return false;

	// Open stdout for the output file: 
	if ((GifFileOut = EGifOpen(pStreamOut, WriteFunction)) == nullptr)
	{
		DGifCloseFile(GifFileIn);
		return false;
	}

	// Dump out exactly same screen information: 
	if (EGifPutScreenDesc(GifFileOut,
	                      GifFileIn->SWidth, GifFileIn->SHeight,
	                      GifFileIn->SColorResolution, GifFileIn->SBackGroundColor,
	                      GifFileIn->SColorMap) == GIF_ERROR)
	{
		DGifCloseFile(GifFileIn);
		EGifCloseFile(GifFileOut);
		return false;
	}

	IW::CBuffer<GifPixelType> lineStorage(GifFileIn->SWidth);

	pLineBuffer = lineStorage;

	if (pLineBuffer == nullptr)
	{
		DGifCloseFile(GifFileIn);
		EGifCloseFile(GifFileOut);
		return false;
	}

	// Now add the comment!!
	// Existing comments have to be replaced with
	// the stored comment profile
	if (imageIn.HasMetaData(IW::MetaDataTypes::PROFILE_COMMENT))
	{
		const IW::MetaData comment = imageIn.GetMetaData(IW::MetaDataTypes::PROFILE_COMMENT);

		// Comment blocks must be smaller the 256
		// So write in bits
		int nLengthRemaining = static_cast<int>(comment.GetSize());
		auto pStr = reinterpret_cast<LPCSTR>(comment.GetData());

		while (nLengthRemaining > 0)
		{
			int nWrite = IW::Min(200, nLengthRemaining);

			EGifPutExtension(GifFileOut, COMMENT_EXT_FUNC_CODE,
			                 nWrite,
			                 pStr);

			nLengthRemaining -= nWrite;
			pStr += nWrite;
		}
	}


	// Scan the content of the GIF file and load the image(s) in: 
	do
	{
		if (DGifGetRecordType(GifFileIn, &RecordType) == GIF_ERROR)
		{
			DGifCloseFile(GifFileIn);
			EGifCloseFile(GifFileOut);
			return false;
		}

		switch (RecordType)
		{
		case IMAGE_DESC_RECORD_TYPE:
			if (DGifGetImageDesc(GifFileIn) == GIF_ERROR)
			{
				DGifCloseFile(GifFileIn);
				EGifCloseFile(GifFileOut);
				return false;
			}

			Row = GifFileIn->Image.Top; // Image Position relative to Screen. 
			Col = GifFileIn->Image.Left;
			Width = GifFileIn->Image.Width;
			Height = GifFileIn->Image.Height;

			// giflib does not confine a frame to the logical screen, and pLineBuffer
			// is only SWidth wide.
			if (Width <= 0 || Height <= 0 ||
				Col < 0 || Row < 0 ||
				Col + Width > GifFileIn->SWidth ||
				Row + Height > GifFileIn->SHeight)
			{
				DGifCloseFile(GifFileIn);
				EGifCloseFile(GifFileOut);
				return false;
			}

			// Put the image descriptor to out file: 
			if (EGifPutImageDesc(GifFileOut, Col, Row, Width, Height,
			                     GifFileIn->Image.Interlace, GifFileIn->Image.ColorMap) == GIF_ERROR)
			{
				DGifCloseFile(GifFileIn);
				EGifCloseFile(GifFileOut);
				return false;
			}


			// Load the image, and dump it. 
			for (i = 0; i < Height; i++)
			{
				//GifQprintf("\b\b\b\b%-4d", i);
				if (DGifGetLine(GifFileIn, pLineBuffer, Width) == GIF_ERROR) break;
				if (EGifPutLine(GifFileOut, pLineBuffer, Width) == GIF_ERROR)
				{
					DGifCloseFile(GifFileIn);
					EGifCloseFile(GifFileOut);
					return false;
				}
			}
			break;

		case EXTENSION_RECORD_TYPE:
			// copy other extensions
			nExtCode = 0;

			if (DGifGetExtension(GifFileIn, &nExtCode, &Extension) == GIF_ERROR)
			{
				DGifCloseFile(GifFileIn);
				EGifCloseFile(GifFileOut);
				return false;
			}

			// We skip comments!!
			// But copy other extensions
			while (Extension != nullptr)
			{
				if (nExtCode != COMMENT_EXT_FUNC_CODE)
				{
					if (EGifPutExtension(GifFileOut, nExtCode, Extension[0],
					                     Extension + 1) == GIF_ERROR)
					{
						DGifCloseFile(GifFileIn);
						EGifCloseFile(GifFileOut);
						return false;
					}
				}

				if (DGifGetExtensionNext(GifFileIn, &Extension) == GIF_ERROR)
				{
					DGifCloseFile(GifFileIn);
					EGifCloseFile(GifFileOut);
					return false;
				}
			}

			break;

		case TERMINATE_RECORD_TYPE:
			break;

		default: /* Should be traps by DGifGetRecordType. */
			break;
		}
	}
	while (RecordType != TERMINATE_RECORD_TYPE);


	if (DGifCloseFile(GifFileIn) == GIF_ERROR)
	{
		EGifCloseFile(GifFileOut);
		return false;
	}

	if (EGifCloseFile(GifFileOut) == GIF_ERROR)
	{
		return false;
	}

	return true;
}

bool CLoadGif::Write(const CString& str, IW::IStreamOut* pStreamOut, const IW::Image& imageIn, const IW::CodecSettings& settings, IW::IStatus* pStatus)
{
	CString strStatus;
	strStatus.Format(IDS_ENCODING_FMT, static_cast<LPCTSTR>(GetTitle()));
	pStatus->SetStatusMessage(strStatus);

	if (imageIn.HasMetaData(IW::MetaDataTypes::GIF_IMAGE))
	{
		const IW::MetaData data = imageIn.GetMetaData(IW::MetaDataTypes::GIF_IMAGE);
		IW::StreamConstBlob streamIn(data);

			return Write(pStreamOut, &streamIn, imageIn, settings, pStatus);
	}

	IW::Page pageIn = imageIn.GetFirstPage();

	ColorMapObject* pOutputColorMap1 = nullptr;
	ColorMapObject* pOutputColorMap2 = nullptr;
	IW::Image image = imageIn;


	// easy if we already have a palette
	// other wise fail
	if (!pageIn.GetPixelFormat().HasPalette())
	{
		IW::Image imageQuantize;
		IW::Quantize(image, imageQuantize, pStatus);
		image = imageQuantize;
		pageIn = image.GetFirstPage();
	}

	int nColorMapSize = pageIn.GetPixelFormat().NumberOfPaletteEntries();
	if ((pOutputColorMap1 = MakeMapObject(nColorMapSize, nullptr)) == nullptr)
		return false;

	if ((pOutputColorMap2 = MakeMapObject(nColorMapSize, nullptr)) == nullptr)
		return false;

	IW::LPCCOLORREF pRGB = pageIn.GetPalette();

	for (int i = 0; i < nColorMapSize; i++)
	{
		pOutputColorMap1->Colors[i].Blue = IW::GetR(pRGB[i]);
		pOutputColorMap1->Colors[i].Green = IW::GetG(pRGB[i]);
		pOutputColorMap1->Colors[i].Red = IW::GetB(pRGB[i]);
	}

	GifFileType* GifFile;

	// Open stdout for the output file:
	if ((GifFile = EGifOpen(pStreamOut, WriteFunction)) == nullptr)
	{
		// Free objects
		FreeMapObject(pOutputColorMap1);
		FreeMapObject(pOutputColorMap2);

		return false;
	}

	// One cleanup path: every failure below used to free whichever map was in
	// scope and leave the encoder itself open.
	struct Guard
	{
		GifFileType*& file;
		ColorMapObject*& map1;
		ColorMapObject*& map2;

		~Guard()
		{
			if (map1) FreeMapObject(map1);
			if (map2) FreeMapObject(map2);
			if (file) EGifCloseFile(file);
		}
	} guard{GifFile, pOutputColorMap1, pOutputColorMap2};

	CRect rc = image.GetBoundingRect();

	if (EGifPutScreenDesc(GifFile, rc.right - rc.left, rc.bottom - rc.top, 8, 0, pOutputColorMap1) == GIF_ERROR)
	{
		return false;
	}

	if (image.GetPageCount() > 1)
	{
		// Netscape extension to make sure
		// the slide show loops
		BYTE buff[] = {
			'!', APPLICATION_EXT_FUNC_CODE, 11,
			'N', 'E', 'T', 'S', 'C', 'A', 'P', 'E', '2', '.', '0',
			3, 1, 0, 0, 0
		};

		// giflib writes the leader, the length-prefixed blocks and the trailer;
		// one 19-byte blob would be a single malformed sub-block.
		EGifPutExtensionLeader(GifFile, APPLICATION_EXT_FUNC_CODE);
		EGifPutExtensionBlock(GifFile, 11, buff + 3);
		EGifPutExtensionBlock(GifFile, 3, buff + 15);
		EGifPutExtensionTrailer(GifFile);
	}

	// Now add the comment!!
	// Existing comments have to be replaced with
	// the stored comment profile
	if (image.HasMetaData(IW::MetaDataTypes::PROFILE_COMMENT))
	{
		const IW::MetaData comment = image.GetMetaData(IW::MetaDataTypes::PROFILE_COMMENT);

		// Comment blocks must be smaller the 256
		// So write in bits
		int nLengthRemaining = static_cast<int>(comment.GetSize());
		auto pStr = reinterpret_cast<LPCSTR>(comment.GetData());

		while (nLengthRemaining > 0)
		{
			int nWrite = IW::Min(200, nLengthRemaining);

			EGifPutExtension(GifFile, COMMENT_EXT_FUNC_CODE,
			                 nWrite,
			                 pStr);

			nLengthRemaining -= nWrite;
			pStr += nWrite;
		}
	}

	for (IW::Image::PageList::const_iterator page = image.Pages.begin(); page != image.Pages.end(); ++page)
	{
		DWORD dw = page->GetFlags();

		int nTimeDelay = page->GetTimeDelay() / 10;
		int nTransparentPixel = page->GetTransparent();

		// Add a graphic control extension if
		// the image is multi paged
		DWORD nDisposal = ((dw & IW::PageFlags::HasTransparent) ? 0x1 : 0x0) | (((dw >> 8) & 0x7) << 2);

		BYTE graphics[] = {
			static_cast<BYTE>(nDisposal), // disposal
			static_cast<BYTE>(nTimeDelay),
			static_cast<BYTE>(nTimeDelay >> 8), // Time Delay
			static_cast<BYTE>(nTransparentPixel), // Transparent
			0,
			0
		};

		EGifPutExtension(GifFile,
		                 GRAPHICS_EXT_FUNC_CODE,
		                 4,
		                 graphics);


		// If we have multiple pages
		// then we have multiple color maps
		const IW::PixelFormat pfPage = page->GetPixelFormat();

		// A later page can be a narrower format than the first, and the map was
		// sized from the first -- reading it at that count runs off the palette.
		if (!pfPage.HasPalette())
			return false;

		const int nPageColors = IW::Min(pfPage.NumberOfPaletteEntries(), nColorMapSize);
		IW::LPCCOLORREF pagePalette = page->GetPalette();

		IW::MemZero(pOutputColorMap2->Colors, nColorMapSize * sizeof(GifColorType));

		for (int i = 0; i < nPageColors; i++)
		{
			pOutputColorMap2->Colors[i].Blue = IW::GetR(pagePalette[i]);
			pOutputColorMap2->Colors[i].Green = IW::GetG(pagePalette[i]);
			pOutputColorMap2->Colors[i].Red = IW::GetB(pagePalette[i]);
		}


		// If the palettes are the same then dont need it
		ColorMapObject* pOutputColorMap = nullptr;

		if (memcmp(pOutputColorMap1->Colors, pOutputColorMap2->Colors, nColorMapSize * 3) != 0)
		{
			pOutputColorMap = pOutputColorMap2;
		}

		RECT pageRect = page->GetPageRect();

		// Description
		if (EGifPutImageDesc(GifFile, pageRect.left, pageRect.top,
		                     page->GetWidth(), page->GetHeight(),
		                     FALSE, pOutputColorMap) == GIF_ERROR)
		{
			return false;
		}


		IW::CBuffer<BYTE> p(page->GetWidth());
		int xx = 0;

		static int masks[] = {
			0x80, 0x40, 0x20, 0x10,
			0x08, 0x04, 0x02, 0x01
		};


		for (int i = 0; i < page->GetHeight(); i++)
		{
			LPCBYTE pLineSrc = page->GetBitmapLine(i);

			switch (page->GetPixelFormat()._pf)
			{
			case IW::PixelFormat::PF1:
				{
					for (xx = 0; xx < page->GetWidth(); xx++)
					{
						p[xx] = (pLineSrc[xx >> 3] & masks[xx & 7]) ? 0x1 : 0x00;
					}
					break;
				}
			case IW::PixelFormat::PF4:
				{
					for (xx = 0; xx < page->GetWidth(); xx++)
					{
						p[xx] = (xx & 1) ? pLineSrc[xx >> 1] & 0x0f : pLineSrc[xx >> 1] >> 4;
					}
					break;
				}
			case IW::PixelFormat::PF8:
				{
					IW::MemCopy(p, pLineSrc, page->GetWidth());
					break;
				}
			default:
				assert(FALSE);
			}

			if (EGifPutLine(GifFile, p, page->GetWidth()) == GIF_ERROR)
			{
				return false;
			}

			// Setting the error and carrying on wrote the whole file anyway.
			if (pStatus->QueryCancel())
			{
				pStatus->SetError(App.LoadString(IDS_CANCELED));
				return false;
			}

			pStatus->Progress(i, page->GetHeight());
		}
	}

	// Closed here so a write error is reported; the guard only mops up.
	GifFileType* pClose = GifFile;
	GifFile = nullptr;

	return EGifCloseFile(pClose) != GIF_ERROR;
}

#endif // COMPILE_GIF
