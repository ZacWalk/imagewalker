#pragma once

#pragma push_macro("min")
#pragma push_macro("max")
#undef min
#undef max
#include <iw/imaging/Document.h>
#include <iw/imaging/ImageEdits.h>
#include <iw/imaging/PixelOps.h>
#include <iw/imaging/WebPCodec.h>
#include <iw/imaging/Diagnostics.h>
#include <iw/channelaverage.h>
#pragma pop_macro("max")
#pragma pop_macro("min")

#include <iw/legacy/LegacyPch.h>
#include <iw/legacy/FileFormat.h>
#include <iw/legacy/FileFormatList.h>
#include <iw/legacy/FileBmp.h>
#include <iw/legacy/FileCompoundDoc.h>
#include <iw/legacy/FileGif.h>
#include <iw/legacy/FileJpeg.h>
#include <iw/legacy/FileMetaFile.h>
#include <iw/legacy/FileModern.h>
#include <iw/legacy/FilePcx.h>
#include <iw/legacy/FilePng.h>
#include <iw/legacy/FilePsd.h>
#include <iw/legacy/FileTiff.h>
#include <iw/legacy/FileWpg.h>
#include <iw/legacy/FileFormatAny.h>
#include <iw/legacy/Imaging.h>
#include <iw/legacy/ImagingLock.h>
#include <iw/legacy/ImagingBlitter.h>
#include <iw/legacy/ImagingBlitterSimd.h>
#include <iw/legacy/ImagingEdits.h>
#include <iw/legacy/ImagingStreams.h>
#include <iw/legacy/RenderImage.h>
#include <iw/legacy/Metadata.h>
#include <iw/legacy/MetadataExif.h>
#include <iw/legacy/MetadataIPTC.h>
#include <iw/legacy/MetadataXMP.h>
#include <iw/legacy/FileJpegTran.h>
#include <iw/legacy/JpegTransform.h>
#include <iw/legacy/NativeCodec.h>
#include <iw/legacy/LibfilesDocument.h>
#include <iw/legacy/DibSurface.h>
#include <iw/legacy/OperationStatus.h>
#include <iw/legacy/UtilStrings.h>
#include <iw/legacy/UtilMemory.h>
#include <iw/legacy/UtilSerialize.h>
#include <iw/legacy/CoreSupport.h>

// Historical ImageWalker adapters are inline so every application can consume the
// same compatibility boundary without a compiled shared implementation object.

// ---- FileBmp ----
// ImageWalker by Zac Walker
//
// Purpose: Windows BMP and DIB implementation.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#include "LegacyPch.h"

#include "FileBmp.h"
#include "LibfilesDocument.h"
#include <iw/legacy/NativeCodec.h>
#include "ImagingStreams.h"

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

inline CLoadBmp::CLoadBmp()
{
}

inline CLoadBmp::~CLoadBmp()
{
}


inline void CLoadBmp::CopyPalette(LPCOLORREF pDst, IW::LPCCOLORREF pSrc, UINT uColorEntries) const
{
	for (UINT i = 0; i < uColorEntries; i++)
	{
		pDst[i] = pSrc[i] | 0xff000000;
	}
}


inline bool CLoadBmp::Read(const CString& strType, IW::IStreamIn* pStreamIn, IW::IImageStream* pImageOut, IW::IStatus* pStatus)
{
	BITMAPINFOHEADER header{};
	DWORD header_bytes{};
	pStreamIn->Seek(IW::IStreamCommon::eBegin, sizeof(BITMAPFILEHEADER));
	pStreamIn->Read(&header, sizeof(header), &header_bytes);
	pStreamIn->Seek(IW::IStreamCommon::eBegin, 0);
	if (header_bytes == sizeof(header) && header.biCompression == BI_RGB)
		return iw::imaging::detail::read_libfiles_document(libfiles::image_format::bmp,
			GetKey(), pStreamIn, pImageOut, pStatus);

	// Compressed DIBs retain the Windows SDK rendering path.
	IW::MetaData data;
	HRESULT hr = data.LoadFromStream(pStreamIn);

	if (FAILED(hr))
		return false;

	DWORD nSize = data.GetDataSize();
	LPBYTE pByte = data.GetData();

	if (pByte == nullptr || nSize < sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER))
	{
		return false;
	}

	// Dib Header
	auto pInfo = (BITMAPINFO*)(pByte + sizeof(BITMAPFILEHEADER));
	const auto* fileHeader = reinterpret_cast<const BITMAPFILEHEADER*>(pByte);
	if (fileHeader->bfType != 0x4d42 || pInfo->bmiHeader.biWidth <= 0 ||
		pInfo->bmiHeader.biHeight == 0 || pInfo->bmiHeader.biHeight == INT_MIN)
	{
		pStatus->SetError(L"Invalid BMP header or dimensions.");
		return false;
	}

	// Check that we got a real Windows DIB
	if (pInfo->bmiHeader.biSize != sizeof(BITMAPINFOHEADER))
	{
		return FALSE;
	}

	// I only want single plain headers
	if (pInfo->bmiHeader.biPlanes != 1)
	{
		return FALSE;
	}

	IW::PixelFormat pf = IW::PixelFormat::FromBpp(pInfo->bmiHeader.biBitCount);
	UINT nWidth = pInfo->bmiHeader.biWidth;
	UINT nHeight = abs(pInfo->bmiHeader.biHeight); // negative biHeight means a top-down DIB

	UINT nColors = pInfo->bmiHeader.biClrUsed;
	const UINT nMaxColors = pf.NumberOfPaletteEntries();

	if (nColors == 0 || nColors > nMaxColors)
	{
		nColors = nMaxColors;
	}

	if (nWidth == 0 || nHeight == 0)
	{
		return false;
	}

	const auto minimumOffset = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) +
		static_cast<std::uint64_t>(nColors) * sizeof(RGBQUAD) +
		(pInfo->bmiHeader.biCompression == BI_BITFIELDS ? 3 * sizeof(DWORD) : 0);
	// bfSize is informational and often wrong, and some writers leave bfOffBits zero
	// to mean "after the palette". A nonzero offset must still lie inside the file.
	const auto offset = fileHeader->bfOffBits != 0 ? fileHeader->bfOffBits : static_cast<DWORD>(minimumOffset);
	const auto stride = ((static_cast<std::uint64_t>(nWidth) * pf.ToStorageBpp() + 31) / 32) * 4;
	const auto bytes = stride * nHeight;
	if (offset < minimumOffset || offset >= nSize ||
		((pInfo->bmiHeader.biCompression == BI_RGB || pInfo->bmiHeader.biCompression == BI_BITFIELDS) &&
			bytes > nSize - offset))
	{
		pStatus->SetError(L"The BMP pixel data is truncated or its offset is invalid.");
		return false;
	}

	auto* pBitmap = pByte + offset;
	const int nBytesMax = static_cast<int>((std::min)(nSize - offset, static_cast<DWORD>(INT_MAX)));

	IW::Image image;

	if (!image.Copy(pInfo, pBitmap, nBytesMax))
	{
		return false;
	}
	if (pf.HasPalette())
	{
		auto palette = image.GetFirstPage().GetPalette();
		for (UINT i = 0; i < nMaxColors; ++i) palette[i] |= 0xff000000;
	}

	IW::IterateImage(image, *pImageOut, pStatus);

	// Information
	CString strInformation;
	strInformation.Format(IW::Text::BMP_FMT, nWidth, nHeight, pf.ToBpp());

	IW::CameraSettings settings;
	settings.XPelsPerMeter = pInfo->bmiHeader.biXPelsPerMeter;
	settings.YPelsPerMeter = pInfo->bmiHeader.biYPelsPerMeter;
	settings.OriginalImageSize.cx = nWidth;
	settings.OriginalImageSize.cy = nHeight;
	settings.OriginalBpp = pf;

	pImageOut->SetCameraSettings(settings);
	pImageOut->SetLoaderName(GetKey());
	pImageOut->SetStatistics(strInformation);

	return true;
}

inline bool CLoadBmp::Write(const CString& strType, IW::IStreamOut* pStreamOut, const IW::Image& imageIn, const IW::CodecSettings& settings, IW::IStatus* pStatus)
{
	CString status;
	status.Format(IW::Text::ENCODING_FMT, static_cast<LPCTSTR>(GetTitle()));
	pStatus->SetStatusMessage(status);
	return iw::imaging::write_libfiles_document(
		libfiles::image_format::bmp, pStreamOut, imageIn, settings, pStatus);
}

// ---- FileCompoundDoc ----
#include "LegacyPch.h"
#include "FileCompoundDoc.h"

#include <platform.h>

namespace iw::legacy_detail::compound_doc
{
	inline std::span<const std::uint8_t> read_input(IW::IStreamIn& input,
		std::vector<std::uint8_t>& owned, IW::IStatus& status)
	{
		const auto size = input.GetFileSize().GetByteCount();
		if (size == 0 || size > INT_MAX)
		{
			if (size > INT_MAX) status.SetResourceError(
				L"The compound document is empty or exceeds the input limit.");
			else status.SetError(L"The compound document is empty or exceeds the input limit.");
			return {};
		}
		owned.resize(static_cast<std::size_t>(size));
		if (input.Seek(IW::IStreamCommon::eBegin, 0) != 0)
		{
			status.SetError(L"The compound document could not be read completely.");
			return {};
		}
		DWORD read{};
		if (!input.Read(owned.data(), static_cast<DWORD>(owned.size()), &read) ||
			read != owned.size())
		{
			status.SetError(L"The compound document could not be read completely.");
			return {};
		}
		return owned;
	}

	inline void set_failure(IW::IStatus& status, const pf::bitmap_load_result& result)
	{
		if (result.error == pf::bitmap_load_error::cancelled)
		{
			status.SetError(L"Compound preview decoding was cancelled.");
			return;
		}
		const auto message = result.diagnostic.empty()
			? CString(L"The compound document thumbnail could not be decoded.")
			: CString(CA2W(result.diagnostic.c_str(), CP_UTF8));
		if (result.error == pf::bitmap_load_error::too_large)
			status.SetResourceError(message);
		else
			status.SetError(message);
	}
}

inline CLoadCompoundDoc::CLoadCompoundDoc()
{
}

inline CLoadCompoundDoc::~CLoadCompoundDoc()
{
}

inline bool CLoadCompoundDoc::Read(
	const CString&,
	IW::IStreamIn* pStreamIn,
	IW::IImageStream* pImageOut,
	IW::IStatus* pStatus)
try
{
	if (!pStreamIn || !pImageOut || !pStatus) return false;

	std::vector<std::uint8_t> owned;
	const auto bytes = iw::legacy_detail::compound_doc::read_input(
		*pStreamIn, owned, *pStatus);
	if (bytes.empty()) return false;

	pf::bitmap_load_options options;
	options.max_pixels = IW::kMaxPageBytes / sizeof(std::uint32_t);
	options.max_decoded_bytes = IW::kMaxPageBytes;
	const auto thumbnail = pImageOut->GetThumbnailSize();
	if (thumbnail.cx > 0 && thumbnail.cy > 0)
	{
		options.target_width = static_cast<std::uint32_t>(thumbnail.cx);
		options.target_height = static_cast<std::uint32_t>(thumbnail.cy);
		options.thumbnail = true;
	}
	options.cancelled = [pStatus] { return pStatus->QueryCancel(); };
	options.progress = [pStatus](const std::uint64_t current, const std::uint64_t total)
	{
		pStatus->Progress(
			static_cast<int>((std::min)(current, static_cast<std::uint64_t>(INT_MAX))),
			static_cast<int>((std::min)(total, static_cast<std::uint64_t>(INT_MAX))));
	};

	const auto decoded = pf::load_compound_preview_result(bytes, options);
	if (!decoded.success())
	{
		iw::legacy_detail::compound_doc::set_failure(*pStatus, decoded);
		return false;
	}
	const auto& bitmap = *decoded.image;
	if (bitmap.width <= 0 || bitmap.height <= 0 ||
		bitmap.pixels.size() != static_cast<std::size_t>(bitmap.width) * bitmap.height)
	{
		pStatus->SetError(L"The compound document thumbnail has invalid pixel storage.");
		return false;
	}

	IW::CameraSettings settings;
	settings.OriginalImageSize = CSize(
		static_cast<int>(decoded.source_width ? decoded.source_width : bitmap.width),
		static_cast<int>(decoded.source_height ? decoded.source_height : bitmap.height));
	settings.OriginalBpp = IW::PixelFormat::PF32;
	settings.Orientation = decoded.stored_orientation;
	pImageOut->SetCameraSettings(settings);
	pImageOut->SetStatistics(GetTitle());
	pImageOut->SetLoaderName(GetKey());
	pImageOut->CreatePage(CRect(0, 0, bitmap.width, bitmap.height),
		IW::PixelFormat::PF32, true);
	const auto* pixels = bitmap.pixels.data();
	for (int y = 0; y < bitmap.height; ++y)
	{
		if (pStatus->QueryCancel())
		{
			pStatus->SetError(L"Compound preview decoding was cancelled.");
			return false;
		}
		pImageOut->SetBitmap(y, pixels + static_cast<std::size_t>(y) * bitmap.width);
	}
	pImageOut->Flush();
	return true;
}
catch (const std::bad_alloc&)
{
	if (pStatus) pStatus->SetResourceError(L"Not enough memory to read the compound document.");
	return false;
}
catch (const std::exception& error)
{
	if (pStatus) pStatus->SetError(CString(error.what()));
	return false;
}

inline bool CLoadCompoundDoc::Write(const CString&, IW::IStreamOut*, const IW::Image&,
	const IW::CodecSettings&, IW::IStatus*)
{
	return false;
}

// ---- FileFormat ----
// ImageWalker by Zac Walker
//
// Purpose: CLoadBase shared behaviour.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#include "LegacyPch.h"
#include "FileFormat.h"

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

inline CLoadBase::CLoadBase()
{
	//m_nBufferSize = 0;
	//m_pBuffer = NULL;
}

inline CLoadBase::~CLoadBase()
{
	//if (m_pBuffer)
	//	delete m_pBuffer;
}

// ---- FileFormatAny ----
// ImageWalker by Zac Walker
//
// Purpose: Historical format-label catalog. Codec parsing and native writing
// are delegated to libfiles at the legacy boundary.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#include "LegacyPch.h"

#include "FileJpeg.h"
#include "FileBmp.h"
#include "FileGif.h"
#include "FilePcx.h"
#include "FilePng.h"
#include "FileTiff.h"
#include "FilePsd.h"
#include "FileWpg.h"
#include "FileMetaFile.h"
#include "FileCompoundDoc.h"
#include "FileModern.h"
#include "FileFormatAny.h"

//////////////////////////////////////////////////////////////
// The historical dialog catalog

template <class T>
inline IW::IImageLoader* CreateLoader()
{
	return new IW::RefObj<T>;
}

// Each loader still describes itself through its own static members, so adding
// one is a single row here.
template <class T>
inline IW::ImageLoaderInfo LoaderInfo()
{
	IW::ImageLoaderInfo info;
	info._strKey = T::_GetKey();
	info._strTitle = T::_GetTitle();
	info._strDescription = T::_GetDescription();
	info._strExtensionList = T::_GetExtensionList();
	info._strExtensionDefault = T::_GetExtensionDefault();
	info._dwFlags = T::_GetFlags();
	info._pfnCreate = &CreateLoader<T>;
	return info;
}

inline ImageLoaders::ImageLoaders()
{
	// Reserved so the rows never move: every lookup hands out a pointer into
	// this vector and those have to stay valid.
	_loaders.reserve(16);

	Add(LoaderInfo<CLoadJpg>());
	AddHeaderWord(0xD8FF, CLoadJpg::_GetKey());

	Add(LoaderInfo<CLoadBmp>());
	AddHeaderWord(0x4D42, CLoadBmp::_GetKey());

#ifdef COMPILE_GIF
	Add(LoaderInfo<CLoadGif>());
	AddHeaderWord(0x4947, CLoadGif::_GetKey());
#endif

#ifdef COMPILE_PNG
	Add(LoaderInfo<CLoadPng>());
	AddHeaderWord(0x5089, CLoadPng::_GetKey());
#endif

#ifdef COMPILE_TIFF
	Add(LoaderInfo<CLoadTiff>());
	AddHeaderWord(0x4949, CLoadTiff::_GetKey());
	AddHeaderWord(0x4d4d, CLoadTiff::_GetKey());
#endif

#ifdef COMPILE_PSD
	Add(LoaderInfo<CLoadPsd>());
#endif

	Add(LoaderInfo<CLoadWpg>());

	Add(LoaderInfo<CLoadPcx>());
	AddHeaderWord(0x050A, CLoadPcx::_GetKey());

	Add(LoaderInfo<CLoadMetaFile>());
	Add(LoaderInfo<CLoadCompoundDoc>());
	AddHeaderWord(0x4238, CLoadPsd::_GetKey());

	Add(LoaderInfo<CLoadWebP>());
	AddHeaderWord(0x4952, CLoadWebP::_GetKey());
	Add(LoaderInfo<CLoadWindows>(), true);

	assert(_loaders.size() <= 16);

	// Built last: Add() grows _loaders and would invalidate these.
	_sorted.reserve(_byKey.size());

	for (auto it = _byKey.begin(); it != _byKey.end(); ++it)
		_sorted.push_back(&_loaders[it->second]);
}

inline void ImageLoaders::Add(const IW::ImageLoaderInfo& source, bool unclaimedOnly)
{
	IW::ImageLoaderInfo info = source;
	if (unclaimedOnly)
	{
		CString extensions;
		int position = 0;
		CString extension = source.GetExtensionList().Tokenize(L",", position);
		while (!extension.IsEmpty())
		{
			extension.MakeLower();
			if (!_byExtension.contains(extension))
			{
				if (extensions.IsEmpty()) info._strExtensionDefault = extension;
				else extensions += L",";
				extensions += extension;
			}
			extension = source.GetExtensionList().Tokenize(L",", position);
		}
		if (extensions.IsEmpty()) return;
		info._strExtensionList = extensions;
	}
	CString strKey = info.GetKey();
	strKey.MakeLower();

	assert(!_byKey.contains(strKey));

	const size_t n = _loaders.size();
	_loaders.push_back(info);
	_byKey[strKey] = n;

	CString str(info.GetExtensionList());
	int curPos = 0;

	CString token = str.Tokenize(_T(","), curPos);

	while (!token.IsEmpty())
	{
		token.MakeLower();
		_byExtension[token] = n;
		token = str.Tokenize(_T(","), curPos);
	}
}

inline void ImageLoaders::AddHeaderWord(WORD w, const CString& strKey)
{
	CString str(strKey);
	str.MakeLower();

	auto it = _byKey.find(str);

	if (it != _byKey.end())
	{
		_byHeaderWord[w] = it->second;
	}
}

inline IW::ImageLoaderInfoPtr ImageLoaders::Find(const CString &strKeyIn) const
{
	CString strKey(strKeyIn);
	strKey.MakeLower();

	auto itKey = _byKey.find(strKey);

	if (itKey != _byKey.end())
		return &_loaders[itKey->second];

	// If it has a dot try without
	if (!strKey.IsEmpty() && strKey[0] == '.')
		strKey.Delete(0);

	auto itExt = _byExtension.find(strKey);

	if (itExt != _byExtension.end())
		return &_loaders[itExt->second];

	return nullptr;
}

inline IW::ImageLoaderInfoPtr ImageLoaders::Find(WORD w) const
{
	auto it = _byHeaderWord.find(w);

	if (it != _byHeaderWord.end())
		return &_loaders[it->second];

	return nullptr;
}

// ---- FileGif ----
// ImageWalker by Zac Walker
//
// Purpose: Historical GIF boundary adapter. GIF structure and encoding belong
// to libfiles; this file retains only the legacy stream/status contract.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.

#include "LegacyPch.h"
#include "FileGif.h"
#include "LibfilesDocument.h"
#include <iw/legacy/NativeCodec.h>

#ifdef COMPILE_GIF

inline CLoadGif::CLoadGif() = default;
inline CLoadGif::~CLoadGif() = default;

inline int CLoadGif::ReadFunction(GifFileType*, GifByteType*, int)
{
	return 0;
}

inline int CLoadGif::WriteFunction(GifFileType*, const GifByteType*, int)
{
	return 0;
}

inline bool CLoadGif::Read(const CString&, IW::IStreamIn* input, IW::IImageStream* output,
                    IW::IStatus* status)
{
	return iw::imaging::detail::read_libfiles_document(
		libfiles::image_format::gif, GetKey(), input, output, status);
}

inline bool CLoadGif::Write(IW::IStreamOut* output, IW::IStreamIn*, const IW::Image& image,
                     const IW::CodecSettings& settings, IW::IStatus* status)
{
	return Write(GetKey(), output, image, settings, status);
}

inline bool CLoadGif::Write(const CString&, IW::IStreamOut* output, const IW::Image& image,
                     const IW::CodecSettings& settings, IW::IStatus* status)
{
	if (!output || !status || image.IsEmpty() || status->QueryCancel()) return false;
	IW::Image indexed = image;
	if (!indexed.GetFirstPage().GetPixelFormat().HasPalette())
	{
		IW::Image quantized;
		if (!IW::Quantize(indexed, quantized, status)) return false;
		indexed = quantized;
	}

	for (auto& page : indexed.Pages)
	{
		auto* palette = page.GetPalette();
		int transparent = (page.GetFlags() & IW::PageFlags::HasTransparent)
			? static_cast<int>(page.GetTransparent()) : -1;
		for (int i = 0; i < page.GetPixelFormat().NumberOfPaletteEntries(); ++i)
		{
			if (i == transparent || (IW::GetA(palette[i]) == 0 && transparent < 0))
				transparent = i;
			else
				palette[i] = (palette[i] & 0x00ffffffu) | 0xff000000u;
		}
		if (transparent >= 0)
		{
			palette[transparent] &= 0x00ffffffu;
			page.SetTransparent(static_cast<DWORD>(transparent));
		}
	}
	return iw::imaging::write_libfiles_document(
		libfiles::image_format::gif, output, indexed, settings, status);
}

#endif // COMPILE_GIF

// ---- FileJpeg ----
// ImageWalker by Zac Walker
//
// Purpose: Historical JPEG boundary adapter. libfiles owns JPEG pixels,
// metadata and coefficient transforms; this file translates the old streams
// and status callbacks.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.

#include "LegacyPch.h"
#include "FileJpeg.h"
#include "LibfilesDocument.h"
#include <iw/legacy/NativeCodec.h>
#include <iw/jfifdensity.h>

#include <libfiles/encode.h>

namespace iw::legacy_detail::source_filejpeg
{
	inline std::vector<std::uint8_t> read_stream(IW::IStreamIn* input)
	{
		if (!input) return {};
		const auto memory = input->GetMemory();
		if (!memory.empty()) return {memory.begin(), memory.end()};
		const auto size = input->GetFileSize().GetByteCount();
		if (size == 0 || size > INT_MAX) return {};
		std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
		if (input->Seek(IW::IStreamCommon::eBegin, 0) != 0) return {};
		DWORD offset = 0;
		while (offset < bytes.size())
		{
			DWORD read{};
			const auto wanted = static_cast<DWORD>(
				(std::min<std::size_t>)(bytes.size() - offset, MAXDWORD));
			if (!input->Read(bytes.data() + offset, wanted, &read) || read == 0) return {};
			offset += read;
		}
		return bytes;
	}

	inline bool write_stream(IW::IStreamOut* output, const std::span<const std::uint8_t> bytes)
	{
		if (!output) return false;
		std::size_t offset = 0;
		while (offset < bytes.size())
		{
			const auto count = (std::min<std::size_t>)(bytes.size() - offset, MAXDWORD);
			DWORD written{};
			if (!output->Write(bytes.data() + offset, static_cast<DWORD>(count), &written) ||
				written != count)
				return false;
			offset += count;
		}
		return true;
	}

	inline std::optional<libfiles::jpeg_transform> transform_code(const JXFORM_CODE code)
	{
		switch (code)
		{
		case JXFORM_NONE: return libfiles::jpeg_transform::none;
		case JXFORM_FLIP_H: return libfiles::jpeg_transform::flip_horizontal;
		case JXFORM_FLIP_V: return libfiles::jpeg_transform::flip_vertical;
		case JXFORM_TRANSPOSE: return libfiles::jpeg_transform::transpose;
		case JXFORM_TRANSVERSE: return libfiles::jpeg_transform::transverse;
		case JXFORM_ROT_90: return libfiles::jpeg_transform::rotate90;
		case JXFORM_ROT_180: return libfiles::jpeg_transform::rotate180;
		case JXFORM_ROT_270: return libfiles::jpeg_transform::rotate270;
		default: return {};
		}
	}
}
using namespace iw::legacy_detail::source_filejpeg;

inline CLoadJpg::CLoadJpg() :
	_pStatus(IW::CNullStatus::Instance),
	m_bCrop(false),
	_nOrientation(IW::Orientation::TopLeft),
	m_transformcode(JXFORM_NONE)
{
}

inline CLoadJpg::~CLoadJpg() = default;

inline bool CLoadJpg::Read(const CString&, IW::IStreamIn* input, IW::IImageStream* output,
                    IW::IStatus* status)
{
	return iw::imaging::detail::read_libfiles_document(
		libfiles::image_format::jpeg, GetKey(), input, output, status);
}

inline bool CLoadJpg::Write(const CString&, IW::IStreamOut* output, const IW::Image& image,
                     const IW::CodecSettings& settings, IW::IStatus* status)
{
	if (!output || !status || image.IsEmpty() || status->QueryCancel()) return false;
	if (!image.HasMetaData(IW::MetaDataTypes::JPEG_IMAGE))
		return iw::imaging::write_libfiles_document(
			libfiles::image_format::jpeg, output, image, settings, status);

	CJpegTransformation transformation(output, image, status);
	image.IterateMetaData(&transformation);
	return transformation._bSuccess;
}

inline bool CLoadJpg::Write(IW::IStreamOut* output, IW::IStreamIn* input,
                     const IW::Image& image, const IW::CodecSettings&, IW::IStatus* status)
{
	if (!output || !input || !status || status->QueryCancel()) return false;
	const auto bytes = read_stream(input);
	if (bytes.empty()) return false;
	if (m_bCrop)
	{
		status->SetError(L"JPEG coefficient cropping is not supported by the shared transform API.");
		return false;
	}
	const auto transform = transform_code(m_transformcode);
	if (!transform)
	{
		status->SetError(L"Unsupported JPEG coefficient transform.");
		return false;
	}

	libfiles::jpeg_encoder encoder;
	libfiles::jpeg_transformer transformer;
	libfiles::jpeg_transform_options options;
	options.transform = *transform;
	options.policy = libfiles::jpeg_transform_policy::partial_blocks;
	options.maximum_bytes = std::uint64_t{1} << 30;
	options.cancelled = [status] { return status->QueryCancel() != FALSE; };
	options.progress = [status](const std::uint32_t current, const std::uint32_t total)
	{
		status->Progress(static_cast<int>((std::min)(current, static_cast<std::uint32_t>(INT_MAX))),
		                 static_cast<int>((std::min)(total, static_cast<std::uint32_t>(INT_MAX))));
	};
	const auto& camera = image.GetCameraSettings();
	options.density = libfiles::jpeg_density{
		libfiles::jpeg_density_unit::centimetres,
		IW::JfifDensity(camera.XPelsPerMeter, true),
		IW::JfifDensity(camera.YPelsPerMeter, true)};
	const auto result = transformer.transform(bytes, encoder, options);
	if (result.data.empty())
	{
		if (result.cancelled || status->QueryCancel())
			status->SetError(L"JPEG transformation was cancelled.");
		else if (result.over_budget || result.resource_limit)
			status->SetError(L"JPEG transformation exceeds the memory limit.");
		else
			status->SetError(CString(CA2W(result.error.c_str(), CP_UTF8)));
		return false;
	}
	return write_stream(output, result.data);
}

inline bool CLoadJpg::AddMetaDataBlob(const IW::MetaData&)
{
	return true;
}

// ---- FileMetaFile ----
// ImageWalker by Zac Walker
//
// Purpose: Metafile implementation.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#include "LegacyPch.h"
#include "FileMetaFile.h"
#include "DibSurface.h"

#define RECTWIDTH(lprc)     ((lprc)->right - (lprc)->left)
#define RECTHEIGHT(lprc)    ((lprc)->bottom - (lprc)->top)

//
// Aldus Placeable Metafile (APM) definitions
//

#define APM_SIGNATURE 0x9AC6CDD7

#ifndef RC_INVOKED       // RC can't handle #pragmas
#pragma pack(2)
typedef struct tagRECTS
{
	short left;
	short top;
	short right;
	short bottom;
} RECTS, *PRECTS;

typedef struct tagAPMFILEHEADER
{
	DWORD key;
	WORD hmf;
	RECTS bbox;
	WORD inch;
	DWORD reserved;
	WORD checksum;
} APMFILEHEADER, *PAPMFILEHEADER;
#pragma pack()
#endif  /* !RC_INVOKED */


//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

inline CLoadMetaFile::CLoadMetaFile()
{
}

inline CLoadMetaFile::~CLoadMetaFile()
{
}


inline bool CLoadMetaFile::Read(const CString& str, IW::IStreamIn* pStreamIn, IW::IImageStream* pImageOut,
                         IW::IStatus* pStatus)
{
	IW::MetaData data;
	HRESULT hr = data.LoadFromStream(pStreamIn);

	if (FAILED(hr))
		return false;

	return Read(data.GetData(), data.GetDataSize(), pImageOut, pStatus);
}

inline bool CLoadMetaFile::Read(LPCBYTE pByte, DWORD nSize, IW::IImageStream* pImageOut, IW::IStatus* pStatus)
{
	// First check to see if it is an enhanced metafile
	//
	if ((nSize > sizeof(ENHMETAHEADER))
		&& (((ENHMETAHEADER*)pByte)->dSignature == ENHMETA_SIGNATURE))
	{
		return ReadEnhancedMetaFile(pImageOut, pByte, nSize) != 0;
	}
	//
	// Not an enhanced metafile... check for Aldus Placeable Metafile (APM)
	//
	if ((nSize > (sizeof(APMFILEHEADER) + sizeof(METAHEADER))) &&
		*((LPDWORD)pByte) == APM_SIGNATURE)
	{
		return ReadMetaFilePict(pImageOut, pByte, nSize) != 0;
	}
	//
	// Not enhanced or placeable... must be a Windows 3x metafile (we hope)
	//
	if (nSize > sizeof(METAHEADER)) // && (*((LPWORD)pByte)) == 2)
	{
		return ReadMetaFile(pImageOut, pByte, nSize) != 0;
	}

	return FALSE;
}

inline int CLoadMetaFile::ReadMetaFile(IW::IImageStream* pImageOut, LPCBYTE pByte, DWORD nSize)
{
	CRect r(0, 0, 640, 480);
	//HMETAFILE hemf = SetWinMetaFileBits(nSize,
	//	(LPBYTE)pByte, adc, NULL);

	HMETAFILE hmf = SetMetaFileBitsEx(nSize, pByte);

	// We dont know the extent of this meta file?
	//dt._imageInformation = _T("Standard Windows Metafile");

	if (hmf)
	{
		constexpr int cx = 640;
		constexpr int cy = 480;

		IW::DibSurface render;
		IW::ScreenDC dc;

		if (render.Create(nullptr, r))
		{
			render.Play(hmf, cx, cy);
			render.RenderToSurface(pImageOut);
		}

		DeleteMetaFile(hmf);

		// Information
		pImageOut->SetStatistics(GetTitle());
		pImageOut->SetLoaderName(GetKey());

		return TRUE;
	}

	return FALSE;
}

inline int CLoadMetaFile::ReadMetaFilePict(IW::IImageStream* pImageOut, LPCBYTE pByte, DWORD nSize)
{
	CRect r(0, 0, 640, 480);
	IW::ScreenDC dc;
	if (!dc) return FALSE;

	auto papm = (PAPMFILEHEADER)pByte;
	auto pmf = (PMETAHEADER)(pByte + sizeof(APMFILEHEADER)); // Windows MetaFile


	//
	// Set up a METAFILEPICT with MM_ANISOTROPIC (requires xExt and
	// yExt to be in MM_HIMETRIC units).
	//
	// Note: papm->bbox   = bounding rect in metafile units
	//       papm->inch   = # of metafile units per inch
	//       HIMETRICINCH = # of hi-metric units (.01mm) per inch
	//

	// papm->inch is a WORD from the file; zero makes MulDiv answer -1, and an
	// inverted bounding box gives a negative extent directly.
	if (papm->inch == 0)
		return FALSE;

	int xExt = MulDiv(RECTWIDTH(&papm->bbox), 2540, papm->inch);
	int yExt = MulDiv(RECTHEIGHT(&papm->bbox), 2540, papm->inch);

	if (xExt <= 0 || yExt <= 0)
		return FALSE;

	r.top = 0;
	r.left = 0;
	int cx = r.right = MulDiv(xExt, dc.GetDeviceCaps(HORZRES), dc.GetDeviceCaps(HORZSIZE) * 100);
	int cy = r.bottom = MulDiv(yExt, dc.GetDeviceCaps(VERTRES), dc.GetDeviceCaps(VERTSIZE) * 100);

	HMETAFILE hmf = SetMetaFileBitsEx(nSize - sizeof(APMFILEHEADER), (PBYTE)pmf);

	if (r.right < 1) r.right = 1;
	if (r.bottom < 1) r.bottom = 1;
	if (r.right > 2000) r.right = 2000;
	if (r.bottom > 2000) r.bottom = 2000;

	if (hmf)
	{
		IW::DibSurface render;
		IW::ScreenDC renderDC;

		if (render.Create(renderDC, r))
		{
			render.Play(hmf, xExt, yExt);
			render.RenderToSurface(pImageOut);
		}

		DeleteMetaFile(hmf);


		// Information
		CString str;
		str.Format(IW::Text::ALDUS_FMT, cx, cy);

		pImageOut->SetStatistics(str);
		pImageOut->SetLoaderName(GetKey());

		IW::CameraSettings settings;
		settings.OriginalImageSize.cx = cx;
		settings.OriginalImageSize.cy = cy;
		pImageOut->SetCameraSettings(settings);
		pImageOut->Flush();

		return TRUE;
	}

	return FALSE;
}

inline int CLoadMetaFile::ReadEnhancedMetaFile(IW::IImageStream* pImageOut, LPCBYTE pByte, DWORD nSize)
{
	CRect r(0, 0, 640, 480);

	ENHMETAHEADER emh;
	HENHMETAFILE hemf = nullptr;

	hemf = SetEnhMetaFileBits(nSize, pByte);

	if ((hemf) && GetEnhMetaFileHeader(hemf, sizeof(emh), &emh))
	{
		// szlMillimeters comes from the file and divides.
		if (emh.szlMillimeters.cx > 0 && emh.szlMillimeters.cy > 0)
		{
			r.left = 0;
			r.top = 0;
			r.right = MulDiv(RECTWIDTH(&emh.rclFrame), emh.szlDevice.cx, emh.szlMillimeters.cx * 100);
			r.bottom = MulDiv(RECTHEIGHT(&emh.rclFrame), emh.szlDevice.cy, emh.szlMillimeters.cy * 100);
		}
	}

	if (r.right < 1) r.right = 1;
	if (r.bottom < 1) r.bottom = 1;
	if (r.right > 2000) r.right = 2000;
	if (r.bottom > 2000) r.bottom = 2000;

	CSize size(r.Size());

	// Information
	CString str;
	str.Format(IW::Text::EMF_FMT, size.cx, size.cy);

	pImageOut->SetStatistics(str);
	pImageOut->SetLoaderName(GetKey());

	IW::CameraSettings settings;
	settings.OriginalImageSize.cx = size.cx;
	settings.OriginalImageSize.cy = size.cy;
	pImageOut->SetCameraSettings(settings);

	if (hemf)
	{
		IW::DibSurface render;
		IW::ScreenDC dc;

		if (render.Create(dc, r))
		{
			render.Play(hemf);
			render.RenderToSurface(pImageOut);
		}

		DeleteEnhMetaFile(hemf);

		return TRUE;
	}

	return FALSE;
}

inline bool CLoadMetaFile::Write(
	const CString& str,
	IW::IStreamOut* pStreamOut,
	const IW::Image& imageIn,

	const IW::CodecSettings& settings, IW::IStatus* pStatus)
{
	return FALSE;
}

// ---- FileModern ----
#include <iw/imaging/Codec.h>
#include <iw/imaging/WebPCodec.h>
#include <iw/imaging/Diagnostics.h>
#include <iw/legacy/NativeCodec.h>
#include <iw/legacy/NativeCodec.h>
#include "LegacyPch.h"
#include "FileModern.h"

namespace iw::legacy_detail::source_filemodern
{
	inline bool read_modern(const iw::imaging::PixelDecoder decoder, IW::IStreamIn* input, IW::IImageStream* output,
	                 IW::IStatus* status, const CString& key, const CString& title)
	{
		const int size = input->GetFileSize();
		if (size <= 0 || size > 512 * 1024 * 1024)
		{
			status->SetError(L"The encoded image is empty or exceeds the input limit.");
			return false;
		}
		input->Seek(IW::IStreamCommon::eBegin, 0);
		DWORD read{};
		if (decoder == iw::imaging::PixelDecoder::webp)
		{
			// The loader is also reached by a RIFF header word, which AVI and WAV files share.
			std::uint8_t header[12]{};
			if (!input->Read(header, sizeof(header), &read) || !iw::imaging::has_webp_signature({header, read}))
			{
				status->SetError(L"The file does not carry a WebP signature.");
				return false;
			}
			input->Seek(IW::IStreamCommon::eBegin, 0);
		}
		std::vector<std::uint8_t> bytes(static_cast<size_t>(size));
		if (!input->Read(bytes.data(), static_cast<DWORD>(bytes.size()), &read) || read != bytes.size())
		{
			status->SetError(L"The encoded image could not be read completely.");
			return false;
		}
		iw::imaging::DecodeOptions options;
		const auto thumbnail = output->GetThumbnailSize();
		options.maximumWidth = thumbnail.cx;
		options.maximumHeight = thumbnail.cy;
		options.thumbnail = thumbnail.cx > 0 && thumbnail.cy > 0;
		options.applyOrientation = output->WantUpright();
		options.cancelled = [status] { return status->QueryCancel(); };
		options.progress = [status](int current, int total) { status->Progress(current, total); };
		auto decoded = iw::imaging::decode_with(decoder, bytes, {}, options);
		if (!decoded)
		{
			status->SetError(decoded.message.c_str());
			return false;
		}
		const auto& image = decoded.image;
		IW::CameraSettings settings;
		settings.OriginalImageSize = CSize(image.originalWidth, image.originalHeight);
		settings.OriginalBpp = IW::PixelFormat::PF32Alpha;
		settings.Orientation = decoded.orientation;
		output->SetCameraSettings(settings);
		output->SetLoaderName(key);
		output->SetStatistics(title);
		output->CreatePage(CRect(0, 0, image.width, image.height), IW::PixelFormat::PF32Alpha, true);
		for (int y = 0; y < image.height; ++y)
		{
			if (status->QueryCancel())
			{
				status->SetError(L"Image decoding was cancelled.");
				return false;
			}
			output->SetBitmap(y, image.pixels.data() + static_cast<size_t>(y) * image.width);
		}
		output->Flush();
		return true;
	}
}
using namespace iw::legacy_detail::source_filemodern;

inline bool CLoadWebP::Read(const CString&, IW::IStreamIn* input, IW::IImageStream* output, IW::IStatus* status)
{
	return read_modern(iw::imaging::PixelDecoder::webp, input, output, status, GetKey(), GetTitle());
}

inline bool CLoadWebP::Write(const CString&, IW::IStreamOut* output, const IW::Image& image,
                     const IW::CodecSettings& settings, IW::IStatus* status)
{
	if (image.GetPageCount() != 1)
	{
		status->SetError(L"Static WebP output requires exactly one image frame.");
		return false;
	}
	if (status->QueryCancel())
	{
		status->SetError(L"Image encoding was cancelled.");
		return false;
	}
	iw::imaging::SaveOptions options;
	options.quality = settings.JpegQuality;
	const auto encoded = iw::webp::encode(iw::imaging::image_pixels(image), options);
	if (encoded.empty() || encoded.size() > MAXDWORD)
	{
		status->SetError(L"The image could not be encoded as WebP.");
		return false;
	}
	if (!image.Blobs.empty())
	{
		constexpr auto warning = L"WebP output does not carry source metadata.";
		status->SetWarning(warning);
		iw::imaging::report_diagnostic(warning);
	}
	DWORD written{};
	if (!output->Write(encoded.data(), static_cast<DWORD>(encoded.size()), &written) || written != encoded.size())
	{
		status->SetError(L"The WebP output could not be written completely.");
		return false;
	}
	return true;
}

inline CString CLoadWindows::_GetExtensionList()
{
	CString result;
	for (const auto& extension : iw::imaging::windows_image_extensions())
	{
		if (extension.size() < 2 || extension.front() != L'.') continue;
		if (!result.IsEmpty()) result += L",";
		result += extension.c_str() + 1;
	}
	return result;
}

inline bool CLoadWindows::Read(const CString&, IW::IStreamIn* input, IW::IImageStream* output, IW::IStatus* status)
{
	return read_modern(iw::imaging::PixelDecoder::wic, input, output, status, GetKey(), GetTitle());
}

// ---- FilePcx ----
// ImageWalker by Zac Walker
//
// Purpose: PCX native image-stream adapter to libfiles.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.

#include "LegacyPch.h"
#include "FilePcx.h"
#include "LibfilesDocument.h"

inline CLoadPcx::CLoadPcx() = default;
inline CLoadPcx::~CLoadPcx() = default;

inline bool CLoadPcx::Read(const CString&, IW::IStreamIn* input, IW::IImageStream* output, IW::IStatus* status)
{
	return iw::imaging::detail::read_libfiles_document(libfiles::image_format::pcx,
		GetKey(), input, output, status);
}

inline bool CLoadPcx::Write(const CString&, IW::IStreamOut*, const IW::Image&,
	const IW::CodecSettings&, IW::IStatus*)
{
	return false;
}

// ---- FilePng ----
// ImageWalker by Zac Walker
//
// Purpose: Historical PNG boundary adapter. libfiles owns PNG parsing,
// metadata and encoding; this file preserves the old stream/status contract.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.

#include "LegacyPch.h"
#include "FilePng.h"
#include "LibfilesDocument.h"
#include <iw/legacy/NativeCodec.h>

#ifdef COMPILE_PNG

inline CLoadPng::CLoadPng() : m_png_ptr(nullptr), m_info_ptr(nullptr) {}
inline CLoadPng::~CLoadPng() = default;

inline bool CLoadPng::IsPng(LPCBYTE bytes)
{
	static constexpr BYTE signature[] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
	return bytes && std::memcmp(bytes, signature, sizeof(signature)) == 0;
}

inline void __cdecl CLoadPng::read_data(png_struct*, png_byte*, png_size_t) {}
inline void __cdecl CLoadPng::user_error_fn(png_structp, png_const_charp) {}
inline void __cdecl CLoadPng::user_warning_fn(png_structp, png_const_charp) {}
inline void __cdecl CLoadPng::write_data(png_struct*, png_byte*, png_size_t) {}

inline bool CLoadPng::Read(const CString&, IW::IStreamIn* input, IW::IImageStream* output,
                    IW::IStatus* status)
{
	return iw::imaging::detail::read_libfiles_document(
		libfiles::image_format::png, GetKey(), input, output, status);
}

inline bool CLoadPng::Write(IW::IStreamOut* output, IW::IStreamIn*, const IW::Image& image,
                     const IW::CodecSettings& settings, IW::IStatus* status)
{
	return Write(GetKey(), output, image, settings, status);
}

inline bool CLoadPng::WriteTranscode(IW::IStreamOut* output, IW::IStreamIn*,
                              const IW::Image& image, const IW::CodecSettings& settings,
                              IW::IStatus* status)
{
	return Write(GetKey(), output, image, settings, status);
}

inline bool CLoadPng::Write(const CString&, IW::IStreamOut* output, const IW::Image& image,
                     const IW::CodecSettings& settings, IW::IStatus* status)
{
	if (!output || !status || image.IsEmpty() || status->QueryCancel()) return false;
	return iw::imaging::write_libfiles_document(
		libfiles::image_format::png, output, image, settings, status);
}

#endif // COMPILE_PNG

// ---- FilePsd ----
// ImageWalker by Zac Walker
//
// Purpose: PSD native image-stream adapter to libfiles.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.

#include "LegacyPch.h"
#include "FilePsd.h"
#include "LibfilesDocument.h"

#ifdef COMPILE_PSD
inline CLoadPsd::CLoadPsd() = default;
inline CLoadPsd::~CLoadPsd() = default;

inline bool CLoadPsd::Read(const CString&, IW::IStreamIn* input, IW::IImageStream* output, IW::IStatus* status)
{
	return iw::imaging::detail::read_libfiles_document(libfiles::image_format::psd,
		GetKey(), input, output, status);
}

inline bool CLoadPsd::Write(const CString&, IW::IStreamOut*, const IW::Image&, const IW::CodecSettings&, IW::IStatus*)
{
	return false;
}

inline bool CLoadPsd::AddMetaDataBlob(IW::MetaData&) { return true; }
#endif

// ---- FileTiff ----
// ImageWalker by Zac Walker
//
// Purpose: Historical TIFF boundary adapter. libfiles owns TIFF parsing,
// metadata, multipage storage and encoding.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.

#include "LegacyPch.h"
#include "FileTiff.h"
#include "LibfilesDocument.h"
#include <iw/legacy/NativeCodec.h>

#ifdef COMPILE_TIFF

inline CLoadTiff::CLoadTiff() : m_hTiffOut(nullptr) {}
inline CLoadTiff::~CLoadTiff() = default;

inline bool CLoadTiff::Read(const CString&, IW::IStreamIn* input, IW::IImageStream* output,
                     IW::IStatus* status)
{
	return iw::imaging::detail::read_libfiles_document(
		libfiles::image_format::tiff, GetKey(), input, output, status);
}

inline bool CLoadTiff::Write(const CString&, IW::IStreamOut* output, const IW::Image& image,
                      const IW::CodecSettings& settings, IW::IStatus* status)
{
	if (!output || !status || image.IsEmpty() || status->QueryCancel()) return false;
	return iw::imaging::write_libfiles_document(
		libfiles::image_format::tiff, output, image, settings, status);
}

inline bool CLoadTiff::AddMetaDataBlob(const IW::MetaData&)
{
	return true;
}

#endif // COMPILE_TIFF

// ---- FileWpg ----
// ImageWalker by Zac Walker
//
// Purpose: WPG native image-stream adapter to libfiles.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.

#include "LegacyPch.h"
#include "FileWpg.h"
#include "LibfilesDocument.h"

inline CLoadWpg::CLoadWpg() = default;
inline CLoadWpg::~CLoadWpg() = default;

inline bool CLoadWpg::Read(const CString&, IW::IStreamIn* input, IW::IImageStream* output, IW::IStatus* status)
{
	return iw::imaging::detail::read_libfiles_document(libfiles::image_format::wpg,
		GetKey(), input, output, status);
}

inline bool CLoadWpg::Write(const CString&, IW::IStreamOut*, const IW::Image&, const IW::CodecSettings&, IW::IStatus*)
{
	return false;
}

// ---- Imaging ----
// ImageWalker by Zac Walker
//
// Purpose: IW::Image implementation - page creation, cloning, histograms,
//          DIB and clipboard conversion, orientation.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

//
// Image : implementation file
//
//
//////////////////////////////////////////////////////////////////////

#include "LegacyPch.h"

#include "ImagingStreams.h"

#include "FileFormatAny.h"
#include "FileJpeg.h"
#include "MetadataExif.h"
#include "DibSurface.h"


//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

inline IW::Image IW::CreatePreview(const IW::Image& imageIn, CSize sizeIn, bool bHighQuality)
{
	constexpr int nDiv = 0x8000;
	CSize sizeAll = imageIn.GetBoundingRect().Size();

	int sx = MulDiv(sizeIn.cx, nDiv, sizeAll.cx);
	int sy = MulDiv(sizeIn.cy, nDiv, sizeAll.cy);

	if (sy < sx)
	{
		sizeIn.cx = MulDiv(sizeIn.cy, sizeAll.cx, sizeAll.cy);
	}
	else
	{
		sizeIn.cy = MulDiv(sizeIn.cx, sizeAll.cy, sizeAll.cx);
	}

	IW::Image imageOut;

	// The stream writes its last band from its destructor, so it has to be gone
	// before the image is returned.
	if (bHighQuality)
	{
		ImageStreamThumbnailCore<CNull, true> thumbnailStream(imageOut, sizeIn);
		IterateImage(imageIn, thumbnailStream, IW::CNullStatus::Instance);
	}
	else
	{
		ImageStreamThumbnailCore<CNull> thumbnailStream(imageOut, sizeIn);
		IterateImage(imageIn, thumbnailStream, IW::CNullStatus::Instance);
	}

	return imageOut;
}


inline void IW::Image::GetHistogram(IW::Histogram& histogram) const
{
	for (auto pageIn = Pages.begin(); pageIn != Pages.end(); ++pageIn)
	{
		const int nHeight = pageIn->GetHeight();
		const int nWidth = pageIn->GetWidth();

		IW::ConstIImageSurfaceLockPtr pLockIn = pageIn->GetSurfaceLock();
		IW::CBuffer<COLORREF> pBuffer(nWidth);

		for (int y = 0; y < nHeight; y++)
		{
			pLockIn->RenderLine(pBuffer, y, 0, nWidth);

			for (int x = 0; x < nWidth; x++)
			{
				histogram.AddColor(pBuffer[x]);
			}
		}
	}

	histogram.CalcMax();
}


inline void IW::Image::FixOrientation(IW::IStatus* pStatus, bool enabled)
{
	const int orientation = _settings.Orientation;
	if (enabled && orientation > IW::Orientation::TopLeft && orientation <= IW::Orientation::LeftBottom)
	{
		IW::Image imageTemp;
		bool transformed = false;

		if (HasMetaData(MetaDataTypes::JPEG_IMAGE))
		{
			const auto code = jtransform_compose_exif_orientation(JXFORM_NONE, orientation);
			IW::ImageStreamCore<IW::IImageStream> imageStream(imageTemp, false);
			CJpegTransformation trans(code, &imageStream, *this, pStatus);
			IW::CameraSettings settings = trans._imageIn.GetCameraSettings();
			settings.Orientation = IW::Orientation::TopLeft;
			trans._imageIn.SetCameraSettings(settings);
			IterateMetaData(&trans);
			transformed = trans._bSuccess;
		}
		else
		{
			switch (orientation)
			{
			case IW::Orientation::TopRight: transformed = MirrorLR(*this, imageTemp, pStatus); break;
			case IW::Orientation::BottomRight: transformed = Rotate180(*this, imageTemp, pStatus); break;
			case IW::Orientation::BottomLeft: transformed = MirrorTB(*this, imageTemp, pStatus); break;
			case IW::Orientation::RightTop: transformed = Rotate90(*this, imageTemp, pStatus); break;
			case IW::Orientation::LeftBottom: transformed = Rotate270(*this, imageTemp, pStatus); break;
			default:
			{
				IW::Image rotated;
				if (Rotate90(*this, rotated, pStatus))
					transformed = orientation == IW::Orientation::LeftTop
						? MirrorLR(rotated, imageTemp, pStatus) : MirrorTB(rotated, imageTemp, pStatus);
				break;
			}
			}
			if (transformed)
			{
				IterateImageMetaData(*this, imageTemp, pStatus);
				if (orientation >= IW::Orientation::LeftTop)
					std::swap(imageTemp._settings.OriginalImageSize.cx, imageTemp._settings.OriginalImageSize.cy);
				if (imageTemp.HasMetaData(MetaDataTypes::PROFILE_EXIF))
				{
					auto exif = imageTemp.GetMetaData(MetaDataTypes::PROFILE_EXIF).Clone();
					Exif::SetOrientation(exif.GetData(), exif.GetDataSize(), IW::Orientation::TopLeft);
					const auto size = imageTemp.GetBoundingRect().Size();
					Exif::FixExif(exif.GetData(), exif.GetDataSize(), size.cx, size.cy);
					imageTemp.SetMetaData(exif);
				}
			}
		}

		if (transformed && !imageTemp.IsEmpty())
		{
			imageTemp._settings.Orientation = IW::Orientation::TopLeft;
			Copy(imageTemp);
		}
	}
};


inline bool IW::Image::Copy(const BITMAPINFOHEADER* pBmpFileHdr, int nSize)
{
	// Read the file header to get the file size and to
	// find out where the bits start in the
	auto pByte = (LPBYTE)pBmpFileHdr;


	// Check that we got a real Windows DIB
	if (pBmpFileHdr->biSize != sizeof(BITMAPINFOHEADER))
	{
		return FALSE;
	}

	if (nSize == -1)
		nSize = pBmpFileHdr->biSizeImage;

	// I only want single plain headers
	if (pBmpFileHdr->biPlanes != 1)
	{
		return FALSE;
	}

	IW::PixelFormat pf = IW::PixelFormat::FromBpp(pBmpFileHdr->biBitCount);

	auto pRgb = (LPRGBQUAD)(pByte + static_cast<WORD>(pBmpFileHdr->biSize));
	UINT nColors = pBmpFileHdr->biClrUsed;
	const UINT nMaxColors = pf.NumberOfPaletteEntries();

	if (nColors == 0 || nColors > nMaxColors)
	{
		nColors = nMaxColors;
	}

	assert(pBmpFileHdr->biHeight != 0);

	// We really need to calculate the bfOffBits
	// ourselves as if it is corrupt a GPF may occur!
	auto pBytes = (LPCBYTE)(pRgb + nColors);

	// Do this in 64-bit: truncating to int lets a huge biClrUsed come back positive.
	const ptrdiff_t nOffset = pBytes - pByte;

	if (nOffset < 0 || nOffset > nSize)
	{
		return false;
	}

	const int nBytesMax = static_cast<int>(nSize - nOffset);

	return Copy((const BITMAPINFO*)pBmpFileHdr, pBytes, nBytesMax);
}


inline bool IW::Image::Copy(const BITMAPINFO* pInfo, LPCBYTE pBytes, int nBytesMax)
{
	bool bSuccess = false;


	// Check that we got a real Windows image
	if (pInfo->bmiHeader.biSize != sizeof(BITMAPINFOHEADER))
		return false;

	const int nWidth = pInfo->bmiHeader.biWidth;
	const int nHeight = abs(pInfo->bmiHeader.biHeight);


	// If its a complicated image use
	// SetDIBits
	if (pInfo->bmiHeader.biPlanes != 1 ||
		pInfo->bmiHeader.biCompression != BI_RGB)
	{
		const CRect rcCreate(0, 0, nWidth, nHeight);

		DibSurface render;

		if (render.Create(nullptr, rcCreate))
		{
			HDC dc = render.dc();

			StretchDIBits(dc,
			              0, 0,
			              nWidth, nHeight,
			              0, 0,
			              nWidth, nHeight,
			              pBytes,
			              pInfo, // BITMAPINFO
			              DIB_RGB_COLORS, // Options
			              SRCCOPY); // Raster operation code (ROP)


			render.RenderToSurface(*this);
			bSuccess = true;
		}
	}
	else
	{
		const IW::PixelFormat pf = IW::PixelFormat::FromBpp(pInfo->bmiHeader.biBitCount);

		IW::Page page = CreatePage(nWidth, nHeight, pf);
		const int nStorageWidth = CalcStorageWidth(nWidth, pf);
		int nColors = pInfo->bmiHeader.biClrUsed;
		const int nMaxColors = pf.NumberOfPaletteEntries();

		if (nColors <= 0 || nColors > nMaxColors)
		{
			nColors = nMaxColors;
		}

		auto pRgb = (LPCCOLORREF)((LPSTR)pInfo + static_cast<WORD>(pInfo->bmiHeader.biSize));

		if (nColors && pf.HasPalette())
			IW::MemCopy(page.GetPalette(), pRgb, nColors * sizeof(COLORREF));

		const int nTotalBytes = nStorageWidth * nHeight;

		if (nBytesMax == -1)
		{
			nBytesMax = nTotalBytes;
		}
		else
		{
			nBytesMax = Min(nBytesMax, nTotalBytes);
		}

		// A short or corrupt source would otherwise leave the tail as raw heap.
		if (nBytesMax < nTotalBytes)
			IW::MemZero(page.GetBitmap(), nTotalBytes);

		if (nBytesMax > 0)
		{
			if (pInfo->bmiHeader.biHeight < 0)
			{
				// Top-down source into bottom-up storage: copy row by row.
				const int nRows = Min(nHeight, nBytesMax / nStorageWidth);

				for (int y = 0; y < nRows; ++y)
					IW::MemCopy(page.GetBitmapLine(y), pBytes + (nStorageWidth * y), nStorageWidth);
			}
			else
			{
				IW::MemCopy(page.GetBitmap(), pBytes, nBytesMax);
			}
		}

		bSuccess = true;
	}


	SetXPelsPerMeter(pInfo->bmiHeader.biXPelsPerMeter);
	SetYPelsPerMeter(pInfo->bmiHeader.biYPelsPerMeter);


	return bSuccess;
}


inline bool IW::Image::Copy(HGLOBAL hglb)
{
	bool bSuccess = false;

	const SIZE_T cb = ::GlobalSize(hglb);
	auto pBytes = static_cast<LPBYTE>(GlobalLock(hglb));

	if (pBytes != nullptr)
	{
		// Image Header
		auto pInfo = (BITMAPINFO*)pBytes;

		if (cb >= sizeof(BITMAPINFOHEADER) &&
			pInfo->bmiHeader.biSize >= sizeof(BITMAPINFOHEADER) &&
			pInfo->bmiHeader.biSize <= cb)
		{
			// Check that we got a real Windows image
			// I only want single plain headers
			const IW::PixelFormat pf = IW::PixelFormat::FromBpp(pInfo->bmiHeader.biBitCount);
			SIZE_T nColors = pInfo->bmiHeader.biClrUsed;
			const SIZE_T nMaxColors = pf.NumberOfPaletteEntries();

			if (nColors == 0 || nColors > nMaxColors)
			{
				nColors = nMaxColors;
			}

			if (pInfo->bmiHeader.biCompression == BI_BITFIELDS)
			{
				nColors = 3;
			}

			const SIZE_T nOffset = pInfo->bmiHeader.biSize + (nColors * 4);

			if (nOffset < cb)
			{
				auto pBitmap = pBytes + nOffset;
				const SIZE_T nBytesMax = cb - nOffset;

				bSuccess = Copy(pInfo, pBitmap, static_cast<int>(nBytesMax > INT_MAX ? INT_MAX : nBytesMax));
			}
		}

		GlobalUnlock(hglb);
	}

	return bSuccess;
}


inline HGLOBAL IW::Image::CopyToHandle() const
{
	HANDLE hCopy;

	if (IsEmpty())
		return nullptr;

	IW::Page page = GetFirstPage();

	// How large is this image
	IW::PixelFormat pf = page.GetPixelFormat();
	int cx = page.GetWidth();
	int cy = page.GetHeight();
	int nStorageWidth = CalcStorageWidth(cx, pf);
	int nSizeImage = nStorageWidth * cy;
	IW::CImageBitmapInfoHeader bmi(page);
	const auto* info = static_cast<LPBITMAPINFO>(bmi);
	const int nColors = info->bmiHeader.biCompression == BI_BITFIELDS ? 3 :
		static_cast<int>(info->bmiHeader.biClrUsed);
	const int nSizeHeader = sizeof(BITMAPINFOHEADER) + nColors * sizeof(RGBQUAD);


	// Alloc memory
	hCopy = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, nSizeImage + nSizeHeader);

	if (hCopy == nullptr)
		throw std::bad_alloc();

	auto lpCopy = static_cast<LPBYTE>(GlobalLock(hCopy));

	if (lpCopy == nullptr)
	{
		GlobalFree(hCopy);
		throw std::bad_alloc();
	}

	IW::MemCopy(lpCopy, info, nSizeHeader);
	IW::MemCopy(lpCopy + nSizeHeader, page.GetBitmap(), nSizeImage);

	GlobalUnlock(hCopy);

	return hCopy;
}

inline void IW::Image::AddImageData(IW::IStreamIn* pStreamIn, DWORD dwType)
{
	pStreamIn->Seek(IW::IStreamCommon::eBegin, 0);

	IW::MetaData data(dwType, pStreamIn);

	for (auto i = Blobs.begin(); i != Blobs.end(); ++i)
	{
		if (i->IsType(dwType))
		{
			*i = data;
			return;
		}
	}

	SetMetaData(data);
}

inline bool IW::Image::Render(IW::Image& imageOut) const
{
	const CRect rc = GetBoundingRect();

	DibSurface render;
	render.Create(nullptr, rc);
	render.Fill(RGB(0, 0, 0));

	for (auto pageIn = Pages.begin(); pageIn != Pages.end(); ++pageIn)
	{
		IW::Page pageOut = imageOut.CreatePage(rc, IW::PixelFormat::PF32);

		if (pageIn->GetDisposalMethod() == 2)
		{
			render.Fill(RGB(0, 0, 0));
		}

		// Render to surface then copy to destination
		render.DrawImage(*pageIn, 0, 0);
		render.RenderToSurface(pageOut, 0, 0);

		if (pageIn->GetFlags() & IW::PageFlags::HasTimeDelay)
		{
			pageOut.SetTimeDelay(pageIn->GetTimeDelay());
		}
	}

	return true;
}


inline bool IW::Crop(const IW::Image& imageIn, IW::Image& imageOut, const CRect& rectCrop, IW::IStatus* pStatus)
{
	const CRect rcBounding = imageIn.GetBoundingRect();

	IW::CBuffer<COLORREF> pLine(rcBounding.right - rcBounding.left);

	for (auto pageIn = imageIn.Pages.begin(); pageIn != imageIn.Pages.end(); ++pageIn)
	{
		CRect rcOrg = pageIn->GetPageRect();

		CRect rc;
		if (IntersectRect(&rc, &rectCrop, &rcOrg))
		{
			IW::PixelFormat pf = pageIn->GetPixelFormat();
			IW::Page pageOut = imageOut.CreatePage(rc, pf);

			const int nPaletteEntries = pf.NumberOfPaletteEntries();

			if (nPaletteEntries != 0)
			{
				IW::MemCopy(pageOut.GetPalette(), pageIn->GetPalette(), nPaletteEntries * sizeof(RGBQUAD));
			}

			const int nWidth = rc.right - rc.left;
			const int nHeight = rc.bottom - rc.top;
			const int nStartX = rc.left - rcOrg.left;
			const int nStartY = rc.top - rcOrg.top;

			IW::IImageSurfaceLockPtr pLockOut = pageOut.GetSurfaceLock();
			IW::ConstIImageSurfaceLockPtr pLockIn = pageIn->GetSurfaceLock();

			for (int y = 0; y < nHeight; y++)
			{
				pLockIn->GetLine(pLine, y + nStartY, nStartX, nWidth);
				pLockOut->SetLine(pLine, y, 0, nWidth);
			}

			pageOut.CopyExtraInfo(*pageIn);
		}
	}

	imageOut.Normalize();

	return true;
}


inline bool IW::MirrorTB(const IW::Image& imageIn, IW::Image& imageOut, IW::IStatus* pStatus)
{
	for (auto pageIn = imageIn.Pages.begin(); pageIn != imageIn.Pages.end(); ++pageIn)
	{
		IW::Page pageOut = imageOut.CreatePage(*pageIn);

		const int nHeight = pageIn->GetHeight();
		const int nStorageWidth = pageIn->GetStorageWidth();

		for (int y = 0; y < nHeight; y++)
		{
			int y1 = nHeight - (y + 1);
			IW::MemCopy(pageOut.GetBitmapLine(y1), pageIn->GetBitmapLine(y), nStorageWidth);
		}

		pageOut.CopyExtraInfo(*pageIn);
	}

	imageOut.Normalize();

	return true;
}


inline bool IW::MirrorLR(const IW::Image& imageIn, IW::Image& imageOut, IW::IStatus* pStatus)
{
	for (auto pageIn = imageIn.Pages.begin(); pageIn != imageIn.Pages.end(); ++pageIn)
	{
		IW::Page pageOut = imageOut.CreatePage(*pageIn);

		const int nHeight = pageIn->GetHeight();
		const int nWidth = pageIn->GetWidth();

		IW::ConstIImageSurfaceLockPtr pLockIn = pageIn->GetSurfaceLock();
		IW::IImageSurfaceLockPtr pLockOut = pageOut.GetSurfaceLock();

		// GetLine and SetLine agree on what a value means for every format --
		// indices for the palettised ones, 888 for the rest -- so the mirror is a
		// reverse of the row rather than a pixel-at-a-time swap.
		IW::CBuffer<COLORREF> lineBuffer(nWidth);

		for (int y = 0; y < nHeight; y++)
		{
			pLockIn->GetLine(lineBuffer, y, 0, nWidth);
			std::reverse(lineBuffer.data(), lineBuffer.data() + nWidth);
			pLockOut->SetLine(lineBuffer, y, 0, nWidth);
		}

		pageOut.CopyExtraInfo(*pageIn);
	}

	imageOut.Normalize();

	return true;
}


inline bool IW::GrayScale(const IW::Image& imageIn, IW::Image& imageOut, IW::IStatus* pStatus)
{
	for (auto pageIn = imageIn.Pages.begin(); pageIn != imageIn.Pages.end(); ++pageIn)
	{
		CRect rc = pageIn->GetPageRect();
		IW::Page pageOut = imageOut.CreatePage(rc, IW::PixelFormat::PF8GrayScale);

		const int nHeight = pageIn->GetHeight();
		const int nWidth = pageIn->GetWidth();

		int y, x;
		unsigned c;

		IW::CBuffer<COLORREF> pBuffer(nWidth);

		IW::ConstIImageSurfaceLockPtr pLockIn = pageIn->GetSurfaceLock();

		for (y = 0; y < nHeight; y++)
		{
			pLockIn->RenderLine(pBuffer, y, 0, nWidth);
			LPBYTE pLineOut = pageOut.GetBitmapLine(y);

			for (x = 0; x < nWidth; x++)
			{
				c = pBuffer[x];
				pLineOut[x] = (IW::GetR(c) + IW::GetG(c) + IW::GetB(c)) / 3;
			}
		}

		pageOut.CopyExtraInfo(*pageIn);
	}

	imageOut.Normalize();

	return true;
}


inline bool IW::Dither(const IW::Image& imageIn, IW::Image& imageOut, IW::IStatus* pStatus)
{
	for (auto pageIn = imageIn.Pages.begin(); pageIn != imageIn.Pages.end(); ++pageIn)
	{
		CRect rc = pageIn->GetPageRect();
		IW::Page pageOut = imageOut.CreatePage(rc, IW::PixelFormat::PF1);

		const int nHeight = pageIn->GetHeight();
		const int nWidth = pageIn->GetWidth();

		int y, x;
		unsigned c;

		IW::CBuffer<COLORREF> pBuffer(nWidth);
		IW::ConstIImageSurfaceLockPtr pLockIn = pageIn->GetSurfaceLock();
		IW::IImageSurfaceLockPtr pLockOut = pageOut.GetSurfaceLock();

		for (y = 0; y < nHeight; y++)
		{
			pLockIn->RenderLine(pBuffer, y, 0, nWidth);

			for (x = 0; x < nWidth; x++)
			{
				c = pBuffer[x];
				pBuffer[x] = (IW::GetR(c) + IW::GetG(c) + IW::GetB(c)) / 3;
			}


			// Multi-Level Ordered-Dithering by Kenny Hoff (Oct. 12, 1995)
			constexpr int NumRows = 4;
			constexpr int NumCols = 4;
			constexpr int NumIntensityLevels = 2;
			constexpr int NumRowsLessOne = (NumRows - 1);
			constexpr int NumColsLessOne = (NumCols - 1);
			constexpr int RowsXCols = (NumRows * NumCols);
			constexpr int MaxIntensityVal = 255;
			constexpr int MaxDitherIntensityVal = (NumRows * NumCols * (NumIntensityLevels - 1));

			int DitherMatrix[NumRows][NumCols] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};

			unsigned char Intensity[NumIntensityLevels] = {1, 0}; // 2 LEVELS B/W
			//unsigned char Intensity[NumIntensityLevels] = { 0,255 };                       // 2 LEVELS
			//unsigned char Intensity[NumIntensityLevels] = { 0,127,255 };                   // 3 LEVELS
			//unsigned char Intensity[NumIntensityLevels] = { 0,85,170,255 };                // 4 LEVELS
			//unsigned char Intensity[NumIntensityLevels] = { 0,63,127,191,255 };            // 5 LEVELS
			//unsigned char Intensity[NumIntensityLevels] = { 0,51,102,153,204,255 };        // 6 LEVELS
			//unsigned char Intensity[NumIntensityLevels] = { 0,42,85,127,170,213,255 };     // 7 LEVELS
			//unsigned char Intensity[NumIntensityLevels] = { 0,36,73,109,145,182,219,255 }; // 8 LEVELS
			int DitherIntensity, DitherMatrixIntensity, Offset, DeviceIntensity;

			for (x = 0; x < nWidth; x++)
			{
				DeviceIntensity = pBuffer[x];
				DitherIntensity = DeviceIntensity * MaxDitherIntensityVal / MaxIntensityVal;
				DitherMatrixIntensity = DitherIntensity % RowsXCols;
				Offset = DitherIntensity / RowsXCols;

				if (DitherMatrix[y & NumRowsLessOne][x & NumColsLessOne] < DitherMatrixIntensity)
				{
					pBuffer[x] = Intensity[1 + Offset];
				}
				else
				{
					pBuffer[x] = Intensity[0 + Offset];
				}
			}

			pLockOut->SetLine(pBuffer, y, 0, nWidth);
		}


		LPCOLORREF pRGBOut = pageOut.GetPalette();
		pRGBOut[0] = RGB(255, 255, 255);
		pRGBOut[1] = RGB(0, 0, 0);

		pageOut.CopyExtraInfo(*pageIn);
	}

	imageOut.Normalize();

	return true;
}

inline bool Filter(const IW::Page& pageIn, IW::Image& imageOut, long* kernel, long Ksize, long Kfactor, long Koffset,
                   IW::IStatus* pStatus)
{
	long k2 = Ksize / 2;
	long kmax = Ksize - k2;
	long r, g, b, a, i;
	COLORREF c;
	IW::PixelFormat pf = pageIn.GetPixelFormat();
	int nWidth = pageIn.GetWidth();
	int nHeight = pageIn.GetHeight();
	int x, y, j, k;

	CRect rcOrg = pageIn.GetPageRect();
	CRect rc = rcOrg;

	const bool hasAlpha = pf.HasAlpha() || (pageIn.GetFlags() & IW::PageFlags::HasTransparent);
	IW::Page pageOut = imageOut.CreatePage(rc, hasAlpha ? IW::PixelFormat::PF32Alpha : IW::PixelFormat::PF24);
	IW::ConstIImageSurfaceLockPtr pLockIn = pageIn.GetSurfaceLock();
	IW::IImageSurfaceLockPtr pLockOut = pageOut.GetSurfaceLock();

	IW::CBuffer<COLORREF> pLine(nWidth);
	const bool hasInterior = Ksize <= nWidth && Ksize <= nHeight;
	IW::CBuffer<COLORREF> lines(hasInterior ? static_cast<size_t>(Ksize) * nWidth : 0);
	const auto readLine = [&](COLORREF* line, int row)
	{
		if (!hasAlpha) pLockIn->RenderLine(line, row, 0, nWidth);
		else
		{
			pLockIn->GetLine(line, row, 0, nWidth);
			if (pf.HasPalette())
				for (int column = 0; column < nWidth; ++column)
				{
					const auto index = line[column] & 0xff;
					auto value = pageIn.GetPalette()[index];
					if (!pf.HasAlpha()) value |= 0xff000000;
					if ((pageIn.GetFlags() & IW::PageFlags::HasTransparent) && index == pageIn.GetTransparent())
						value &= 0x00ffffff;
					line[column] = value;
				}
		}
	};

	for (y = 0; y < nHeight; y++)
	{
		if (pStatus->QueryCancel()) return false;
		readLine(pLine, y);
		if (hasInterior && y >= k2 && y < nHeight - k2)
		{
			for (k = 0; k < Ksize; k++)
				readLine(lines.data() + static_cast<size_t>(k) * nWidth, y + k - k2);

			for (x = k2; x < nWidth - k2; x++)
			{
				r = b = g = 0;
				a = IW::GetA(pLine[x]);

				for (j = -k2; j < kmax; j++)
				{
					for (k = -k2; k < kmax; k++)
					{
						const COLORREF* pBuffer = lines.data() + static_cast<size_t>(k + k2) * nWidth;
						int n = x + j;
						c = pBuffer[n];

						i = kernel[(j + k2) + Ksize * (k + k2)];

						r += IW::GetR(c) * i;
						g += IW::GetG(c) * i;
						b += IW::GetB(c) * i;
					}
				}

				if (Kfactor > 1)
				{
					r /= Kfactor;
					g /= Kfactor;
					b /= Kfactor;
				}

				pLine[x] = IW::SaturateRGBA(r + Koffset, g + Koffset, b + Koffset, a);
			}
		}

		pLockOut->SetLine(pLine, y, 0, nWidth);
		pStatus->Progress(y, nHeight);
		if (pStatus->QueryCancel()) return false;
	}

	pageOut.CopyExtraInfo(pageIn);

	return true;
}

inline bool IW::Filter(const IW::Image& imageIn, IW::Image& imageOut, long* kernel, long Ksize, long Kfactor, long Koffset,
                IW::IStatus* pStatus)
{
	assert(imageOut.GetPageCount() == 0);
	if (!kernel || Ksize <= 0 || (Ksize & 1) == 0)
	{
		pStatus->SetError(L"Invalid image filter kernel.");
		return false;
	}

	for (auto pageIn = imageIn.Pages.begin(); pageIn != imageIn.Pages.end(); ++pageIn)
	{
		if (pStatus->QueryCancel() || !::Filter(*pageIn, imageOut, kernel, Ksize, Kfactor, Koffset, pStatus))
			return false;
	}

	imageOut.Normalize();

	return true;
}


namespace iw::legacy_detail::frame
{
inline void clipped_set_pixel(IW::Page& pageOut, IW::IImageSurfaceLock* pLockOut, const int x, const int y, const UINT c)
{
	if (x >= 0 && x < pageOut.GetWidth() && y >= 0 && y < pageOut.GetHeight())
	{
		pLockOut->SetPixel(x, y, c);
	}
}

inline void draw_border(IW::Page& pageOut, IW::IImageSurfaceLock* pLockOut, CRect& rcFrame, int nOffset, COLORREF crColor)
{
	int nWidth = rcFrame.right - rcFrame.left;
	int nHeight = rcFrame.bottom - rcFrame.top;

	// First draw the frame
	for (int x = nOffset; x < nWidth - nOffset; x++)
	{
		clipped_set_pixel(pageOut, pLockOut, rcFrame.left + x, rcFrame.top + nOffset, crColor);
		clipped_set_pixel(pageOut, pLockOut, rcFrame.left + x, rcFrame.top + nHeight - nOffset - 1, crColor);
	}

	for (int y = nOffset; y < nHeight - nOffset; y++)
	{
		clipped_set_pixel(pageOut, pLockOut, rcFrame.left + nOffset, rcFrame.top + y, crColor);
		clipped_set_pixel(pageOut, pLockOut, rcFrame.left + nWidth - nOffset - 1, rcFrame.top + y, crColor);
	}
}
}

inline bool IW::Frame(const IW::Image& imageIn, IW::Image& imageOut, COLORREF clrFrame, IW::IStatus* pStatus)
{
	for (auto pageIn = imageIn.Pages.begin(); pageIn != imageIn.Pages.end(); ++pageIn)
	{
		IW::ConstIImageSurfaceLockPtr pLockIn = pageIn->GetSurfaceLock();

		constexpr int nFrameWidth = 1;

		CRect rc = pageIn->GetPageRect();
		CRect rcCreate = rc;
		rc.OffsetRect(nFrameWidth, nFrameWidth);
		rcCreate.right += nFrameWidth * 2;
		rcCreate.bottom += nFrameWidth * 2;

		CRect rcFrame = rcCreate;
		IW::Page pageOut = imageOut.CreatePage(rcCreate, IW::PixelFormat::PF24);
		IW::IImageSurfaceLockPtr pLockOut = pageOut.GetSurfaceLock();

		int nWidthOut = rcCreate.right - rcCreate.left;
		int nWidthIn = rc.right - rc.left;
		int nHeightIn = rc.bottom - rc.top;

		// Allocate a line buffer
		IW::CBuffer<COLORREF> pLine(nWidthOut);

		// First draw the frame
		::iw::legacy_detail::frame::draw_border(pageOut, pLockOut, rcFrame, 0, SwapRB(clrFrame));

		int xx = rc.left - rcCreate.left;
		int yy = rc.top - rcCreate.top;

		for (int y = 0; y < nHeightIn; y++)
		{
			pLockIn->RenderLine(pLine, y, 0, nWidthIn);
			pLockOut->SetLine(pLine, y + yy, xx, nWidthIn);
			pStatus->Progress(y, nHeightIn);
			if (pStatus->QueryCancel()) return false;
		}

		pageOut.CopyExtraInfo(*pageIn);
	}

	imageOut.Normalize();

	return true;
}


inline bool IW::Page::CompareBits(const IW::Page& other) const
{
	if (_pBlob->dwSize != other._pBlob->dwSize ||
		_pBlob->pf != other._pBlob->pf ||
		_pBlob->dwTimeDelay != other._pBlob->dwTimeDelay ||
		_pBlob->dwBackGround != other._pBlob->dwBackGround ||
		_pBlob->dwTransparent != other._pBlob->dwTransparent ||
		_pBlob->rectPage.left != other._pBlob->rectPage.left ||
		_pBlob->rectPage.top != other._pBlob->rectPage.top ||
		_pBlob->rectPage.right != other._pBlob->rectPage.right ||
		_pBlob->rectPage.bottom != other._pBlob->rectPage.bottom)
	{
		return false;
	}

	if (memcmp(GetData(), other.GetData(), GetDataSize()) != 0)
	{
		return false;
	}

	return true;
}

inline bool IW::Page::CopyExtraInfo(const IW::Page& other)
{
	SetTimeDelay(other.GetTimeDelay());
	SetTransparent(other.GetTransparent());
	SetBackGround(other.GetBackGround());
	SetFlags(other.GetFlags());

	return false;
}

inline CString IW::Image::ToString() const
{
	CString str;
	str.Format(_T(
			   "PageCount = %Iu, MetaDataCount = %Iu, PelsPerMeter = %d*%d, Title=%s, Tags=%s, Description=%s, ObjectName=%s, Statistics = %s, LoaderName = %s, Flags = %d, OriginalImageSize = %d*%d, OriginalBpp = %d"),
			   Pages.size(),
			   Blobs.size(),
			   _settings.XPelsPerMeter,
			   _settings.YPelsPerMeter,
			   static_cast<LPCTSTR>(_strTitle),
			   static_cast<LPCTSTR>(_strTags),
			   static_cast<LPCTSTR>(_strDescription),
			   static_cast<LPCTSTR>(_strObjectName),
			   static_cast<LPCTSTR>(_strStatistics),
			   static_cast<LPCTSTR>(_strLoaderName),
			   _dwFlags,
	           _settings.OriginalImageSize.cx,
	           _settings.OriginalImageSize.cy,
	           static_cast<int>(_settings.OriginalBpp._pf));

	return str;
}

inline IW::CImageBitmapInfoHeader::CImageBitmapInfoHeader(const IW::Page& page)
{
	// GDI reads bmiColors for any 8-bpp header; leaving it as stack garbage leaks
	// uninitialised memory into the rendered image.
	IW::MemZero(&m_info, sizeof(m_info));

	IW::PixelFormat pf = page.GetPixelFormat();
	int nPaletteEntries = pf.NumberOfPaletteEntries();
	int cx = page.GetWidth();
	int cy = page.GetHeight();
	int nStorageWidth = CalcStorageWidth(cx, pf);
	int nSizeImage = nStorageWidth * cy;

	m_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	m_info.bmiHeader.biWidth = cx;
	m_info.bmiHeader.biHeight = cy;
	m_info.bmiHeader.biPlanes = 1;
	m_info.bmiHeader.biBitCount = static_cast<WORD>(pf.ToBpp());
	m_info.bmiHeader.biCompression = BI_RGB;
	m_info.bmiHeader.biSizeImage = nSizeImage;
	m_info.bmiHeader.biXPelsPerMeter = 0;
	m_info.bmiHeader.biYPelsPerMeter = 0;
	m_info.bmiHeader.biClrUsed = nPaletteEntries;
	m_info.bmiHeader.biClrImportant = 0;

	if (nPaletteEntries != 0)
	{
		IW::MemCopy(m_info.bmiColors, page.GetPalette(), nPaletteEntries * sizeof(RGBQUAD));
	}
	else
	{
		// 16 or 32 bit need bitfields
		switch (pf._pf)
		{
		case IW::PixelFormat::PF1:
		case IW::PixelFormat::PF2:
		case IW::PixelFormat::PF4:
		case IW::PixelFormat::PF8:
		case IW::PixelFormat::PF8Alpha:
		case IW::PixelFormat::PF24:
			// Nothing todo
			break;
		case IW::PixelFormat::PF8GrayScale:
			// No palette is stored with the page, so supply the grey ramp GDI needs.
			for (int i = 0; i < 256; ++i)
				m_info.bmiColors[i] = RGB(i, i, i);
			m_info.bmiHeader.biClrUsed = 256;
			break;
		case IW::PixelFormat::PF555:
			m_info.bmiColors[0] = 0x00007c00;
			m_info.bmiColors[1] = 0x000003e0;
			m_info.bmiColors[2] = 0x0000001f;
			m_info.bmiHeader.biBitCount = 16;
			m_info.bmiHeader.biCompression = BI_BITFIELDS;
			break;
		case IW::PixelFormat::PF565:
			m_info.bmiColors[0] = 0x0000f800;
			m_info.bmiColors[1] = 0x000007e0;
			m_info.bmiColors[2] = 0x0000001f;
			m_info.bmiHeader.biBitCount = 16;
			m_info.bmiHeader.biCompression = BI_BITFIELDS;
			break;
		case IW::PixelFormat::PF32:
		case IW::PixelFormat::PF32Alpha:
			m_info.bmiColors[0] = 0x00ff0000;
			m_info.bmiColors[1] = 0x0000ff00;
			m_info.bmiColors[2] = 0x000000ff;
			m_info.bmiHeader.biBitCount = 32;
			m_info.bmiHeader.biCompression = BI_BITFIELDS;
		}
	}
}

inline void IW::Image::ConvertTo(IW::PixelFormat pf)
{
	IW::Image image;

	for (auto pageIn = Pages.begin(); pageIn != Pages.end(); ++pageIn)
	{
		CRect rc = pageIn->GetPageRect();
		IW::Page pageOut = image.CreatePage(rc, pf);

		const int cx = pageOut.GetWidth();
		const int cy = pageOut.GetHeight();

		IW::ConstIImageSurfaceLockPtr pLockIn = pageIn->GetSurfaceLock();
		IW::IImageSurfaceLockPtr pLockOut = pageOut.GetSurfaceLock();
		IW::CBuffer<COLORREF> pLine(cx);

		for (int y = 0; y < cy; y++)
		{
			pLockIn->RenderLine(pLine, y, 0, cx);
			pLockOut->SetLine(pLine, y, 0, cx);
		}

		pageOut.CopyExtraInfo(*pageIn);
	}

	Copy(image);
}

inline void IW::Image::Normalize()
{
	if (!Pages.empty())
	{
		CRect rc = GetBoundingRect();

		if (rc.top != 0 || rc.left != 0)
		{
			int cx = 0 - rc.left;
			int cy = 0 - rc.top;

			for (auto i = Pages.begin(); i != Pages.end(); ++i)
			{
				rc = i->GetPageRect();
				int x = Min(rc.left, rc.right) + cx;
				int y = Min(rc.top, rc.bottom) + cy;
				i->SetPageOffset(x, y);
			}
		}
	}
}

inline bool IW::Image::Compare(const IW::Image& image, bool bCompareMetaData) const
{
	if (_strTitle != image._strTitle ||
		_strTags != image._strTags ||
		_strDescription != image._strDescription ||
		_strObjectName != image._strObjectName ||
		_strStatistics != image._strStatistics ||
		_strLoaderName != image._strLoaderName ||
		_dwFlags != image._dwFlags ||
		_strErrors != image._strErrors ||
		_strWarnings != image._strWarnings ||
		_settings != image._settings ||
		GetPageCount() != image.GetPageCount() ||
		Blobs.size() != image.Blobs.size())
	{
		return false;
	}

	unsigned nCount = GetPageCount();
	for (unsigned i = 0; i < nCount; ++i)
	{
		const IW::Page p1 = GetPage(i);
		const IW::Page p2 = image.GetPage(i);

		if (!p1.CompareBits(p2))
		{
			return false;
		}
	}

	for (std::size_t index = 0; index < Blobs.size(); ++index)
	{
		const auto& blob = Blobs[index];

		if (bCompareMetaData)
		{
			const IW::MetaData* otherBlob = nullptr;
			if (legacy_detail::metadata_is_repeatable(blob.GetType()))
			{
				std::size_t occurrence = 0;
				for (std::size_t previous = 0; previous < index; ++previous)
					if (Blobs[previous].IsType(blob.GetType())) ++occurrence;

				std::size_t candidate = 0;
				for (const auto& value : image.Blobs)
				{
					if (!value.IsType(blob.GetType())) continue;
					if (candidate++ == occurrence)
					{
						otherBlob = &value;
						break;
					}
				}
			}
			else
			{
				const auto singular = image.GetMetaData(blob.GetType());
				if (blob.GetDataSize() != singular.GetDataSize()) return false;
				if (!blob.IsEmpty() &&
					std::memcmp(blob.GetData(), singular.GetData(), blob.GetDataSize()) != 0)
					return false;
				continue;
			}

			if (!otherBlob ||
				blob.GetDataSize() != otherBlob->GetDataSize() ||
				(!blob.IsEmpty() &&
					std::memcmp(blob.GetData(), otherBlob->GetData(), blob.GetDataSize()) != 0))
				return false;
		}
	}

	return true;
}


inline IW::Image IW::Image::Clone() const
{
	IW::Image other;

	for (auto i = Pages.begin(); i != Pages.end(); ++i)
		other.Pages.push_back(i->Clone());

	for (auto i = Blobs.begin(); i != Blobs.end(); ++i)
		other.Blobs.push_back(i->Clone());

	other._strTitle = _strTitle;
	other._strTags = _strTags;
	other._strDescription = _strDescription;
	other._strObjectName = _strObjectName;
	other._strStatistics = _strStatistics;
	other._strLoaderName = _strLoaderName;
	other._dwFlags = _dwFlags;
	other._settings = _settings;
	other._strErrors = _strErrors;
	other._strWarnings = _strWarnings;

	return other;
}

inline void IW::Image::Copy(const IW::Image& other)
{
	Pages = other.Pages;
	Blobs = other.Blobs;

	_strTitle = other._strTitle;
	_strTags = other._strTags;
	_strDescription = other._strDescription;
	_strObjectName = other._strObjectName;
	_strStatistics = other._strStatistics;
	_strLoaderName = other._strLoaderName;
	_dwFlags = other._dwFlags;
	_settings = other._settings;
	_strErrors = other._strErrors;
	_strWarnings = other._strWarnings;
}


// Create a new image
inline IW::Page& IW::Image::CreatePage(int cx, int cy, const IW::PixelFormat& pf)
{
	Free();
	return CreatePage(CRect(0, 0, cx, cy), pf);
};

// Create a new image
inline IW::Page& IW::Image::CreatePage(const IW::Page& pageIn)
{
	CRect rc = pageIn.GetPageRect();

	IW::PixelFormat pf = pageIn.GetPixelFormat();
	IW::Page& pageOut = CreatePage(rc, pf);
	int nPaletteEntries = pf.NumberOfPaletteEntries();

	if (nPaletteEntries != 0)
	{
		IW::MemCopy(pageOut.GetPalette(), pageIn.GetPalette(), nPaletteEntries * sizeof(RGBQUAD));
	}

	return pageOut;
};

inline IW::Page& IW::Image::CreatePage(const CRect& rc, const IW::PixelFormat& pf)
{
	const auto width = static_cast<std::int64_t>(rc.right) - rc.left;
	const auto height = static_cast<std::int64_t>(rc.bottom) - rc.top;
	if (width <= 0 || height <= 0 || width > INT_MAX || height > INT_MAX)
		throw std::bad_alloc();

	const int cx = static_cast<int>(width);
	const int cy = static_cast<int>(height);
	const int nStorageWidth = CalcStorageWidth(cx, pf);
	const auto nSizeImage64 = static_cast<std::uint64_t>(nStorageWidth) * cy;
	if (nSizeImage64 > kMaxPageBytes) throw std::bad_alloc();

	const int nPaletteEntries = pf.NumberOfPaletteEntries();
	const DWORD nSizeImage = static_cast<DWORD>(nSizeImage64);
	const DWORD nSizePalette = nPaletteEntries * sizeof(RGBQUAD);

	IW::Page page;
	PIWBITMAPINFO pIWBM = page.Alloc(nSizePalette + nSizeImage);
	pIWBM->dwBackGround = 0xffffffff;
	pIWBM->rectPage = rc;
	pIWBM->pf = pf;

	// Blob::Alloc only zeroes the header. An alpha page is composited *into* --
	// the blitter reads the destination back and leaves a fully transparent
	// pixel untouched -- so it must not start as heap residue.
	if (pf.HasAlpha())
	{
		IW::MemZero(page.GetBitmap(), nSizeImage);
	}

	if (nPaletteEntries)
	{
		// Create an arbitrary color table (gray scale).
		LPCOLORREF pRGB = page.GetPalette();

		if (nPaletteEntries == 2)
		{
			pRGB[1] = RGB(255, 255, 255);
			pRGB[0] = RGB(0, 0, 0);
		}
		else
		{
			for (int i = 0; i < nPaletteEntries; i++)
			{
				*pRGB = RGB(i, i, i);
				pRGB++;
			}
		}
	}

	Pages.push_back(page);
	return Pages.back();
}


inline IW::PixelFormat IW::PixelFormat::FromBpp(int nBpp)
{
	switch (nBpp)
	{
	case 1:
		return IW::PixelFormat(PF1);
	case 2:
		return IW::PixelFormat(PF2);
	case 4:
		return IW::PixelFormat(PF4);
	case 8:
		return IW::PixelFormat(PF8);
	case 15:
	case 16:
		return IW::PixelFormat(PF555);
	case 24:
		return IW::PixelFormat(PF24);
	case 32:
	default:
		return IW::PixelFormat(PF32);
	}
}

inline LPCTSTR IW::PixelFormat::ToString() const
{
	switch (_pf)
	{
	case PF1:
		return _T("1");
	case PF2:
		return _T("2");
	case PF4:
		return _T("4");
	case PF8:
	case PF8Alpha:
	case PF8GrayScale:
		return _T("8");
	case PF555:
		return _T("555");
	case PF565:
		return _T("565");
	case PF24:
		return _T("888");
	}

	return _T("8888");
}


inline CString IW::CameraSettings::FormatAperture() const
{
	CString str;
	if (Aperture.denominator != 0)
	{
		float f = Aperture.ToFloat() / 2.0f;
		str.Format(_T("f/%.01f"), pow(2, f));
	}
	return str;
}

inline CString IW::CameraSettings::FormatIsoSpeed() const
{
	CString str;
	if (IsoSpeed != 0)
		str = IToStr(IsoSpeed);
	return str;
}

inline CString IW::CameraSettings::FormatWhiteBalance() const
{
	CString str;
	if (WhiteBalance != 0)
		str = IToStr(WhiteBalance);
	return str;
}

inline CString IW::CameraSettings::FormatExposureTime() const
{
	CString str;
	if (ExposureTime.denominator != 0)
	{
		float t = ExposureTime.ToFloat();

		if (t < 1.0)
		{
			str.Format(_T("1/%d s"), static_cast<int>(ceil(1.0f / t)));
		}
		else
		{
			str.Format(_T("%d s"), static_cast<int>(t));
		}
	}
	return str;
}

inline CString IW::CameraSettings::FormatFocalLength() const
{
	CString str;
	if (FocalLength.denominator != 0)
	{
		str.Format(_T("%.1f mm"), FocalLength.ToFloat());
	}

	if (FocalLength35mmEquivalent != 0)
	{
		CString str35mm;
		str35mm.Format(_T(" (%dmm film eq)"), FocalLength35mmEquivalent);
		str += str35mm;
	}

	return str;
}

// ---- ImagingConvert ----
// ImageWalker by Zac Walker
//
// Purpose: Pixel format conversion - every depth up to 32-bit RGBA, and the
//          running sums the scaler uses.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

//
// IW::Image : implementation file
//
//
//////////////////////////////////////////////////////////////////////

#include "LegacyPch.h"
#include "ImagingLock.h"
#include "iw/channelaverage.h"
#include <cstring>

using IW::LockDetail::masks1;
using IW::LockDetail::masks2;
using IW::LockDetail::shift2;

inline void IW::IndexesToRGBA(LPCOLORREF pBitsOut, const int nSize, LPCCOLORREF pPalette)
{
	for (int x = 0; x < nSize; x++)
	{
		pBitsOut[x] = pPalette[pBitsOut[x]] | 0xFF000000;
	}
}

inline void IW::SumLineTo32(LPBYTE pLine, const RGBSUM* ps, int cx)
{
	for (int i = 0; i < cx; i++)
	{
		pLine[0] = ChannelAverage(ps->r, ps->c, true);
		pLine[1] = ChannelAverage(ps->g, ps->c, true);
		pLine[2] = ChannelAverage(ps->b, ps->c, true);
		// Alpha has its own sample count (transparent pixels omit RGB).
		pLine[3] = ChannelAverage(ps->a, ps->ac, true);

		++ps;
		pLine += 4;
	}
}

inline void IW::SumLineTo16(LPBYTE pLine, const RGBSUM* ps, int cx)
{
	assert((reinterpret_cast<UINT_PTR>(pLine) & 0x03) == 0); // Should be aligned to align??

	int denom, denom2;
	const RGBSUM* ps2;

	LPBYTE pLineEnd = pLine + (cx * 2);

	while (pLine < (pLineEnd - 2))
	{
		// blend with white
		denom = Max(ps->c, 1u);

		ps2 = ps + 1;
		denom2 = Max(ps2->c, 1u);

		*((int*)pLine) =
			(((ps->r / denom) >> 3) & 0x0000001F) |
			(((ps->g / denom) << 2) & 0x000003E0) |
			(((ps->b / denom) << 7) & 0x00007C00) |
			(((ps2->r / denom2) << 13) & 0x001F0000) |
			(((ps2->g / denom2) << 18) & 0x03E00000) |
			(((ps2->b / denom2) << 23) & 0x7C000000);

		ps += 2;
		pLine += 4;
	}

	// May be a last one!!
	while (pLine < pLineEnd)
	{
		denom = Max(ps->c, 1u);

		*((short*)pLine) =
			(((ps->r / denom) >> (3)) & 0x0000001F) |
			(((ps->g / denom) << (2)) & 0x000003E0) |
			(((ps->b / denom) << (7)) & 0x00007C00);

		ps++;
		pLine += 2;
	}
}

inline void IW::ToSums(LPRGBSUM pSum, LPDWORD pLineIn, int cxOut, int cxIn, bool isHighQuality, bool bFirstY)
{
	std::int64_t x = static_cast<std::int64_t>(cxOut >> 1) + cxIn;

	LPCRGBSUM pSumEnd = pSum + cxOut;
	LPCDWORD pLineEnd = pLineIn + cxIn;
	DWORD c, a;

	if (isHighQuality)
	{
		while (pLineIn < pLineEnd)
		{
			assert(pSum < pSumEnd); // Check for out buffer overflow

			c = *pLineIn++;
			a = IW::GetA(c);

			if (a)
			{
				pSum->r += IW::GetR(c);
				pSum->g += IW::GetG(c);
				pSum->b += IW::GetB(c);
				pSum->c++;
				pSum->a += a;
			}

			pSum->ac++;
			x -= cxOut;
			if (x < cxOut)
			{
				pSum++;
				x += cxIn;
			}
		}
	}
	else
	{
		if (bFirstY)
		{
			while (pLineIn < pLineEnd)
			{
				assert(pSum < pSumEnd); // Check for out buffer overflow

				c = *pLineIn++;
				pSum->r += IW::GetR(c);
				pSum->g += IW::GetG(c);
				pSum->b += IW::GetB(c);
				pSum->c++;

				x -= cxOut;
				if (x < cxOut)
				{
					pSum++;
					x += cxIn;
				}
			}
		}
		else
		{
			for (int i = 0; i < cxOut; i++)
			{
				int xx = static_cast<int>((static_cast<std::int64_t>(i) * cxIn) / cxOut);
				assert(xx < cxIn);

				c = pLineIn[xx];

				pSum->r += IW::GetR(c);
				pSum->g += IW::GetG(c);
				pSum->b += IW::GetB(c);
				pSum->c++;

				pSum++;
			}
		}
	}
}


inline void IW::Convert1to32(LPCOLORREF pBitsOut, LPCBYTE pBitsInIn, const int nStart, const int nSize)
{
	const int nSizeWithStart = nSize + nStart;
	auto pBitsIn = (LPCDWORD)pBitsInIn;
	DWORD c = pBitsIn[nStart >> 5];
	int xn;

	for (int xx = nStart; xx < nSizeWithStart; xx++)
	{
		xn = xx & 31;
		if (xn == 0)
		{
			c = pBitsIn[xx >> 5];
		}

		*pBitsOut++ = (c & masks1[xn]) ? 1 : 0;
	}
}

inline void IW::Convert1to32(LPCOLORREF pBitsOut, LPCBYTE pBitsInIn, const int nStart, const int nSize, LPCCOLORREF pPalette)
{
	Convert1to32(pBitsOut, pBitsInIn, nStart, nSize);
	IndexesToRGBA(pBitsOut, nSize, pPalette);
}

inline void IW::Convert2to32(LPCOLORREF pBitsOut, LPCBYTE pBitsIn, const int nStart, const int nSize)
{
	const int nSizeWithStart = nSize + nStart;

	for (int xx = nStart; xx < nSizeWithStart; xx++)
	{
		BYTE b = (pBitsIn[xx >> 2] & masks2[xx & 3]) >> shift2[xx & 3];
		*pBitsOut++ = b;
	}
}

inline void IW::Convert2to32(LPCOLORREF pBitsOut, LPCBYTE pBitsIn, const int nStart, const int nSize, LPCCOLORREF pPalette)
{
	Convert2to32(pBitsOut, pBitsIn, nStart, nSize);
	IndexesToRGBA(pBitsOut, nSize, pPalette);
}

inline void IW::Convert4to32(LPCOLORREF pBitsOut, LPCBYTE pBitsIn, const int nStart, const int nSize)
{
	const int nSizeWithStart = nSize + nStart;
	int c;

	for (int xx = nStart; xx < nSizeWithStart; xx++)
	{
		c = (xx & 1) ? pBitsIn[xx >> 1] & 0x0f : pBitsIn[xx >> 1] >> 4;
		*pBitsOut++ = c;
	}
}

inline void IW::Convert4to32(LPCOLORREF pBitsOut, LPCBYTE pBitsIn, const int nStart, const int nSize, LPCCOLORREF pPalette)
{
	Convert4to32(pBitsOut, pBitsIn, nStart, nSize);
	IndexesToRGBA(pBitsOut, nSize, pPalette);
}

inline void IW::Convert8to32(LPCOLORREF pBitsOut, LPCBYTE pBitsIn, const int nStart, const int nSize)
{
	const int nSizeWithStart = nSize + nStart;
	pBitsIn += nStart;

	for (int n = nStart; n < nSizeWithStart; n++)
	{
		*pBitsOut++ = *pBitsIn++;
	}
}

inline void IW::Convert8to32(LPCOLORREF pBitsOut, LPCBYTE pBitsIn, const int nStart, const int nSize, LPCCOLORREF pPalette)
{
	Convert8to32(pBitsOut, pBitsIn, nStart, nSize);
	IndexesToRGBA(pBitsOut, nSize, pPalette);
}

inline void IW::Convert8Alphato32(LPCOLORREF pBitsOut, LPCBYTE pBitsIn, const int nStart, const int nSize,
                           LPCCOLORREF pPalette)
{
	const int nSizeWithStart = nSize + nStart;
	pBitsIn += nStart;

	for (int n = nStart; n < nSizeWithStart; n++)
	{
		*pBitsOut++ = pPalette[*pBitsIn++];
	}
}

inline void IW::Convert8GrayScaleto32(LPCOLORREF pBitsOut, LPCBYTE pBitsIn, const int nStart, const int nSize)
{
	int c;
	const int nSizeWithStart = nSize + nStart;
	pBitsIn += nStart;

	for (int n = nStart; n < nSizeWithStart; n++)
	{
		c = *pBitsIn++;
		// Opaque, like every other ConvertNto32. Alpha 0 here made the
		// high-quality downscale accumulator, which weights by alpha, drop
		// every sample.
		*pBitsOut++ = RGB(c, c, c) | 0xFF000000;
	}
}

inline void IW::Convert555to32(LPCOLORREF pBitsOut, LPCBYTE pBitsInIn, const int nStart, const int nSize)
{
	const COLORREF* pBitsOutEnd = pBitsOut + nSize;
	auto pBitsIn = reinterpret_cast<const unsigned short*>(pBitsInIn) + nStart;
	DWORD c;

	while (pBitsOutEnd - pBitsOut >= 2)
	{
		std::memcpy(&c, pBitsIn, sizeof(c));
		pBitsIn += 2;
		*pBitsOut++ = RGB555to888(c) | 0xFF000000;
		c >>= 16;
		*pBitsOut++ = RGB555to888(c) | 0xFF000000;
	}
	if (pBitsOut < pBitsOutEnd) *pBitsOut = RGB555to888(*pBitsIn) | 0xFF000000;
}

inline void IW::Convert565to32(LPCOLORREF pBitsOut, LPCBYTE pBitsInIn, const int nStart, const int nSize)
{
	const COLORREF* pBitsOutEnd = pBitsOut + nSize;
	auto pBitsIn = reinterpret_cast<const unsigned short*>(pBitsInIn) + nStart;
	DWORD c;

	while (pBitsOutEnd - pBitsOut >= 2)
	{
		std::memcpy(&c, pBitsIn, sizeof(c));
		pBitsIn += 2;
		*pBitsOut++ = RGB565to888(c) | 0xFF000000;
		c >>= 16;
		*pBitsOut++ = RGB565to888(c) | 0xFF000000;
	}
	if (pBitsOut < pBitsOutEnd) *pBitsOut = RGB565to888(*pBitsIn) | 0xFF000000;
}

inline void IW::Convert24to32(LPCOLORREF pBitsOut, LPCBYTE pBitsIn, const int nStart, const int nSize)
{
	// Get a pointer to the end
	const COLORREF* pBitsOutEnd = pBitsOut + nSize;

	// Find start location
	if (nStart)
		pBitsIn += 3 * nStart;
	// Move to an aligned bit part of memory
	auto pOut = (LPBYTE)pBitsOut;
	// Align to 8 byte boundary
	while ((0 != (reinterpret_cast<UINT_PTR>(pBitsIn) & 3)) && (pOut < (LPCBYTE)pBitsOutEnd))
	{
		*pOut++ = *pBitsIn++;
		*pOut++ = *pBitsIn++;
		*pOut++ = *pBitsIn++;
		*pOut++ = static_cast<BYTE>(0xff);
	}

	pBitsOut = (LPDWORD)pOut;

	{
		// Continue processing
		UINT nPixel1, nPixel2;
		auto pIn = (LPCDWORD)pBitsIn;
		const COLORREF* pBitsOutEnd2 = pBitsOutEnd - 4;

		while (pBitsOut < pBitsOutEnd2)
		{
			// DWORD 1
			nPixel1 = *pIn++;

			// pixel 1
			*pBitsOut++ = (nPixel1 & 0x00ffffff) | 0xff000000;

			// pixel 2
			nPixel2 = (nPixel1 >> 24) & 0xff;

			// DWORD 2
			nPixel1 = *pIn++;

			nPixel2 |= (nPixel1 << 8) & 0x00ffff00;

			*pBitsOut++ = nPixel2 | 0xff000000;
			// pixel 3
			nPixel2 = (nPixel1 >> 16) & 0x0000ffff;

			// DWORD 3
			nPixel1 = *pIn++;
			nPixel2 |= (nPixel1 << 16) & 0x00ff0000;
			*pBitsOut++ = nPixel2 | 0xff000000;

			// pixel 4
			*pBitsOut++ = (nPixel1 >> 8) | 0xff000000;
		}
		pBitsIn = (LPCBYTE)pIn;
		pOut = (LPBYTE)pBitsOut;
	}

	// Complete any incomplete work
	while (pOut < (LPCBYTE)pBitsOutEnd)
	{
		*pOut++ = *pBitsIn++;
		*pOut++ = *pBitsIn++;
		*pOut++ = *pBitsIn++;
		*pOut++ = static_cast<BYTE>(0xff);
	}
}

////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////

inline void IW::ConvertARGBtoABGR(LPBYTE pBitsOutX, LPCBYTE pBitsInX, const int nSize)
{
	auto pBitsIn = (LPDWORD)pBitsInX;
	auto pBitsOut = (LPDWORD)pBitsOutX;
	const LPDWORD pBitsOutEnd = pBitsOut + nSize;

	while (pBitsOut < pBitsOutEnd)
	{
		*pBitsOut++ = SwapRB(*pBitsIn++);
	}
}


inline void IW::ConvertRGBtoBGR(LPBYTE pBitsOut, LPCBYTE pBitsIn, const int nSize)
{
	const LPBYTE pBitsOutEnd = pBitsOut + (nSize * 3);
	BYTE r, g, b;

	while (pBitsOut < pBitsOutEnd)
	{
		r = *pBitsIn++;
		g = *pBitsIn++;
		b = *pBitsIn++;

		*pBitsOut++ = b;
		*pBitsOut++ = g;
		*pBitsOut++ = r;
	}
}

inline void IW::ConvertYCbCrtoBGR(LPBYTE pBitsOut, LPCBYTE pBitsIn, const int nSize)
{
	for (int i = 0; i < nSize; i++)
	{
		const int Y = pBitsIn[0];
		const int Cb = pBitsIn[1] - 128;
		const int Cr = pBitsIn[2] - 128;

		pBitsOut[0] = ByteClamp(Y + ((454 * Cb) >> 8));
		pBitsOut[1] = ByteClamp(Y - ((88 * Cb + 183 * Cr) >> 8));
		pBitsOut[2] = ByteClamp(Y + ((359 * Cr) >> 8));

		pBitsIn += 3;
		pBitsOut += 3;
	}
}

inline void IW::ConvertCMYKtoBGR(LPBYTE pBitsOut, LPCBYTE pBitsIn, const int nSize, bool bInverted)
{
	for (int i = 0; i < nSize; i++)
	{
		// Work in "how much light gets through", which is what an inverted file
		// already stores and what a plain one is 255 minus.
		int c = pBitsIn[0];
		int m = pBitsIn[1];
		int y = pBitsIn[2];
		int k = pBitsIn[3];

		if (!bInverted)
		{
			c = 255 - c;
			m = 255 - m;
			y = 255 - y;
			k = 255 - k;
		}

		pBitsOut[0] = static_cast<BYTE>((y * k) / 255);
		pBitsOut[1] = static_cast<BYTE>((m * k) / 255);
		pBitsOut[2] = static_cast<BYTE>((c * k) / 255);

		pBitsIn += 4;
		pBitsOut += 3;
	}
}

inline void IW::ConvertCIELABtoBGR(LPBYTE pBitsOut, LPCBYTE pBitsIn, const int nSize)
{
	int x = 0;
	int R, G, B;
	int L, a, b;

	for (int i = 0; i < nSize; i++)
	{
		// TIFF CIELAB samples are stored L*, a*, b*.
		L = pBitsIn[x + 0];
		a = pBitsIn[x + 1];
		b = pBitsIn[x + 2];

		LABtoRGB(L, a, b, R, G, B);

		// The destination is a DIB scanline: byte 0 is blue.
		// LABtoRGB clamps all three channels to [0, 255].
		pBitsOut[x + 0] = static_cast<BYTE>(B);
		pBitsOut[x + 1] = static_cast<BYTE>(G);
		pBitsOut[x + 2] = static_cast<BYTE>(R);
		x += 3;
	}
}

inline void IW::ConvertICCLABtoBGR(LPBYTE pBitsOut, LPCBYTE pBitsIn, const int nSize)
{
	ConvertCIELABtoBGR(pBitsOut, pBitsIn, nSize);
}

inline void IW::ConvertITULABtoBGR(LPBYTE pBitsOut, LPCBYTE pBitsIn, const int nSize)
{
	ConvertCIELABtoBGR(pBitsOut, pBitsIn, nSize);
}

inline void IW::ConvertLOGLtoBGR(LPBYTE pBitsOut, LPCBYTE pBitsIn, const int nSize)
{
}

inline void IW::ConvertLOGLUVtoBGR(LPBYTE pBitsOut, LPCBYTE pBitsIn, const int nSize)
{
}


////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////

inline void IW::RGBtoHSL(int R, int G, int B, int& H, int& S, int& L)
{
	constexpr int HSLMAX = 255; /* H,L, and S vary over 0-HSLMAX */
	constexpr int RGBMAX = 255; /* R,G, and B vary over 0-RGBMAX */
	/* HSLMAX BEST IF DIVISIBLE BY 6 */
	/* RGBMAX, HSLMAX must each fit in a BYTE. */
	/* Hue is undefined if Saturation is 0 (grey-scale) */
	/* This value determines where the Hue scrollbar is */
	/* initially set for achromatic colors */
	constexpr int UNDEFINED = (HSLMAX * 2 / 3);

	WORD Rdelta, Gdelta, Bdelta; /* intermediate value: % of spread from max*/
	const int cMax = Max(Max(R, G), B); /* calculate lightness */
	const int cMin = Min(Min(R, G), B);

	L = static_cast<BYTE>((((cMax + cMin) * HSLMAX) + RGBMAX) / (2 * RGBMAX));

	if (cMax == cMin) /* r=g=b --> achromatic case */
	{
		S = 0; /* saturation */
		H = UNDEFINED; /* hue */
	}
	else
	{
		/* chromatic case */
		if (L <= (HSLMAX / 2)) /* saturation */
			S = static_cast<BYTE>((((cMax - cMin) * HSLMAX) + ((cMax + cMin) / 2)) / (cMax + cMin));
		else
			S = static_cast<BYTE>((((cMax - cMin) * HSLMAX) + ((2 * RGBMAX - cMax - cMin) / 2)) / (2 * RGBMAX - cMax -
				cMin));
		/* hue */
		Rdelta = static_cast<WORD>((((cMax - R) * (HSLMAX / 6)) + ((cMax - cMin) / 2)) / (cMax - cMin));
		Gdelta = static_cast<WORD>((((cMax - G) * (HSLMAX / 6)) + ((cMax - cMin) / 2)) / (cMax - cMin));
		Bdelta = static_cast<WORD>((((cMax - B) * (HSLMAX / 6)) + ((cMax - cMin) / 2)) / (cMax - cMin));

		if (R == cMax)
			H = static_cast<BYTE>(Bdelta - Gdelta);
		else if (G == cMax)
			H = static_cast<BYTE>((HSLMAX / 3) + Rdelta - Bdelta);
		else /* B == cMax */
			H = static_cast<BYTE>(((2 * HSLMAX) / 3) + Gdelta - Rdelta);

		if (H < 0) H += HSLMAX;
		if (H > HSLMAX) H -= HSLMAX;
	}
}


////////////////////////////////////////////////////////////////////////////////
inline float HueToRGB(float n1, float n2, float hue)
{
	//<F. Livraghi> fixed implementation for HSL2RGB routine
	float rValue;

	if (hue > 360)
		hue = hue - 360;
	else if (hue < 0)
		hue = hue + 360;

	if (hue < 60)
		rValue = n1 + (n2 - n1) * hue / 60.0f;
	else if (hue < 180)
		rValue = n2;
	else if (hue < 240)
		rValue = n1 + (n2 - n1) * (240 - hue) / 60;
	else
		rValue = n1;

	return rValue;
}

////////////////////////////////////////////////////////////////////////////////


inline void IW::HSLtoRGB(int H, int S, int L, int& r, int& g, int& b)
{
	//<F. Livraghi> fixed implementation for HSL2RGB routine
	const float h = static_cast<float>(H) * 360.0f / 255.0f;
	const float s = static_cast<float>(S) / 255.0f;
	const float l = static_cast<float>(L) / 255.0f;
	const float m2 = (l <= 0.5) ? l * (1 + s) : l + s - l * s;
	const float m1 = 2 * l - m2;

	if (s == 0)
	{
		r = g = b = static_cast<BYTE>(l * 255.0f);
	}
	else
	{
		r = static_cast<int>(HueToRGB(m1, m2, h + 120) * 255.0f);
		g = static_cast<int>(HueToRGB(m1, m2, h) * 255.0f);
		b = static_cast<int>(HueToRGB(m1, m2, h - 120) * 255.0f);
	}
}


inline void IW::RGBtoLAB(int R, int G, int B, int& L, int& a, int& b)
{
	// Convert between RGB and CIE-Lab color spaces
	// Uses ITU-R recommendation BT.709 with D65 as reference white.
	// algorithm contributed by "Mark A. Ruzon" <ruzon@CS.Stanford.EDU>
	double fX, fY, fZ;
	double X = 0.412453 * R + 0.357580 * G + 0.180423 * B;
	double Y = 0.212671 * R + 0.715160 * G + 0.072169 * B;
	double Z = 0.019334 * R + 0.119193 * G + 0.950227 * B;

	X /= (255 * 0.950456);
	Y /= 255;
	Z /= (255 * 1.088754);

	if (Y > 0.008856)
	{
		fY = pow(Y, 1.0 / 3.0);
		L = static_cast<int>(116.0 * fY - 16.0 + 0.5);
	}
	else
	{
		fY = 7.787 * Y + 16.0 / 116.0;
		L = static_cast<int>(903.3 * Y + 0.5);
	}

	if (X > 0.008856)
		fX = pow(X, 1.0 / 3.0);
	else
		fX = 7.787 * X + 16.0 / 116.0;

	if (Z > 0.008856)
		fZ = pow(Z, 1.0 / 3.0);
	else
		fZ = 7.787 * Z + 16.0 / 116.0;

	a = static_cast<int>(500.0 * (fX - fY) + 0.5);
	b = static_cast<int>(200.0 * (fY - fZ) + 0.5);
}

inline void IW::LABtoRGB(int L, int a, int b, int& R, int& G, int& B)
{
	// Convert between RGB and CIE-Lab color spaces
	// Uses ITU-R recommendation BT.709 with D65 as reference white.
	// algorithm contributed by "Mark A. Ruzon" <ruzon@CS.Stanford.EDU>
	double X, Y, Z;
	double fY = pow((L + 16.0) / 116.0, 3.0);
	if (fY < 0.008856)
		fY = L / 903.3;
	Y = fY;

	if (fY > 0.008856)
		fY = pow(fY, 1.0 / 3.0);
	else
		fY = 7.787 * fY + 16.0 / 116.0;

	double fX = a / 500.0 + fY;
	if (fX > 0.206893)
		X = pow(fX, 3.0);
	else
		X = (fX - 16.0 / 116.0) / 7.787;

	double fZ = fY - b / 200.0;
	if (fZ > 0.206893)
		Z = pow(fZ, 3.0);
	else
		Z = (fZ - 16.0 / 116.0) / 7.787;

	X *= (0.950456 * 255);
	Y *= 255;
	Z *= (1.088754 * 255);

	int RR = static_cast<int>(3.240479 * X - 1.537150 * Y - 0.498535 * Z + 0.5);
	int GG = static_cast<int>(-0.969256 * X + 1.875992 * Y + 0.041556 * Z + 0.5);
	int BB = static_cast<int>(0.055648 * X - 0.204043 * Y + 1.057311 * Z + 0.5);

	R = RR < 0 ? 0 : RR > 255 ? 255 : RR;
	G = GG < 0 ? 0 : GG > 255 ? 255 : GG;
	B = BB < 0 ? 0 : BB > 255 ? 255 : BB;
}

// ---- ImagingEdits ----
// ImageWalker by Zac Walker
//
// Purpose: Applies the edit stack to an image, plus the auto colour and auto
//          straighten that populate it.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

//
// ImageEdits: applying the edit stack to pixels.
//
//////////////////////////////////////////////////////////////////////

#include <iw/imaging/ImageEdits.h>
#include <iw/legacy/NativeCodec.h>
#include "LegacyPch.h"
#include "Imaging.h"
#include "ImagingLock.h"
#include "ImagingStreams.h"
#include "ImagingEdits.h"

#include <cmath>

inline constexpr double s_pi = 3.14159265358979323846;

///////////////////////////////////////////////////////////////////////
// Serialisation

inline CSize ImageEdits::TransformedSize(CSize sizeIn) const
{
	CSize size = sizeIn;

	if (_rotate == 1 || _rotate == 3)
	{
		const int cx = size.cx;
		size.cx = size.cy;
		size.cy = cx;
	}

	// The warp keeps the frame it was given; only the crop changes the extent.
	return size;
}

namespace iw::legacy_detail::source_imagingedits
{
	// Defined below, beside the matrix it needs: walks a rectangle out to the
	// edge of the warped picture one side at a time.
	CRect ShrinkToWarpedPicture(const ImageEdits &edits, CSize size, const CRect &rc);
}
using namespace iw::legacy_detail::source_imagingedits;

inline CRect ImageEdits::CropBounds(CSize sizeIn) const
{
	const CSize size = TransformedSize(sizeIn);
	CRect rc(0, 0, size.cx, size.cy);

	if (size.cx < 2 || size.cy < 2)
		return rc;

	if (_straighten != 0)
	{
		const double angle = fabs(_straighten / 10.0) * s_pi / 180.0;
		const double sa = fabs(sin(angle));
		const double ca = fabs(cos(angle));

		const double w = size.cx;
		const double h = size.cy;
		const bool bWide = w >= h;
		const double longSide = bWide ? w : h;
		const double shortSide = bWide ? h : w;

		double wr, hr;

		// The largest-area upright rectangle inside a w x h rectangle turned by
		// this angle. Past the half-way case the limit is one pair of corners
		// rather than all four, which is what the first branch covers.
		if (shortSide <= 2.0 * sa * ca * longSide || fabs(sa - ca) < 1e-10)
		{
			const double half = 0.5 * shortSide;
			wr = bWide ? half / sa : half / ca;
			hr = bWide ? half / ca : half / sa;
		}
		else
		{
			const double cos2a = ca * ca - sa * sa;
			wr = (w * ca - h * sa) / cos2a;
			hr = (h * ca - w * sa) / cos2a;
		}

		const int cx = IW::Clamp(static_cast<int>(wr), 1, size.cx);
		const int cy = IW::Clamp(static_cast<int>(hr), 1, size.cy);

		rc = CRect(0, 0, cx, cy);
		rc.OffsetRect((size.cx - cx) / 2, (size.cy - cy) / 2);
	}

	// Perspective empties a band along one edge of the frame and a wedge at
	// each end of it, and the shape is not symmetric, so there is no closed
	// form to fold into the case above.
	if (_perspectiveH != 0 || _perspectiveV != 0)
		rc = ShrinkToWarpedPicture(*this, size, rc);

	return rc;
}

///////////////////////////////////////////////////////////////////////
// Tone and colour

namespace iw::legacy_detail::source_imagingedits
{
	inline iw::edits::ImageEdits EditColorValues(const ImageEdits& edits)
	{
		iw::edits::ImageEdits value;
		value.brightness = edits._brightness;
		value.contrast = edits._contrast;
		value.darks = edits._darks;
		value.midtones = edits._midtones;
		value.lights = edits._lights;
		value.saturation = edits._saturation;
		value.vibrance = edits._vibrance;
		value.temperature = edits._temperature;
		value.tint = edits._tint;
		return value;
	}

	inline void ReadEditRow(const libfiles::pixel_rows& frame, int y, std::vector<std::uint32_t>& pixels)
	{
		auto row = frame;
		row.height = 1;
		row.bottom_up = false;
		row.pixels = frame.pixels.subspan(static_cast<size_t>(frame.bottom_up ? static_cast<int>(frame.height) - 1 - y : y) *
			frame.stride, frame.stride);
		libfiles::copy_bgra(row, pixels, frame.width, false);
	}
}
using namespace iw::legacy_detail::source_imagingedits;
inline bool IW::ApplyEditColor(const IW::Image& imageIn, IW::Image& imageOut, const ImageEdits& edits, IW::IStatus* pStatus)
{
	const iw::edits::ColorTransform transform(EditColorValues(edits));

	IW::IterateImageMetaData(imageIn, imageOut, pStatus);

	for (auto pageIn = imageIn.Pages.begin(); pageIn != imageIn.Pages.end(); ++pageIn)
	{
		const IW::PixelFormat pf = pageIn->GetPixelFormat();
		const int nWidth = pageIn->GetWidth();
		const int nHeight = pageIn->GetHeight();
		const int nStorageWidth = IW::CalcStorageWidth(nWidth, pf);

		IW::PixelFormat outputFormat = pf;
		if (pf == IW::PixelFormat::PF8GrayScale && (edits._temperature != 0 || edits._tint != 0))
			outputFormat = IW::PixelFormat::PF24;
		IW::Page pageOut = imageOut.CreatePage(pageIn->GetPageRect(), outputFormat);

		const int nPaletteEntries = pf.NumberOfPaletteEntries();

		if (nPaletteEntries > 0)
		{
			for (int i = 0; i < nPaletteEntries; ++i)
				pageOut.GetPalette()[i] = transform.apply_pixel(pageIn->GetPalette()[i]);

			for (int y = 0; y < nHeight; y++)
			{
				IW::MemCopy(pageOut.GetBitmapLine(y), pageIn->GetBitmapLine(y), nStorageWidth);
			}
		}
		else
		{
			IW::ConstIImageSurfaceLockPtr pLockIn = pageIn->GetSurfaceLock();
			IW::IImageSurfaceLockPtr pLockOut = pageOut.GetSurfaceLock();

			IW::CBuffer<COLORREF> pLine(nWidth);

			for (int y = 0; y < nHeight; y++)
			{
				// GetLine, not RenderLine: RenderLine composites the pixel over
				// the page background and returns it with a zero alpha byte, so
				// writing the result back made the whole page transparent.
				// Without a palette the two are the same for every other format.
				pLockIn->GetLine(pLine, y, 0, nWidth);
				for (int x = 0; x < nWidth; ++x) pLine[x] = transform.apply_pixel(pLine[x]);
				pLockOut->SetLine(pLine, y, 0, nWidth);

				pStatus->Progress(y, nHeight);

				if (pStatus->QueryCancel())
					return false;
			}
		}

		pageOut.CopyExtraInfo(*pageIn);
	}

	imageOut.Normalize();

	return true;
}

///////////////////////////////////////////////////////////////////////
// Straighten and perspective

namespace iw::legacy_detail::source_imagingedits
{
	// A 3x3 projective matrix, row major. Straighten and the two perspective
	// sliders compose into one of these so the image is resampled once.
	struct Matrix3
	{
		double m[9];

		inline static Matrix3 Identity()
		{
			Matrix3 r = {{1, 0, 0, 0, 1, 0, 0, 0, 1}};
			return r;
		}

		Matrix3 operator*(const Matrix3& o) const
		{
			Matrix3 r = {{0}};

			for (int i = 0; i < 3; i++)
				for (int j = 0; j < 3; j++)
					for (int k = 0; k < 3; k++)
						r.m[i * 3 + j] += m[i * 3 + k] * o.m[k * 3 + j];

			return r;
		}

		inline bool Invert(Matrix3& out) const
		{
			const double a = m[0], b = m[1], c = m[2];
			const double d = m[3], e = m[4], f = m[5];
			const double g = m[6], h = m[7], i = m[8];

			const double A = e * i - f * h;
			const double B = -(d * i - f * g);
			const double C = d * h - e * g;
			const double det = a * A + b * B + c * C;

			if (fabs(det) < 1e-12)
				return false;

			const double id = 1.0 / det;

			out.m[0] = A * id;
			out.m[1] = -(b * i - c * h) * id;
			out.m[2] = (b * f - c * e) * id;
			out.m[3] = B * id;
			out.m[4] = (a * i - c * g) * id;
			out.m[5] = -(a * f - c * d) * id;
			out.m[6] = C * id;
			out.m[7] = -(a * h - b * g) * id;
			out.m[8] = (a * e - b * d) * id;

			return true;
		}
	};

	inline Matrix3 BuildWarp(const ImageEdits& edits, int cx, int cy)
	{
		// Everything happens about the centre, so the frame does not drift when
		// only one slider moves.
		const double halfX = cx / 2.0;
		const double halfY = cy / 2.0;

		Matrix3 toOrigin = Matrix3::Identity();
		toOrigin.m[2] = -halfX;
		toOrigin.m[5] = -halfY;

		Matrix3 fromOrigin = Matrix3::Identity();
		fromOrigin.m[2] = halfX;
		fromOrigin.m[5] = halfY;

		const double angle = (edits._straighten / 10.0) * s_pi / 180.0;
		const double cs = cos(angle);
		const double sn = sin(angle);

		Matrix3 rotate = Matrix3::Identity();
		rotate.m[0] = cs;
		rotate.m[1] = -sn;
		rotate.m[3] = sn;
		rotate.m[4] = cs;

		Matrix3 perspective = Matrix3::Identity();

		if (halfX > 0) perspective.m[6] = (edits._perspectiveH / 100.0) * 0.5 / halfX;
		if (halfY > 0) perspective.m[7] = (edits._perspectiveV / 100.0) * 0.5 / halfY;

		// A projective row makes the image shrink towards one edge; scale it
		// back up about the centre so the frame stays filled.
		const double scale = 1.0 + 0.5 * (fabs(edits._perspectiveH) + fabs(edits._perspectiveV)) / 100.0;

		Matrix3 zoom = Matrix3::Identity();
		zoom.m[0] = scale;
		zoom.m[4] = scale;

		return fromOrigin * (perspective * (zoom * (rotate * toOrigin)));
	}

	// Does the warped picture reach this point of the frame? The inverse map is
	// what the resampler uses, so this asks the empty corners the same question
	// they are going to be asked a pixel at a time.
	inline bool IsCovered(const Matrix3& inverse, double x, double y, int cx, int cy)
	{
		const double w = inverse.m[6] * x + inverse.m[7] * y + inverse.m[8];

		if (fabs(w) < 1e-9)
			return false;

		const double sx = (inverse.m[0] * x + inverse.m[1] * y + inverse.m[2]) / w;
		const double sy = (inverse.m[3] * x + inverse.m[4] * y + inverse.m[5]) / w;

		return sx >= 0 && sy >= 0 && sx <= cx && sy <= cy;
	}

	inline bool IsRectCovered(const Matrix3& inverse, const CRect& rc, int cx, int cy)
	{
		constexpr int nSteps = 16;

		for (int i = 0; i <= nSteps; i++)
		{
			const double x = rc.left + rc.Width() * i / static_cast<double>(nSteps);
			const double y = rc.top + rc.Height() * i / static_cast<double>(nSteps);

			if (!IsCovered(inverse, x, rc.top, cx, cy) ||
				!IsCovered(inverse, x, rc.bottom, cx, cy) ||
				!IsCovered(inverse, rc.left, y, cx, cy) ||
				!IsCovered(inverse, rc.right, y, cx, cy))
				return false;
		}

		return true;
	}

	inline CRect ShrinkToWarpedPicture(const ImageEdits& edits, CSize size, const CRect& rc)
	{
		Matrix3 inverse;

		if (!BuildWarp(edits, size.cx, size.cy).Invert(inverse))
			return rc;

		// Every one of these warps keeps the centre of the frame where it is,
		// so that is the one point known to be on the picture to grow from.
		const CPoint pt = rc.CenterPoint();
		CRect rcOut(pt.x, pt.y, pt.x + 1, pt.y + 1);

		if (!IsRectCovered(inverse, rcOut, size.cx, size.cy))
			return rcOut;

		// One side at a time, each as far as it will go. Moving all four
		// together would stop at whichever corner ran out first and give away
		// the rest of the picture with it.
		auto grow = [&](LONG& edge, LONG nTarget)
		{
			const LONG nStart = edge;
			const int nRange = static_cast<int>(nTarget - nStart);

			edge = nTarget;

			if (IsRectCovered(inverse, rcOut, size.cx, size.cy))
				return;

			int lo = 0, hi = 1000;	// permille of the way out to nTarget

			while (hi - lo > 1)
			{
				const int nMid = (lo + hi) / 2;
				edge = nStart + MulDiv(nRange, nMid, 1000);

				if (IsRectCovered(inverse, rcOut, size.cx, size.cy))
					lo = nMid;
				else
					hi = nMid;
			}

			edge = nStart + MulDiv(nRange, lo, 1000);
		};

		grow(rcOut.left, rc.left);
		grow(rcOut.right, rc.right);
		grow(rcOut.top, rc.top);
		grow(rcOut.bottom, rc.bottom);

		return rcOut;
	}

	// One lerp of two packed RGBA pixels, t in 0..256. The two byte pairs are
	// held apart so a 16 bit product cannot carry out of its own channel.
	inline COLORREF Lerp(COLORREF a, COLORREF b, unsigned t)
	{
		const unsigned it = 256 - t;

		const COLORREF lo = ((((a & 0x00FF00FF) * it) + ((b & 0x00FF00FF) * t)) >> 8) & 0x00FF00FF;
		const COLORREF hi = ((((a >> 8) & 0x00FF00FF) * it) + (((b >> 8) & 0x00FF00FF) * t)) & 0xFF00FF00;

		return lo | hi;
	}

	// 255 / a, in 16.16. A premultiplied channel is never greater than the
	// alpha it was multiplied by, so channel * table[a] cannot overflow.
	struct UnpremultiplyTable
	{
		unsigned v[256];

		UnpremultiplyTable()
		{
			v[0] = 0;

			for (int i = 1; i < 256; i++)
				v[i] = (255u << 16) / static_cast<unsigned>(i);
		}
	};

	inline COLORREF Unpremultiply(COLORREF c)
	{
		static const UnpremultiplyTable table;

		const unsigned a = IW::GetA(c);

		if (a == 0)
			return 0;

		if (a == 255)
			return c;

		const unsigned k = table.v[a];

		return IW::RGBA(static_cast<int>((IW::GetR(c) * k + 0x8000) >> 16),
		                static_cast<int>((IW::GetG(c) * k + 0x8000) >> 16),
		                static_cast<int>((IW::GetB(c) * k + 0x8000) >> 16),
		                static_cast<int>(a));
	}

	// Bilinear, in 16.16 source coordinates, over PREMULTIPLIED source pixels.
	//
	// Premultiplied is the fix for the artifact: interpolating straight alpha
	// averages the RGB of a fully transparent pixel into its neighbours, so any
	// image whose transparent area carries a flat colour -- which is most PNGs
	// with an alpha channel -- bled that colour along its own edges as soon as
	// the straighten slider moved.
	//
	// A tap outside the picture contributes nothing at all rather than being
	// clamped to the nearest edge pixel, which would end the wedge on a fully
	// opaque pixel of smeared edge colour instead of one pixel of alpha ramp.
	inline COLORREF SampleWarp(const COLORREF* p, int cx, int cy, int x16, int y16)
	{
		const int x0 = x16 >> 16;
		const int y0 = y16 >> 16;

		if (x0 < -1 || y0 < -1 || x0 >= cx || y0 >= cy)
			return 0;

		const unsigned fx = (x16 >> 8) & 0xFF;
		const unsigned fy = (y16 >> 8) & 0xFF;

		COLORREF c00 = 0, c01 = 0, c10 = 0, c11 = 0;

		if (x0 >= 0 && y0 >= 0 && x0 + 1 < cx && y0 + 1 < cy)
		{
			const COLORREF* q = p + static_cast<size_t>(y0) * cx + x0;

			c00 = q[0];
			c01 = q[1];
			c10 = q[cx];
			c11 = q[cx + 1];
		}
		else
		{
			const bool bLeft = x0 >= 0 && x0 < cx;
			const bool bRight = x0 + 1 >= 0 && x0 + 1 < cx;
			const bool bTop = y0 >= 0 && y0 < cy;
			const bool bBottom = y0 + 1 >= 0 && y0 + 1 < cy;

			const COLORREF* pTop = p + static_cast<size_t>(bTop ? y0 : 0) * cx;
			const COLORREF* pBottom = p + static_cast<size_t>(bBottom ? y0 + 1 : 0) * cx;

			if (bTop && bLeft) c00 = pTop[x0];
			if (bTop && bRight) c01 = pTop[x0 + 1];
			if (bBottom && bLeft) c10 = pBottom[x0];
			if (bBottom && bRight) c11 = pBottom[x0 + 1];
		}

		return Unpremultiply(Lerp(Lerp(c00, c01, fx), Lerp(c10, c11, fx), fy));
	}

	// 16.16, rounded down so the fractional part is always positive.
	inline int ToFixed(double v)
	{
		return static_cast<int>(floor(v * 65536.0));
	}

	// 16.16 cannot hold a coordinate past +/-32768, and the row is walked by
	// repeated addition, so a row that reaches out that far is stepped in
	// double instead. Only a warp of an image tens of thousands of pixels wide
	// can get here.
	constexpr double s_fixedLimit = 30000.0;

	inline bool ApplyWarp(const IW::Image& imageIn, IW::Image& imageOut, const ImageEdits& edits, IW::IStatus* pStatus)
	{
		IW::IterateImageMetaData(imageIn, imageOut, pStatus);

		for (auto pageIn = imageIn.Pages.begin(); pageIn != imageIn.Pages.end(); ++pageIn)
		{
			const int cx = pageIn->GetWidth();
			const int cy = pageIn->GetHeight();

			if (cx <= 0 || cy <= 0)
				continue;

			Matrix3 inverse;

			if (!BuildWarp(edits, cx, cy).Invert(inverse))
				return false;

			// A projective map has no bounded source band per output row, so the
			// page is materialised rather than streamed.
			IW::CBuffer<COLORREF> source(static_cast<size_t>(cx) * cy);

			{
				IW::ConstIImageSurfaceLockPtr pLockIn = pageIn->GetSurfaceLock();

				const bool bAlpha = IW::HasSourceAlpha(*pageIn);

				for (int y = 0; y < cy; y++)
				{
					COLORREF *pLine = source.data() + static_cast<size_t>(y) * cx;

					if (bAlpha)
					{
						IW::ReadStraightLine(*pageIn, pLockIn, pLine, y, 0, cx);

						// The sampler works premultiplied; an opaque source
						// already is, so only this branch has to pay for it.
						for (int x = 0; x < cx; x++)
						{
							const COLORREF c = pLine[x];
							const unsigned a = IW::GetA(c);

							if (a != 255)
								pLine[x] = IW::RGBA(static_cast<int>((IW::GetR(c) * a + 127) / 255),
								                    static_cast<int>((IW::GetG(c) * a + 127) / 255),
								                    static_cast<int>((IW::GetB(c) * a + 127) / 255),
								                    static_cast<int>(a));
						}
					}
					else
					{
						// Neither call promises an alpha byte for a format that
						// has none, and the sampler averages whatever is there.
						pLockIn->RenderLine(pLine, y, 0, cx);

						for (int x = 0; x < cx; x++)
							pLine[x] |= 0xFF000000;
					}
				}
			}

			// The page rect, not just its size: a frame of a multi-page image
			// sits at an offset, and dropping it moves the frame to the corner.
			IW::Page pageOut = imageOut.CreatePage(pageIn->GetPageRect(), IW::PixelFormat::PF32Alpha);
			IW::IImageSurfaceLockPtr pLockOut = pageOut.GetSurfaceLock();

			IW::CBuffer<COLORREF> pLine(cx);

			// Straighten on its own leaves the bottom row of the matrix alone,
			// and that is the case the slider drives: no divide, and the source
			// coordinate can be stepped rather than evaluated.
			const bool bAffine = fabs(inverse.m[6]) < 1e-15 && fabs(inverse.m[7]) < 1e-15;

			for (int y = 0; y < cy; y++)
			{
				const double dy = y + 0.5;

				// Everything below is the row's value at x = 0.5 plus a step.
				const double nx = inverse.m[0] * 0.5 + inverse.m[1] * dy + inverse.m[2];
				const double ny = inverse.m[3] * 0.5 + inverse.m[4] * dy + inverse.m[5];
				const double nw = inverse.m[6] * 0.5 + inverse.m[7] * dy + inverse.m[8];

				bool bStepped = bAffine && fabs(nw) > 1e-9;

				if (bStepped)
				{
					const double invw = 1.0 / nw;
					const double sx = nx * invw - 0.5;
					const double sy = ny * invw - 0.5;
					const double ex = sx + inverse.m[0] * invw * (cx - 1);
					const double ey = sy + inverse.m[3] * invw * (cx - 1);

					bStepped = fabs(sx) < s_fixedLimit && fabs(sy) < s_fixedLimit &&
						fabs(ex) < s_fixedLimit && fabs(ey) < s_fixedLimit;

					if (bStepped)
					{
						int x16 = ToFixed(sx);
						int y16 = ToFixed(sy);

						const int dx16 = ToFixed(inverse.m[0] * invw);
						const int dy16 = ToFixed(inverse.m[3] * invw);

						for (int x = 0; x < cx; x++, x16 += dx16, y16 += dy16)
							pLine[x] = SampleWarp(source, cx, cy, x16, y16);
					}
				}

				if (!bStepped)
				{
					double px = nx, py = ny, pw = nw;

					for (int x = 0; x < cx; x++,
					     px += inverse.m[0], py += inverse.m[3], pw += inverse.m[6])
					{
						if (fabs(pw) < 1e-9)
						{
							pLine[x] = 0;
							continue;
						}

						const double invw = 1.0 / pw;
						const double sx = px * invw - 0.5;
						const double sy = py * invw - 0.5;

						pLine[x] = (fabs(sx) < s_fixedLimit && fabs(sy) < s_fixedLimit)
							           ? SampleWarp(source, cx, cy, ToFixed(sx), ToFixed(sy))
							           : 0;
					}
				}

				pLockOut->SetLine(pLine, y, 0, cx);

				pStatus->Progress(y, cy);

				if (pStatus->QueryCancel())
					return false;
			}

			pageOut.CopyExtraInfo(*pageIn);
		}

		imageOut.Normalize();

		return true;
	}

	inline bool ApplyRotate(const IW::Image& imageIn, IW::Image& imageOut, int quarterTurns, IW::IStatus* pStatus)
	{
		switch (quarterTurns & 3)
		{
		case 1: return IW::Rotate90(imageIn, imageOut, pStatus);
		case 2: return IW::Rotate180(imageIn, imageOut, pStatus);
		case 3: return IW::Rotate270(imageIn, imageOut, pStatus);
		default: break;
		}

		imageOut = imageIn;
		return true;
	}
}
using namespace iw::legacy_detail::source_imagingedits;

inline bool IW::ApplyEditGeometry(const IW::Image& imageIn, IW::Image& imageOut, const ImageEdits& edits, IW::IStatus* pStatus)
{
	if (!edits.HasGeometry())
	{
		imageOut = imageIn;
		return true;
	}

	IW::Image rotated;

	if (!ApplyRotate(imageIn, rotated, edits._rotate, pStatus))
		return false;

	if (edits.HasWarp())
	{
		IW::Image warped;

		if (!ApplyWarp(rotated, warped, edits, pStatus))
			return false;

		rotated = warped;
	}

	if (edits.HasCrop())
	{
		CRect rc;

		if (rc.IntersectRect(rotated.GetBoundingRect(), edits._crop) && !rc.IsRectEmpty())
			return Crop(rotated, imageOut, rc, pStatus);
	}

	imageOut = rotated;
	return true;
}

inline bool IW::ApplyEdits(const IW::Image& imageIn, IW::Image& imageOut, const ImageEdits& edits, IW::IStatus* pStatus)
{
	if (edits.IsEmpty())
	{
		imageOut = imageIn;
		return true;
	}

	if (!edits.HasColor())
		return ApplyEditGeometry(imageIn, imageOut, edits, pStatus);

	if (!edits.HasGeometry())
		return ApplyEditColor(imageIn, imageOut, edits, pStatus);

	IW::Image geometry;

	if (!ApplyEditGeometry(imageIn, geometry, edits, pStatus))
		return false;

	return ApplyEditColor(geometry, imageOut, edits, pStatus);
}

///////////////////////////////////////////////////////////////////////
// Auto adjustments

inline void IW::AutoColor(const IW::Image& imageIn, ImageEdits& edits)
{
	iw::edits::ColorHistogram histogram;
	for (size_t index = 0; index < imageIn.Pages.size(); ++index)
	{
		const auto frame = iw::imaging::image_rows(imageIn, index);
		std::vector<std::uint32_t> pixels(frame.width);
		for (int y = 0; y < static_cast<int>(frame.height); ++y)
		{
			ReadEditRow(frame, y, pixels);
			histogram.add(pixels);
		}
	}
	auto value = EditColorValues(edits);
	if (iw::edits::auto_color(histogram, value))
	{
		edits._brightness = value.brightness;
		edits._contrast = value.contrast;
		edits._temperature = value.temperature;
	}
}

inline void IW::AutoStraighten(const IW::Image& imageIn, ImageEdits& edits)
{
	if (imageIn.IsEmpty()) return;
	const auto frame = iw::imaging::image_rows(imageIn);
	const int frameWidth = static_cast<int>(frame.width);
	const int frameHeight = static_cast<int>(frame.height);
	if (frameWidth < 8 || frameHeight < 8) return;
	const int step = (std::max)(1, (std::max)(frameWidth, frameHeight) / 384);
	const int width = frameWidth / step;
	const int height = frameHeight / step;
	std::vector<int> luma(static_cast<size_t>(width) * height);
	std::vector<std::uint32_t> row(frame.width);
	for (int y = 0; y < height; ++y)
	{
		ReadEditRow(frame, y * step, row);
		for (int x = 0; x < width; ++x)
		{
			const auto pixel = row[x * step];
			luma[static_cast<size_t>(y) * width + x] = (pixel >> 24) == 0 ? 0 :
				static_cast<int>(((pixel >> 16 & 255) * 77 + (pixel >> 8 & 255) * 151 + (pixel & 255) * 28) >> 8);
		}
	}
	iw::edits::ImageEdits value;
	if (iw::edits::auto_straighten(luma, {width, height}, value)) edits._straighten = value.straighten;
}

// ---- ImagingLock ----
// ImageWalker by Zac Walker
//
// Purpose: The one runtime pixel-format switch, mapping a page onto the
//          concrete lock that reads it. Everything else lives in ImagingLock.h.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#include "LegacyPch.h"
#include "ImagingLock.h"

inline const IW::IImageSurfaceLock* IW::Page::GetSurfaceLock() const
{
	return const_cast<IW::Page*>(this)->GetSurfaceLock();
};

inline IW::IImageSurfaceLock* IW::Page::GetSurfaceLock()
{
	switch (GetPixelFormat()._pf)
	{
	case IW::PixelFormat::PF1:
		return new RefObj<SurfaceLockRef<IW::PixelFormat::PF1>>(*this);
	case IW::PixelFormat::PF2:
		return new RefObj<SurfaceLockRef<IW::PixelFormat::PF2>>(*this);
	case IW::PixelFormat::PF4:
		return new RefObj<SurfaceLockRef<IW::PixelFormat::PF4>>(*this);
	case IW::PixelFormat::PF8:
		return new RefObj<SurfaceLockRef<IW::PixelFormat::PF8>>(*this);
	case IW::PixelFormat::PF8Alpha:
		return new RefObj<SurfaceLockRef<IW::PixelFormat::PF8Alpha>>(*this);
	case IW::PixelFormat::PF8GrayScale:
		return new RefObj<SurfaceLockRef<IW::PixelFormat::PF8GrayScale>>(*this);
	case IW::PixelFormat::PF555:
		return new RefObj<SurfaceLockRef<IW::PixelFormat::PF555>>(*this);
	case IW::PixelFormat::PF565:
		return new RefObj<SurfaceLockRef<IW::PixelFormat::PF565>>(*this);
	case IW::PixelFormat::PF24:
		return new RefObj<SurfaceLockRef<IW::PixelFormat::PF24>>(*this);
	case IW::PixelFormat::PF32:
		return new RefObj<SurfaceLockRef<IW::PixelFormat::PF32>>(*this);
	case IW::PixelFormat::PF32Alpha:
		return new RefObj<SurfaceLockRef<IW::PixelFormat::PF32Alpha>>(*this);
	}

	return nullptr;
};

// ---- ImagingQuantize ----
// ImageWalker by Zac Walker
//
// Purpose: Wu colour quantization - reduces a true colour image to a
//          palette.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

///////////////////////////////////////////////////////////////////////
//	    C Implementation of Wu's Color Quantizer (v. 2)
//	    (see Graphics Gems vol. II, pp. 126-133)
//
// Author:	Xiaolin Wu
// Dept. of Computer Science
// Univ. of Western Ontario
// London, Ontario N6A 5B7
// wu@csd.uwo.ca
//
// Algorithm: Greedy orthogonal bipartition of RGB space for variance
// 	   minimization aided by inclusion-exclusion tricks.
// 	   For speed no nearest neighbor search is done. Slightly
// 	   better performance can be expected by more sophisticated
// 	   but more expensive versions.
//
// The author thanks Tom Lane at Tom_Lane@render.GP.CS.CMU.EDU for much of
// additional documentation and a cure to a previous bug.
//
// IW::Free to distribute, comments and suggestions are appreciated.
///////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////
// History
// -------
// July 2000: C++ Implementation of Wu's Color Quantizer
//            and adaptation for the IW::FreeImage 2 Library
//            Author: Herv� Drolon (drolon@iut.univ-lehavre.fr)
///////////////////////////////////////////////////////////////////////

#include "LegacyPch.h"

using Box = struct
{
	int r0; // min value, exclusive
	int r1; // max value, inclusive
	int g0;
	int g1;
	int b0;
	int b1;
	int vol;
};


class CQuantizer
{
protected:
	double* m_pGM2;
	IW::Int64 *m_pnWT, *m_pnMR, *m_pnMG, *m_pnMB;
	IW::UInt16* m_pnQadd;

	// DIB data
	int m_nWidth, m_nHeight;
	IW::Page m_page;

	void Hist3D(IW::Int64* vwt, IW::Int64* vmr, IW::Int64* vmg, IW::Int64* vmb, double* m2);
	void M3D(IW::Int64* vwt, IW::Int64* vmr, IW::Int64* vmg, IW::Int64* vmb, double* m2);
	IW::Int64 Vol(Box* cube, IW::Int64* mmt);
	IW::Int64 Bottom(Box* cube, IW::UInt8 dir, IW::Int64* mmt);
	IW::Int64 Top(Box* cube, IW::UInt8 dir, int pos, IW::Int64* mmt);
	double Var(Box* cube);
	double Maximize(Box* cube, IW::UInt8 dir, int first, int last, int* cut,
	               IW::Int64 whole_r, IW::Int64 whole_g, IW::Int64 whole_b, IW::Int64 whole_w);
	bool Cut(Box* set1, Box* set2);
	void Mark(Box* cube, int label, IW::UInt8* tag);

public:
	// Constructor - Input parameter: DIB 24-bit to be quantized
	CQuantizer(const IW::Page& page);
	// Destructor
	~CQuantizer();
	// Quantizer - Return value: quantized 8-bit (color palette) DIB
	bool Quantize(IW::Image& imageOut);
};

///////////////////////////////////////////////////////////////////////

// 3D array indexation

#define INDEX(r, g, b)	((r << 10) + (r << 6) + r + (g << 5) + g + b)

#define MAXCOLOR	256
#define	RED		2
#define	GREEN	1
#define BLUE	0


// Constructor / Destructor
inline CQuantizer::CQuantizer(const IW::Page& page)
{
	m_page = page;
	m_nWidth = page.GetWidth();
	m_nHeight = page.GetHeight();


	m_pGM2 = nullptr;
	m_pnWT = m_pnMR = m_pnMG = m_pnMB = nullptr;
	m_pnQadd = nullptr;

	// Allocate 3D arrays
	m_pGM2 = static_cast<double*>(IW::Alloc(33 * 33 * 33 * sizeof(double)));
	m_pnWT = static_cast<IW::Int64*>(IW::Alloc(33 * 33 * 33 * sizeof(IW::Int64)));
	m_pnMR = static_cast<IW::Int64*>(IW::Alloc(33 * 33 * 33 * sizeof(IW::Int64)));
	m_pnMG = static_cast<IW::Int64*>(IW::Alloc(33 * 33 * 33 * sizeof(IW::Int64)));
	m_pnMB = static_cast<IW::Int64*>(IW::Alloc(33 * 33 * 33 * sizeof(IW::Int64)));

	// Allocate m_pnQadd
	m_pnQadd = static_cast<IW::UInt16*>(IW::Alloc(sizeof(IW::UInt16) * m_nWidth * m_nHeight));

	IW::MemZero(m_pGM2, 35937 * sizeof(double));
	IW::MemZero(m_pnWT, 35937 * sizeof(IW::Int64));
	IW::MemZero(m_pnMR, 35937 * sizeof(IW::Int64));
	IW::MemZero(m_pnMG, 35937 * sizeof(IW::Int64));
	IW::MemZero(m_pnMB, 35937 * sizeof(IW::Int64));
	IW::MemZero(m_pnQadd, sizeof(IW::UInt16) * m_nWidth * m_nHeight);
}

inline CQuantizer::~CQuantizer()
{
	if (m_pGM2) IW::Free(m_pGM2);
	if (m_pnWT) IW::Free(m_pnWT);
	if (m_pnMR) IW::Free(m_pnMR);
	if (m_pnMG) IW::Free(m_pnMG);
	if (m_pnMB) IW::Free(m_pnMB);
	if (m_pnQadd) IW::Free(m_pnQadd);
}


// Histogram is in elements 1..HISTSIZE along each axis,
// element 0 is for base or marginal value
// NB: these must start out 0!

// Build 3-D color histogram of counts, r/g/b, c^2
inline void CQuantizer::Hist3D(IW::Int64* vwt, IW::Int64* vmr, IW::Int64* vmg, IW::Int64* vmb, double* m2)
{
	int ind;
	int inr, ing, inb, table[256];
	int i;
	int y, x;

	for (i = 0; i < 256; i++)
		table[i] = i * i;


	IW::ConstIImageSurfaceLockPtr pLock = m_page.GetSurfaceLock();
	COLORREF rr, gg, bb, c;
	const DWORD* p;
	IW::CBuffer<DWORD> pLine(m_nWidth);

	for (y = 0; y < m_nHeight; y++)
	{
		pLock->RenderLine(pLine, y, 0, m_nWidth);
		p = pLine;

		for (x = 0; x < m_nWidth; x++)
		{
			c = *p++;

			rr = IW::GetR(c);
			gg = IW::GetG(c);
			bb = IW::GetB(c);

			inr = (bb >> 3) + 1;
			ing = (gg >> 3) + 1;
			inb = (rr >> 3) + 1;

			// Each component is in [1, 32], so INDEX is at most 35936.
			ind = INDEX(inr, ing, inb);
			m_pnQadd[static_cast<size_t>(y) * m_nWidth + x] = static_cast<IW::UInt16>(ind);

			// [inr][ing][inb]
			vwt[ind]++;
			vmr[ind] += bb;
			vmg[ind] += gg;
			vmb[ind] += rr;

			m2[ind] += static_cast<double>(table[bb] +
				table[gg] +
				table[rr]);
		}
	}
}


// At conclusion of the histogram step, we can interpret
// m_pnWT[r][g][b] = sum over voxel of P(c)
// m_pnMR[r][g][b] = sum over voxel of r*P(c)  ,  similarly for m_pnMG, m_pnMB
// m2[r][g][b] = sum over voxel of c^2*P(c)
// Actually each of these should be divided by 'ImageSize' to give the usual
// interpretation of P() as ranging from 0 to 1, but we needn't do that here.


// We now convert histogram into moments so that we can rapidly calculate
// the sums of the above quantities over any desired box.

// Compute cumulative moments
inline void CQuantizer::M3D(IW::Int64* vwt, IW::Int64* vmr, IW::Int64* vmg, IW::Int64* vmb, double* m2)
{
	IW::UInt16 ind1, ind2;
	IW::UInt8 i, r, g, b;
	IW::Int64 line, line_r, line_g, line_b;
	IW::Int64 area[33], area_r[33], area_g[33], area_b[33];
	double line2, area2[33];

	for (r = 1; r <= 32; r++)
	{
		for (i = 0; i <= 32; i++)
		{
			area2[i] = 0;
			area[i] = area_r[i] = area_g[i] = area_b[i] = 0;
		}
		for (g = 1; g <= 32; g++)
		{
			line2 = 0;
			line = line_r = line_g = line_b = 0;
			for (b = 1; b <= 32; b++)
			{
				ind1 = INDEX(r, g, b); // [r][g][b]
				line += vwt[ind1];
				line_r += vmr[ind1];
				line_g += vmg[ind1];
				line_b += vmb[ind1];
				line2 += m2[ind1];
				area[b] += line;
				area_r[b] += line_r;
				area_g[b] += line_g;
				area_b[b] += line_b;
				area2[b] += line2;
				ind2 = ind1 - 1089; // [r-1][g][b]
				vwt[ind1] = vwt[ind2] + area[b];
				vmr[ind1] = vmr[ind2] + area_r[b];
				vmg[ind1] = vmg[ind2] + area_g[b];
				vmb[ind1] = vmb[ind2] + area_b[b];
				m2[ind1] = m2[ind2] + area2[b];
			}
		}
	}
}

// Compute sum over a box of any given statistic
inline IW::Int64 CQuantizer::Vol(Box* cube, IW::Int64* mmt)
{
	return (mmt[INDEX(cube->r1, cube->g1, cube->b1)]
		- mmt[INDEX(cube->r1, cube->g1, cube->b0)]
		- mmt[INDEX(cube->r1, cube->g0, cube->b1)]
		+ mmt[INDEX(cube->r1, cube->g0, cube->b0)]
		- mmt[INDEX(cube->r0, cube->g1, cube->b1)]
		+ mmt[INDEX(cube->r0, cube->g1, cube->b0)]
		+ mmt[INDEX(cube->r0, cube->g0, cube->b1)]
		- mmt[INDEX(cube->r0, cube->g0, cube->b0)]);
}

// The next two routines allow a slightly more efficient calculation
// of Vol() for a proposed subbox of a given box.  The sum of Top()
// and Bottom() is the Vol() of a subbox split in the given direction
// and with the specified new upper bound.


// Compute part of Vol(cube, mmt) that doesn't depend on r1, g1, or b1
// (depending on dir)
inline IW::Int64 CQuantizer::Bottom(Box* cube, IW::UInt8 dir, IW::Int64* mmt)
{
	switch (dir)
	{
	case RED:
		return (-mmt[INDEX(cube->r0, cube->g1, cube->b1)]
			+ mmt[INDEX(cube->r0, cube->g1, cube->b0)]
			+ mmt[INDEX(cube->r0, cube->g0, cube->b1)]
			- mmt[INDEX(cube->r0, cube->g0, cube->b0)]);
		break;
	case GREEN:
		return (-mmt[INDEX(cube->r1, cube->g0, cube->b1)]
			+ mmt[INDEX(cube->r1, cube->g0, cube->b0)]
			+ mmt[INDEX(cube->r0, cube->g0, cube->b1)]
			- mmt[INDEX(cube->r0, cube->g0, cube->b0)]);
		break;
	case BLUE:
		return (-mmt[INDEX(cube->r1, cube->g1, cube->b0)]
			+ mmt[INDEX(cube->r1, cube->g0, cube->b0)]
			+ mmt[INDEX(cube->r0, cube->g1, cube->b0)]
			- mmt[INDEX(cube->r0, cube->g0, cube->b0)]);
		break;
	}

	return 0;
}


// Compute remainder of Vol(cube, mmt), substituting pos for
// r1, g1, or b1 (depending on dir)
inline IW::Int64 CQuantizer::Top(Box* cube, IW::UInt8 dir, int pos, IW::Int64* mmt)
{
	switch (dir)
	{
	case RED:
		return (mmt[INDEX(pos, cube->g1, cube->b1)]
			- mmt[INDEX(pos, cube->g1, cube->b0)]
			- mmt[INDEX(pos, cube->g0, cube->b1)]
			+ mmt[INDEX(pos, cube->g0, cube->b0)]);
		break;
	case GREEN:
		return (mmt[INDEX(cube->r1, pos, cube->b1)]
			- mmt[INDEX(cube->r1, pos, cube->b0)]
			- mmt[INDEX(cube->r0, pos, cube->b1)]
			+ mmt[INDEX(cube->r0, pos, cube->b0)]);
		break;
	case BLUE:
		return (mmt[INDEX(cube->r1, cube->g1, pos)]
			- mmt[INDEX(cube->r1, cube->g0, pos)]
			- mmt[INDEX(cube->r0, cube->g1, pos)]
			+ mmt[INDEX(cube->r0, cube->g0, pos)]);
		break;
	}

	return 0;
}

// Compute the weighted variance of a box
// NB: as with the raw statistics, this is really the variance * ImageSize
inline double CQuantizer::Var(Box* cube)
{
	double dr = static_cast<double>(Vol(cube, m_pnMR));
	double dg = static_cast<double>(Vol(cube, m_pnMG));
	double db = static_cast<double>(Vol(cube, m_pnMB));
	double xx = m_pGM2[INDEX(cube->r1, cube->g1, cube->b1)]
		- m_pGM2[INDEX(cube->r1, cube->g1, cube->b0)]
		- m_pGM2[INDEX(cube->r1, cube->g0, cube->b1)]
		+ m_pGM2[INDEX(cube->r1, cube->g0, cube->b0)]
		- m_pGM2[INDEX(cube->r0, cube->g1, cube->b1)]
		+ m_pGM2[INDEX(cube->r0, cube->g1, cube->b0)]
		+ m_pGM2[INDEX(cube->r0, cube->g0, cube->b1)]
		- m_pGM2[INDEX(cube->r0, cube->g0, cube->b0)];

	return (xx - (dr * dr + dg * dg + db * db) / static_cast<double>(Vol(cube, m_pnWT)));
}

// We want to minimize the sum of the variances of two subboxes.
// The sum(c^2) terms can be ignored since their sum over both subboxes
// is the same (the sum for the whole box) no matter where we split.
// The remaining terms have a minus sign in the variance formula,
// so we drop the minus sign and MAXIMIZE the sum of the two terms.
inline double CQuantizer::Maximize(Box* cube, IW::UInt8 dir, int first, int last, int* cut, IW::Int64 whole_r,
                           IW::Int64 whole_g, IW::Int64 whole_b, IW::Int64 whole_w)
{
	IW::Int64 half_r, half_g, half_b, half_w;
	int i;
	double temp;

	IW::Int64 base_r = Bottom(cube, dir, m_pnMR);
	IW::Int64 base_g = Bottom(cube, dir, m_pnMG);
	IW::Int64 base_b = Bottom(cube, dir, m_pnMB);
	IW::Int64 base_w = Bottom(cube, dir, m_pnWT);

	double max = 0.0;

	*cut = -1;

	for (i = first; i < last; i++)
	{
		half_r = base_r + Top(cube, dir, i, m_pnMR);
		half_g = base_g + Top(cube, dir, i, m_pnMG);
		half_b = base_b + Top(cube, dir, i, m_pnMB);
		half_w = base_w + Top(cube, dir, i, m_pnWT);

		// now half_x is sum over lower half of box, if split at i

		if (half_w == 0)
		{
			// subbox could be empty of pixels!
			continue; // never split into an empty box
		}
		temp = (static_cast<double>(half_r) * static_cast<double>(half_r) +
			static_cast<double>(half_g) * static_cast<double>(half_g) +
			static_cast<double>(half_b) * static_cast<double>(half_b)) / static_cast<double>(half_w);

		half_r = whole_r - half_r;
		half_g = whole_g - half_g;
		half_b = whole_b - half_b;
		half_w = whole_w - half_w;

		if (half_w == 0)
		{
			// subbox could be empty of pixels!
			continue; // never split into an empty box
		}
		temp += (static_cast<double>(half_r) * static_cast<double>(half_r) +
			static_cast<double>(half_g) * static_cast<double>(half_g) +
			static_cast<double>(half_b) * static_cast<double>(half_b)) / static_cast<double>(half_w);

		if (temp > max)
		{
			max = temp;
			*cut = i;
		}
	}

	return max;
}

inline bool CQuantizer::Cut(Box* set1, Box* set2)
{
	IW::UInt8 dir;
	int cutr, cutg, cutb;

	IW::Int64 whole_r = Vol(set1, m_pnMR);
	IW::Int64 whole_g = Vol(set1, m_pnMG);
	IW::Int64 whole_b = Vol(set1, m_pnMB);
	IW::Int64 whole_w = Vol(set1, m_pnWT);

	double maxr = Maximize(set1, RED, set1->r0 + 1, set1->r1, &cutr, whole_r, whole_g, whole_b, whole_w);
	double maxg = Maximize(set1, GREEN, set1->g0 + 1, set1->g1, &cutg, whole_r, whole_g, whole_b, whole_w);
	double maxb = Maximize(set1, BLUE, set1->b0 + 1, set1->b1, &cutb, whole_r, whole_g, whole_b, whole_w);

	if ((maxr >= maxg) && (maxr >= maxb))
	{
		dir = RED;

		if (cutr < 0)
		{
			return false; // can't split the box
		}
	}
	else if ((maxg >= maxr) && (maxg >= maxb))
	{
		dir = GREEN;
	}
	else
	{
		dir = BLUE;
	}

	set2->r1 = set1->r1;
	set2->g1 = set1->g1;
	set2->b1 = set1->b1;

	switch (dir)
	{
	case RED:
		set2->r0 = set1->r1 = cutr;
		set2->g0 = set1->g0;
		set2->b0 = set1->b0;
		break;

	case GREEN:
		set2->g0 = set1->g1 = cutg;
		set2->r0 = set1->r0;
		set2->b0 = set1->b0;
		break;

	case BLUE:
		set2->b0 = set1->b1 = cutb;
		set2->r0 = set1->r0;
		set2->g0 = set1->g0;
		break;
	}

	set1->vol = (set1->r1 - set1->r0) * (set1->g1 - set1->g0) * (set1->b1 - set1->b0);
	set2->vol = (set2->r1 - set2->r0) * (set2->g1 - set2->g0) * (set2->b1 - set2->b0);

	return true;
}


inline void CQuantizer::Mark(Box* cube, int label, IW::UInt8* tag)
{
	for (int r = cube->r0 + 1; r <= cube->r1; r++)
	{
		for (int g = cube->g0 + 1; g <= cube->g1; g++)
		{
			for (int b = cube->b0 + 1; b <= cube->b1; b++)
			{
				// Quantize assigns fewer than MAXCOLOR (256) palette labels.
				tag[INDEX(r, g, b)] = static_cast<IW::UInt8>(label);
			}
		}
	}
}


// Wu Quantization algorithm
inline bool IW::Quantize(const IW::Image& imageIn, IW::Image& imageOut, IW::IStatus* pStatus)
{
	for (auto pageIn = imageIn.Pages.begin(); pageIn != imageIn.Pages.end(); ++pageIn)
	{
		if (pStatus->QueryCancel()) return false;
		CQuantizer q(*pageIn);

		if (!q.Quantize(imageOut))
			return false;

		if (pStatus->QueryCancel())
			return false;
	}

	imageOut.Normalize();

	return true;
}


inline bool CQuantizer::Quantize(IW::Image& imageOut)
{
	int PalSize = 254; // Color look-up table size
	Box cube[MAXCOLOR];
	int next;
	int i;
	IW::Int64 weight;
	int k;
	double vv[MAXCOLOR], temp;

	// Compute 3D histogram
	Hist3D(m_pnWT, m_pnMR, m_pnMG, m_pnMB, m_pGM2);

	// Compute moments

	M3D(m_pnWT, m_pnMR, m_pnMG, m_pnMB, m_pGM2);

	cube[0].r0 = cube[0].g0 = cube[0].b0 = 0;
	cube[0].r1 = cube[0].g1 = cube[0].b1 = 32;
	next = 0;

	for (i = 1; i < PalSize; i++)
	{
		if (Cut(&cube[next], &cube[i]))
		{
			// volume test ensures we won't try to cut one-cell box
			vv[next] = (cube[next].vol > 1) ? Var(&cube[next]) : 0;
			vv[i] = (cube[i].vol > 1) ? Var(&cube[i]) : 0;
		}
		else
		{
			vv[next] = 0.0; // don't try to split this box again
			i--; // didn't create box i
		}

		next = 0;
		temp = vv[0];

		for (k = 1; k <= i; k++)
		{
			if (vv[k] > temp)
			{
				temp = vv[k];
				next = k;
			}
		}

		if (temp <= 0.0)
		{
			PalSize = i + 1;

			// Error: "Only got 'PalSize' boxes"

			break;
		}
	}

	// Partition done

	// the space for array m_pGM2 can be IW::Freed now

	IW::Free(m_pGM2);

	m_pGM2 = nullptr;

	// Allocate a new pImage
	IW::Page pageOut = imageOut.CreatePage(m_page.GetPageRect(), IW::PixelFormat::PF8);

	// create an optimized palette
	auto new_pal = (RGBQUAD*)pageOut.GetPalette();

	IW::CBuffer<IW::UInt8> tag(33 * 33 * 33);

	for (k = 0; k < PalSize; k++)
	{
		Mark(&cube[k], k, tag);
		weight = Vol(&cube[k], m_pnWT);

		if (weight)
		{
			new_pal[k].rgbRed = static_cast<IW::UInt8>(Vol(&cube[k], m_pnMR) / weight);
			new_pal[k].rgbGreen = static_cast<IW::UInt8>(Vol(&cube[k], m_pnMG) / weight);
			new_pal[k].rgbBlue = static_cast<IW::UInt8>(Vol(&cube[k], m_pnMB) / weight);
		}
		else
		{
			// Error: bogus box 'k'

			new_pal[k].rgbRed = new_pal[k].rgbGreen = new_pal[k].rgbBlue = 0;
		}

		// The fourth byte of a palette entry is read as alpha downstream.
			new_pal[k].rgbReserved = 0xFF;
	}


	for (int y = 0; y < m_nHeight; y++)
	{
		IW::UInt8* new_bits = pageOut.GetBitmapLine(y);

		for (int x = 0; x < m_nWidth; x++)
		{
			int n = m_pnQadd[static_cast<size_t>(y) * m_nWidth + x];
			new_bits[x] = tag[n];
		}
	}

	// output 'new_pal' as color look-up table contents,
	// 'new_bits' as the quantized image (array of table addresses).

	return true;
}

// ---- ImagingRotate ----
// ImageWalker by Zac Walker
//
// Purpose: Right-angle rotation and the three-shear rotation by an arbitrary
//          angle.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#include "LegacyPch.h"
#include <iw/imaging/Pixel.h>
#include "ImagingLock.h"
#include "ImagingStreams.h"
#include <type_traits>

namespace iw::legacy_detail::source_imagingrotate
{
	// Right-angle rotation, a tile of source rows at a time. The output is
	// written as SetLine runs rather than a call per pixel, and the tile keeps
	// the transpose inside the cache.
	template<bool TClockwise, class TLockOut>
	inline void TransposePage(const IW::IImageSurfaceLock* pLockIn, TLockOut& lockOut,
	                   const int nWidth, const int nHeight, IW::IStatus* pStatus)
	{
		// 64 rows, but capped by bytes as well: a panorama is wide enough that a
		// row count alone would turn this into a multi-megabyte scratch buffer.
		const int nRowBytes = IW::Max(1, static_cast<int>(nWidth * sizeof(COLORREF)));
		const int nTile = IW::Min(IW::Min(64, IW::Max(1, (1024 * 1024) / nRowBytes)), nHeight);

		IW::CBuffer<COLORREF> tile(static_cast<size_t>(nTile) * nWidth);
		IW::CBuffer<COLORREF> run(nTile);

		for (int y0 = 0; y0 < nHeight; y0 += nTile)
		{
			const int n = IW::Min(nTile, nHeight - y0);

			for (int j = 0; j < n; j++)
			{
				pLockIn->GetLine(tile.data() + (static_cast<size_t>(j) * nWidth), y0 + j, 0, nWidth);
			}

			for (int x = 0; x < nWidth; x++)
			{
				if constexpr (TClockwise)
				{
					// Destination column is nHeight-1-y, so the run walks the tile backwards.
					for (int j = 0; j < n; j++)
						run[j] = tile[(static_cast<size_t>(n - 1 - j) * nWidth) + x];

					const auto target = iw::imaging::rotated_point(x, y0 + n - 1, nWidth, nHeight, 1);
					lockOut.SetLine(run, target.y, target.x, n);
				}
				else
				{
					for (int j = 0; j < n; j++)
						run[j] = tile[(static_cast<size_t>(j) * nWidth) + x];

					const auto target = iw::imaging::rotated_point(x, y0, nWidth, nHeight, 3);
					lockOut.SetLine(run, target.y, target.x, n);
				}
			}

			pStatus->Progress(y0 + n, nHeight);
		}
	}
}
using namespace iw::legacy_detail::source_imagingrotate;

inline bool IW::Rotate90(const IW::Image& imageIn, IW::Image& imageOut, IW::IStatus* pStatus)
{
	for (auto it = imageIn.Pages.begin(); it != imageIn.Pages.end(); ++it)
	{
		const IW::Page& pageIn = *it;
		if (pStatus->QueryCancel()) return false;

		CRect rcOrg = pageIn.GetPageRect();
		CRect rc(rcOrg.top, rcOrg.left, rcOrg.bottom, rcOrg.right);
		IW::PixelFormat pf = pageIn.GetPixelFormat();
		IW::Page pageOut = imageOut.CreatePage(rc, pf);
		int nPaletteEntries = pf.NumberOfPaletteEntries();

		if (nPaletteEntries != 0)
		{
			IW::MemCopy(pageOut.GetPalette(), pageIn.GetPalette(), nPaletteEntries * sizeof(RGBQUAD));
		}

		IW::ConstIImageSurfaceLockPtr pLockIn = pageIn.GetSurfaceLock();

		const int nWidth = pageIn.GetWidth();
		const int nHeight = pageIn.GetHeight();

		WithSurfaceLock(pageOut, [&](auto& lockOut)
		{
			TransposePage<true>(pLockIn, lockOut, nWidth, nHeight, pStatus);
		});

		pageOut.CopyExtraInfo(pageIn);
	}

	imageOut.Normalize();
	IterateImageMetaData(imageIn, imageOut, pStatus);

	return true;
}

inline bool IW::Rotate270(const IW::Image& imageIn, IW::Image& imageOut, IW::IStatus* pStatus)
{
	for (auto it = imageIn.Pages.begin(); it != imageIn.Pages.end(); ++it)
	{
		const IW::Page& pageIn = *it;
		if (pStatus->QueryCancel()) return false;

		CRect rcOrg = pageIn.GetPageRect();
		CRect rc(rcOrg.top, rcOrg.left, rcOrg.bottom, rcOrg.right);
		IW::PixelFormat pf = pageIn.GetPixelFormat();
		IW::Page pageOut = imageOut.CreatePage(rc, pf);
		int nPaletteEntries = pf.NumberOfPaletteEntries();

		if (nPaletteEntries != 0)
		{
			IW::MemCopy(pageOut.GetPalette(), pageIn.GetPalette(), nPaletteEntries * sizeof(RGBQUAD));
		}

		IW::ConstIImageSurfaceLockPtr pLockIn = pageIn.GetSurfaceLock();

		const int nWidth = pageIn.GetWidth();
		const int nHeight = pageIn.GetHeight();

		WithSurfaceLock(pageOut, [&](auto& lockOut)
		{
			TransposePage<false>(pLockIn, lockOut, nWidth, nHeight, pStatus);
		});

		pageOut.CopyExtraInfo(pageIn);
	}

	imageOut.Normalize();
	IterateImageMetaData(imageIn, imageOut, pStatus);

	return true;
}


inline bool IW::Rotate180(const IW::Image& imageIn, IW::Image& imageOut, IW::IStatus* pStatus)
{
	for (auto it = imageIn.Pages.begin(); it != imageIn.Pages.end(); ++it)
	{
		const IW::Page& pageIn = *it;
		if (pStatus->QueryCancel()) return false;

		CRect rcOrg = pageIn.GetPageRect();
		CRect rc(rcOrg.left, rcOrg.top, rcOrg.right, rcOrg.bottom);
		IW::PixelFormat pf = pageIn.GetPixelFormat();
		IW::Page pageOut = imageOut.CreatePage(rc, pf);
		int nPaletteEntries = pf.NumberOfPaletteEntries();

		if (nPaletteEntries != 0)
		{
			IW::MemCopy(pageOut.GetPalette(), pageIn.GetPalette(), nPaletteEntries * sizeof(RGBQUAD));
		}

		IW::ConstIImageSurfaceLockPtr pLockIn = pageIn.GetSurfaceLock();
		IW::IImageSurfaceLockPtr pLockOut = pageOut.GetSurfaceLock();

		const int nWidth = pageIn.GetWidth();
		const int nHalfWidth = nWidth / 2;
		const int nHeight = pageIn.GetHeight();

		IW::CBuffer<COLORREF> lineBuffer(nWidth + 1);

		for (int y = 0; y < nHeight; y++)
		{
			pLockIn->GetLine(lineBuffer, y, 0, nWidth);

			for (int x = 0; x < nHalfWidth; x++)
			{
				Swap(lineBuffer[x], lineBuffer[nWidth - (x + 1)]);
			}

			pLockOut->SetLine(lineBuffer, nHeight - (y + 1), 0, nWidth);

			pStatus->Progress(y, nHeight);
		}

		pageOut.CopyExtraInfo(pageIn);
	}

	imageOut.Normalize();
	IterateImageMetaData(imageIn, imageOut, pStatus);

	return true;
}


inline COLORREF MinusCOLORREF(const COLORREF a, COLORREF b)
{
	return IW::RGBA(
		IW::GetR(a) - IW::GetR(b),
		IW::GetG(a) - IW::GetG(b),
		IW::GetB(a) - IW::GetB(b),
		IW::GetA(a) - IW::GetA(b));
}


inline int interp2(const unsigned a, const unsigned b, const unsigned w)
{
	return (b + (a - b) * w) >> 8;
}

inline COLORREF interp(COLORREF a, COLORREF b, const unsigned w)
{
	return IW::RGBA(
		interp2(IW::GetR(a), IW::GetR(b), w),
		interp2(IW::GetG(a), IW::GetG(b), w),
		interp2(IW::GetB(a), IW::GetB(b), w),
		interp2(IW::GetA(a), IW::GetA(b), w));
}

inline void SkewLine(LPCOLORREF pLineOut, IW::LPCCOLORREF pLineIn, const int len, const int lenOut, const int iOffset,
              const int weight, const COLORREF clrBack)
{
	// Fill gap left of skew with background
	COLORREF pxlOldLeft = clrBack;
	int n = 0;

	for (n = 0; n < iOffset; n++)
		pLineOut[n] = clrBack;

	for (n = 0; n < len; n++)
	{
		const COLORREF pxlSrc = pLineIn[n];
		const COLORREF pxlLeft = interp(pxlSrc, clrBack, weight);
		const int nOut = n + iOffset;

		if ((nOut >= 0) && (nOut < lenOut))
		{
			pLineOut[nOut] = MinusCOLORREF(pxlSrc, MinusCOLORREF(pxlLeft, pxlOldLeft));
		}

		pxlOldLeft = pxlLeft;
	}

	n = len + iOffset;

	if (n < lenOut)
	{
		pLineOut[n] = pxlOldLeft;
		n++;
	}

	for (; n < lenOut; n++)
		pLineOut[n] = clrBack;
}

// The locks and the two line buffers belong to the caller: this used to build a
// pair of heap locks and a pair of buffers for every single scan line.
template<bool TAlpha, class TLockIn, class TLockOut>
inline void HorizontalSkew(
	const TLockIn* pLockIn,
	TLockOut* pLockOut,
	COLORREF* pLineIn,
	COLORREF* pLineOut,
	const int cx, // Source width
	const int cxOut, // Destination width
	unsigned y, // Row index
	int iOffset, // Skew offset
	int weight, // Relative weight of right COLORREF
	COLORREF clrBack, // Background color
	const IW::Page* sourcePage = nullptr,
	bool writeStraight = false
)
{
	if constexpr (TAlpha)
	{
		if constexpr (std::is_base_of_v<IW::IImageSurfaceLock, TLockIn>)
		{
			IW::ReadStraightLine(*sourcePage, pLockIn, pLineIn, y, 0, cx);
			for (int x = 0; x < cx; ++x) pLineIn[x] = IW::PremultiplyStraight(pLineIn[x]);
		}
		else pLockIn->GetLine(pLineIn, y, 0, cx);
	}
	else pLockIn->RenderLine(pLineIn, y, 0, cx);

	SkewLine(pLineOut, pLineIn, cx, cxOut, iOffset, weight, clrBack);
	if constexpr (TAlpha)
		if (writeStraight)
			for (int x = 0; x < cxOut; ++x) pLineOut[x] = IW::UnpremultiplyStraight(pLineOut[x]);

	pLockOut->SetLine(pLineOut, y, 0, cxOut);
}


// Skews a column vertically (with filtered weights)
// Limited to 45 degree skewing only. Filters two adjacent COLORREFs.
//
// A column cannot be read with GetLine, so this one stays per pixel -- but both
// pages here use 32-bit storage, so the caller passes a concrete lock and the access
// inlines to a load and a store.
template<class TLockIn, class TLockOut>
inline void VerticalSkew(
	const TLockIn* pLockIn,
	TLockOut* pLockOut,
	COLORREF* pLineIn,
	COLORREF* pLineOut,
	const int cy, // Source height
	const int cyOut, // Destination height
	unsigned x, // Column index
	int iOffset, // Skew offset
	int weight, // Relative weight of right COLORREF
	COLORREF clrBack // Background color
)
{
	for (int y = 0; y < cy; y++)
	{
		pLineIn[y] = pLockIn->GetPixel(x, y);
	}

	SkewLine(pLineOut, pLineIn, cy, cyOut, iOffset, weight, clrBack);

	for (int y = 0; y < cyOut; y++)
	{
		pLockOut->SetPixel(x, y, pLineOut[y]);
	}
}


////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

template<bool TAlpha>
inline bool RotatePage(IW::Image& imageOut, const IW::Page& pageIn, const double dRadAngle, const double dTan,
                const double dSinE, const COLORREF clrBack, IW::IStatus* pStatus)
{
	const int cx = pageIn.GetWidth();
	const int cy = pageIn.GetHeight();

	const int cx1 = cx + static_cast<int>(double(cy) * fabs(dTan) + 0.5);
	const int cy1 = cy;
	// The private alpha intermediates hold premultiplied samples until the last shear.
	constexpr auto intermediateFormat = TAlpha ? IW::PixelFormat::PF32Alpha : IW::PixelFormat::PF32;
	constexpr auto outputFormat = TAlpha ? IW::PixelFormat::PF32Alpha : IW::PixelFormat::PF24;

	// Calc first shear (horizontal) destination image dimensions
	IW::Image image1;
	IW::Page& page1 = image1.CreatePage(cx1, cy1, intermediateFormat);

	if (pStatus->QueryCancel()) return false;

	// Perform 1st shear (horizontal)
	{
		IW::ConstIImageSurfaceLockPtr pLockIn = pageIn.GetSurfaceLock();
		IW::SurfaceLock<intermediateFormat> lock1(page1);

		IW::CBuffer<COLORREF> pLineIn(cx);
		IW::CBuffer<COLORREF> pLineOut(cx1);

		for (int u = 0; u < cy1; u++)
		{
			if (pStatus->QueryCancel()) return false;
			const double dShear = (dTan >= 0.0) ? ((u + 0.5) * dTan) : (((u - cy1) + 0.5) * dTan);
			const int iShear = static_cast<int>(floor(dShear));
			const int nWeight = static_cast<int>(255 * (dShear - iShear) + 1) & 0xFF;

			HorizontalSkew<TAlpha>(pLockIn.p, &lock1, pLineIn, pLineOut, cx, cx1, u, iShear, nWeight, clrBack, &pageIn);
		}
	}

	// Perform 2nd shear  (vertical)
	const int cx2 = cx1;
	const int cy2 = static_cast<int>((cx * fabs(dSinE)) + (cy * cos(dRadAngle)) + 0.5) + 1;

	IW::Image image2;
	IW::Page& page2 = image2.CreatePage(cx2, cy2, intermediateFormat);

	// Variable skew offset
	double dOffset2 = (dSinE > 0.0) ? ((cx - 1) * dSinE) : (-dSinE * (cx - cx2));

	if (pStatus->QueryCancel()) return false;

	{
		IW::SurfaceLock<intermediateFormat> lock1(page1);
		IW::SurfaceLock<intermediateFormat> lock2(page2);

		IW::CBuffer<COLORREF> pLineIn(cy1);
		IW::CBuffer<COLORREF> pLineOut(cy2);

		for (int u = 0; u < cx2; u++, dOffset2 -= dSinE)
		{
			if (pStatus->QueryCancel()) return false;
			const int iShear = static_cast<int>(floor(dOffset2));
			const int nWeight = static_cast<int>(255 * (dOffset2 - iShear) + 1) & 0xFF;

			VerticalSkew(&lock1, &lock2, pLineIn, pLineOut, cy1, cy2, u, iShear, nWeight, clrBack);
		}
	}

	// Perform 3rd shear (horizontal)
	const int cx3 = static_cast<int>((cy * fabs(dSinE)) + (cx * cos(dRadAngle)) + 0.5) + 1;
	const int cy3 = cy2;

	// Not the CreatePage(cx, cy, pf) overload: that frees the destination, which
	// on a multi-page image throws away the pages already rotated.
	IW::Page& pageOut = imageOut.CreatePage(CRect(0, 0, cx3, cy3), outputFormat);

	double dOffset3 = (dSinE >= 0.0)
		                  ? ((cx - 1) * dSinE * -dTan)
		                  : (dTan * ((cx - 1) * -dSinE + (1 - cy3)));

	if (pStatus->QueryCancel()) return false;

	{
		IW::SurfaceLock<intermediateFormat> lock2(page2);
		IW::SurfaceLock<outputFormat> lockOut(pageOut);

		IW::CBuffer<COLORREF> pLineIn(cx2);
		IW::CBuffer<COLORREF> pLineOut(cx3);

		for (int u = 0; u < cy3; u++, dOffset3 += dTan)
		{
			if (pStatus->QueryCancel()) return false;
			const int iShear = static_cast<int>(floor(dOffset3));
			const int nWeight = static_cast<int>(255 * (dOffset3 - iShear) + 1) & 0xFF;

			HorizontalSkew<TAlpha>(&lock2, &lockOut, pLineIn, pLineOut, cx2, cx3, u, iShear, nWeight, clrBack, nullptr, true);
		}
	}

	pageOut.CopyExtraInfo(pageIn);
	return true;
}


inline bool IW::Rotate(const IW::Image& imageInRaw, IW::Image& imageOut, float fAngle, IW::IStatus* pStatus)
{
	if (!std::isfinite(fAngle))
	{
		pStatus->SetError(L"Invalid rotation angle.");
		return false;
	}
	fAngle = std::fmod(fAngle, 360.0f);
	if (fAngle < 0.0f) fAngle += 360.0f;

	if (fAngle == 0.0)
	{
		imageOut = imageInRaw;
		return true;
	}

	IW::Image imageIn = imageInRaw;

	// If we are multiple pages then do the render
	if (imageIn.NeedRenderForDisplay())
	{
		IW::Image imageTemp;
		if (!imageIn.Render(imageTemp)) return false;
		imageIn = imageTemp;
	}

	if (fAngle > 45.0 && fAngle <= 135.0)
	{
		// Angle in (45.0 .. 135.0]
		// Rotate image by 90 degrees into temporary image,
		// so it requires only an extra rotation angle
		// of -45.0 .. +45.0 to complete rotation.
		fAngle -= 90.0;

		IW::Image imageTemp;
		if (!Rotate90(imageIn, imageTemp, pStatus)) return false;
		imageIn = imageTemp;
	}
	else if (fAngle > 135.0 && fAngle <= 225.0)
	{
		// Angle in (135.0 .. 225.0]
		// Rotate image by 180 degrees into temporary image,
		// so it requires only an extra rotation angle
		// of -45.0 .. +45.0 to complete rotation.
		fAngle -= 180.0;

		IW::Image imageTemp;
		if (!Rotate180(imageIn, imageTemp, pStatus)) return false;
		imageIn = imageTemp;
	}
	else if (fAngle > 225.0 && fAngle <= 315.0)
	{
		// Angle in (225.0 .. 315.0]
		// Rotate image by 270 degrees into temporary image,
		// so it requires only an extra rotation angle
		// of -45.0 .. +45.0 to complete rotation.
		fAngle -= 270.0;

		IW::Image imageTemp;
		if (!Rotate270(imageIn, imageTemp, pStatus)) return false;
		imageIn = imageTemp;
	}

	if (fAngle == 0.0f)
	{
		imageOut = imageIn;
		return true;
	}

	// If we got here, angle is in (-45.0 .. +45.0]
	constexpr double ROTATE_PI = 3.1415926535897932384626433832795;
	const double dRadAngle = fAngle * ROTATE_PI / 180.0; // Angle in radians
	const double dSinE = sin(dRadAngle);
	const double dTan = tan(dRadAngle / 2.0);
	const COLORREF clrBack = RGBA(0, 0, 0, 0);

	// Rotate each page
	//IW::Image imageRotated;
	for (IW::Image::PageList::const_iterator pageIn = imageIn.Pages.begin(); pageIn != imageIn.Pages.end(); ++pageIn)
	{
		if (pStatus->QueryCancel()) return false;
		const bool success = HasSourceAlpha(*pageIn)
			? RotatePage<true>(imageOut, *pageIn, dRadAngle, dTan, dSinE, clrBack, pStatus)
			: RotatePage<false>(imageOut, *pageIn, dRadAngle, dTan, dSinE, clrBack, pStatus);
		if (!success)
			return false;
	}

	/*CRect rectRotated = imageRotated.GetBoundingRect();
	CRect rectIn = imageIn.GetBoundingRect();

	const CRect rectCrop = CalcCropRect(rectIn.Width(), rectIn.Height(), rectRotated.Width(), rectRotated.Height(), dRadAngle);
	if (!IW::Crop(imageRotated, imageOut, rectCrop, pStatus))
		return false;*/

	IterateImageMetaData(imageIn, imageOut, pStatus);

	return true;
}

// ---- ImagingScale ----
// ImageWalker by Zac Walker
//
// Purpose: Both scalers: the fast blitter path used for thumbnails, and the
//          windowed resampling kernels used by the resize filter.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

//
// IW::CImage: implementation
//
//////////////////////////////////////////////////////////////////////

#include "LegacyPch.h"
#include "ImagingLock.h"
#include "ImagingStreams.h"
#include "ImagingBlitter.h"
#include "RenderImage.h"

#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES  1
#endif
#include <Math.h>

namespace iw::legacy_detail::scale
{

// A resampling kernel is a weighted average, and averaging straight RGBA lets
// the colour of a fully transparent pixel into its neighbours. Same reason
// SampleWarp in ImagingEdits.cpp works premultiplied.
inline COLORREF PremultiplyPixel(COLORREF c)
{
	const unsigned a = IW::GetA(c);

	if (a == 255)
		return c;

	return IW::RGBA(static_cast<int>((IW::GetR(c) * a + 127) / 255),
	                static_cast<int>((IW::GetG(c) * a + 127) / 255),
	                static_cast<int>((IW::GetB(c) * a + 127) / 255),
	                static_cast<int>(a));
}

inline COLORREF UnpremultiplyPixel(COLORREF c)
{
	const unsigned a = IW::GetA(c);

	if (a == 0)
		return 0;

	if (a == 255)
		return c;

	// 255 / a in 16.16. A premultiplied channel never exceeds its own alpha.
	const unsigned k = (255u << 16) / a;

	return IW::RGBA(static_cast<int>(IW::Min(static_cast<int>((IW::GetR(c) * k + 0x8000) >> 16), 255)),
	                static_cast<int>(IW::Min(static_cast<int>((IW::GetG(c) * k + 0x8000) >> 16), 255)),
	                static_cast<int>(IW::Min(static_cast<int>((IW::GetB(c) * k + 0x8000) >> 16), 255)),
	                static_cast<int>(a));
}

//////////////////////////////////////////////////////////////////////
// Filter Functions!!


inline double J1(double x)
{
	double p, q;
	long i;
	static constexpr double Pone[] =
	                        {
		                        0.581199354001606143928050809e+21,
		                        -0.6672106568924916298020941484e+20,
		                        0.2316433580634002297931815435e+19,
		                        -0.3588817569910106050743641413e+17,
		                        0.2908795263834775409737601689e+15,
		                        -0.1322983480332126453125473247e+13,
		                        0.3413234182301700539091292655e+10,
		                        -0.4695753530642995859767162166e+7,
		                        0.270112271089232341485679099e+4
	                        },
	                        Qone[] = {
		                        0.11623987080032122878585294e+22,
		                        0.1185770712190320999837113348e+20,
		                        0.6092061398917521746105196863e+17,
		                        0.2081661221307607351240184229e+15,
		                        0.5243710262167649715406728642e+12,
		                        0.1013863514358673989967045588e+10,
		                        0.1501793594998585505921097578e+7,
		                        0.1606931573481487801970916749e+4,
		                        0.1e+1
	                        };

	p = Pone[8];
	q = Qone[8];
	for (i = 7; i >= 0; i--)
	{
		p = p * x * x + Pone[i];
		q = q * x * x + Qone[i];
	}
	return (p / q);
}

inline double P1(double x)
{
	double p, q;
	long i;

	static constexpr double Pone[] =
	                        {
		                        0.352246649133679798341724373e+5,
		                        0.62758845247161281269005675e+5,
		                        0.313539631109159574238669888e+5,
		                        0.49854832060594338434500455e+4,
		                        0.2111529182853962382105718e+3,
		                        0.12571716929145341558495e+1
	                        },
	                        Qone[] = {
		                        0.352246649133679798068390431e+5,
		                        0.626943469593560511888833731e+5,
		                        0.312404063819041039923015703e+5,
		                        0.4930396490181088979386097e+4,
		                        0.2030775189134759322293574e+3,
		                        0.1e+1
	                        };

	p = Pone[5];
	q = Qone[5];
	for (i = 4; i >= 0; i--)
	{
		p = p * (8.0 / x) * (8.0 / x) + Pone[i];
		q = q * (8.0 / x) * (8.0 / x) + Qone[i];
	}
	return (p / q);
}

inline double Q1(double x)
{
	double p, q;
	long i;

	static constexpr double Pone[] =
	                        {
		                        0.3511751914303552822533318e+3,
		                        0.7210391804904475039280863e+3,
		                        0.4259873011654442389886993e+3,
		                        0.831898957673850827325226e+2,
		                        0.45681716295512267064405e+1,
		                        0.3532840052740123642735e-1
	                        },
	                        Qone[] = {
		                        0.74917374171809127714519505e+4,
		                        0.154141773392650970499848051e+5,
		                        0.91522317015169922705904727e+4,
		                        0.18111867005523513506724158e+4,
		                        0.1038187585462133728776636e+3,
		                        0.1e+1
	                        };

	p = Pone[5];
	q = Qone[5];
	for (i = 4; i >= 0; i--)
	{
		p = p * (8.0 / x) * (8.0 / x) + Pone[i];
		q = q * (8.0 / x) * (8.0 / x) + Qone[i];
	}
	return (p / q);
}

inline double BesselOrderOne(double x)
{
	double p, q;

	if (x == 0.0)
		return (0.0);
	p = x;
	if (x < 0.0)
		x = (-x);
	if (x < 8.0)
		return (p * J1(x));
	q = sqrt(2.0 / (M_PI * x)) * (P1(x) * (1.0 / sqrt(2.0) * (sin(x) - cos(x))) - 8.0 / x * Q1(x) *
		(-1.0 / sqrt(2.0) * (sin(x) + cos(x))));
	if (p < 0.0)
		q = (-q);
	return (q);
}

inline double Bessel(const double x, const double support)
{
	if (x == 0.0)
		return (M_PI / 4.0);
	return (BesselOrderOne(M_PI * x) / (2.0 * x));
}

inline double Sinc(const double x, const double support)
{
	if (x == 0.0)
		return (1.0);
	return (sin(M_PI * x) / (M_PI * x));
}

inline double Blackman(const double x, const double support)
{
	return (0.42 + 0.5 * cos(M_PI * x) + 0.08 * cos(2 * M_PI * x));
}

inline double BlackmanBessel(const double x, const double support)
{
	return (Blackman(x / support, support) * Bessel(x, support));
}

inline double BlackmanSinc(const double x, const double support)
{
	return (Blackman(x / support, support) * Sinc(x, support));
}

inline double Box(const double x, const double support)
{
	if (x < -0.5)
		return (0.0);
	if (x < 0.5)
		return (1.0);
	return (0.0);
}

inline double Catrom(const double x, const double support)
{
	if (x < -2.0)
		return (0.0);
	if (x < -1.0)
		return (0.5 * (4.0 + x * (8.0 + x * (5.0 + x))));
	if (x < 0.0)
		return (0.5 * (2.0 + x * x * (-5.0 - 3.0 * x)));
	if (x < 1.0)
		return (0.5 * (2.0 + x * x * (-5.0 + 3.0 * x)));
	if (x < 2.0)
		return (0.5 * (4.0 + x * (-8.0 + x * (5.0 - x))));
	return (0.0);
}

inline double Cubic(const double x, const double support)
{
	if (x < -2.0)
		return (0.0);
	if (x < -1.0)
		return ((2.0 + x) * (2.0 + x) * (2.0 + x) / 6.0);
	if (x < 0.0)
		return ((4.0 + x * x * (-6.0 - 3.0 * x)) / 6.0);
	if (x < 1.0)
		return ((4.0 + x * x * (-6.0 + 3.0 * x)) / 6.0);
	if (x < 2.0)
		return ((2.0 - x) * (2.0 - x) * (2.0 - x) / 6.0);
	return (0.0);
}

inline double Gaussian(const double x, const double support)
{
	return (exp(-2.0 * x * x) * sqrt(2.0 / M_PI));
}

inline double Hanning(const double x, const double support)
{
	return (0.5 + 0.5 * cos(M_PI * x));
}

inline double Hamming(const double x, const double support)
{
	return (0.54 + 0.46 * cos(M_PI * x));
}

inline double Hermite(const double x, const double support)
{
	if (x < -1.0)
		return (0.0);
	if (x < 0.0)
		return ((2.0 * (-x) - 3.0) * (-x) * (-x) + 1.0);
	if (x < 1.0)
		return ((2.0 * x - 3.0) * x * x + 1.0);
	return (0.0);
}

inline double Lanczos(const double x, const double support)
{
	if (x < -3.0)
		return (0.0);
	if (x < 0.0)
		return (Sinc(-x, support) * Sinc(-x / 3.0, support));
	if (x < 3.0)
		return (Sinc(x, support) * Sinc(x / 3.0, support));
	return (0.0);
}

inline double Mitchell(const double x, const double support)
{
	constexpr double B = (1.0 / 3.0);
	constexpr double C = (1.0 / 3.0);
	constexpr double P0 = ((6.0 - 2.0 * B) / 6.0);
	constexpr double P2 = ((-18.0 + 12.0 * B + 6.0 * C) / 6.0);
	constexpr double P3 = ((12.0 - 9.0 * B - 6.0 * C) / 6.0);
	constexpr double Q0 = ((8.0 * B + 24.0 * C) / 6.0);
	constexpr double Q1 = ((-12.0 * B - 48.0 * C) / 6.0);
	constexpr double Q2 = ((6.0 * B + 30.0 * C) / 6.0);
	constexpr double Q3 = ((-1.0 * B - 6.0 * C) / 6.0);

	if (x < -2.0)
		return (0.0);
	if (x < -1.0)
		return (Q0 - x * (Q1 - x * (Q2 - x * Q3)));
	if (x < 0.0)
		return (P0 + x * x * (P2 - x * P3));
	if (x < 1.0)
		return (P0 + x * x * (P2 + x * P3));
	if (x < 2.0)
		return (Q0 + x * (Q1 + x * (Q2 + x * Q3)));
	return (0.0);
}

inline double Quadratic(const double x, const double support)
{
	if (x < -1.5)
		return (0.0);
	if (x < -0.5)
		return (0.5 * (x + 1.5) * (x + 1.5));
	if (x < 0.5)
		return (0.75 - x * x);
	if (x < 1.5)
		return (0.5 * (x - 1.5) * (x - 1.5));
	return (0.0);
}

inline double Triangle(const double x, const double support)
{
	if (x < -1.0)
		return (0.0);
	if (x < 0.0)
		return (1.0 + x);
	if (x < 1.0)
		return (1.0 - x);
	return (0.0);
}

///////////////////////////////////////////////////////////////////////////////////

using FilterInfo = struct _FilterInfo
{
	double (*function)(double, double),
	       support;
};

inline const FilterInfo filters[] =
{
	{Box, 0.0},
	{Box, 1.0},
	{Box, 0.5},
	{Triangle, 1.0},
	{Hermite, 1.0},
	{Hanning, 1.0},
	{Hamming, 1.0},
	{Blackman, 1.0},
	{Gaussian, 1.25},
	{Quadratic, 1.5},
	{Cubic, 2.0},
	{Catrom, 2.0},
	{Mitchell, 2.0},
	{Lanczos, 3.0},
	{BlackmanBessel, 3.2383},
	{BlackmanSinc, 4.0}
};

//	image rescaling routine

using ImageContrib = struct tagImageContrib
{
	int nPixel;
	int dWeight;
};

////////////////////////////////////////////////////////////////////////////////////////

inline bool ScaleBufferCount(int width, int height, size_t itemSize, size_t& count)
{
	if (width <= 0 || height <= 0) return false;
	const auto size = static_cast<std::uint64_t>(width) * height;
	if (size > static_cast<std::uint64_t>(IW::kMaxPageBytes) / itemSize) return false;
	count = static_cast<size_t>(size);
	return true;
}
}

using namespace iw::legacy_detail::scale;

inline bool IW::Scale(const CSize& sizeIn, int nFilterType, const IW::Image& imageIn, IW::Image& imageOut, IW::IStatus* pStatus)
{
	const CRect rcBounding = imageIn.GetBoundingRect();
	const auto boundingWidth = static_cast<std::int64_t>(rcBounding.right) - rcBounding.left;
	const auto boundingHeight = static_cast<std::int64_t>(rcBounding.bottom) - rcBounding.top;
	size_t outputCount{};
	if (boundingWidth <= 0 || boundingHeight <= 0 || boundingWidth > INT_MAX || boundingHeight > INT_MAX ||
		!ScaleBufferCount(sizeIn.cx, sizeIn.cy, sizeof(COLORREF), outputCount))
	{
		pStatus->SetError(L"Invalid or excessive image scaling dimensions.");
		return false;
	}
	const int cxBounding = static_cast<int>(boundingWidth);
	const int cyBounding = static_cast<int>(boundingHeight);

	// zoom scale factors
	const double dScaleX = static_cast<double>(sizeIn.cx) / static_cast<double>(cxBounding);
	const double dScaleY = static_cast<double>(sizeIn.cy) / static_cast<double>(cyBounding);

	if (nFilterType < 0 || nFilterType >= countof(filters)) nFilterType = 0;

	std::int64_t nProgress = 0;
	std::int64_t nProgressMax = 0;
	for (unsigned nPage = 0; nPage < imageIn.Pages.size(); nPage++)
	{
		int cy = imageIn.Pages[nPage].GetHeight();
		nProgressMax += cy;
		nProgressMax += MulDiv(cy, sizeIn.cy, cyBounding);
	}
	const auto progress = [&]
	{
		pStatus->Progress(static_cast<int>((std::min)(nProgress++, static_cast<std::int64_t>(INT_MAX))),
			static_cast<int>((std::min)(nProgressMax, static_cast<std::int64_t>(INT_MAX))));
	};

	for (unsigned nPage = 0; nPage < imageIn.Pages.size(); nPage++)
	{
		const IW::Page& pageIn = imageIn.GetPage(nPage);
		ConstRefPtr<IW::IImageSurfaceLock> pLockIn = pageIn.GetSurfaceLock();

		double dCenter, dWeight; // Filter calculation variables
		int nLeft, nRight, nTotal;
		double dWidth, dScale; // Filter calculation variables

		const CRect rcIn = pageIn.GetPageRect();

		const CRect rcOut(
			MulDiv(rcIn.left, sizeIn.cx, cxBounding),
			MulDiv(rcIn.top, sizeIn.cy, cyBounding),
			MulDiv(rcIn.right, sizeIn.cx, cxBounding),
			MulDiv(rcIn.bottom, sizeIn.cy, cyBounding));

		const int cxIn = rcIn.right - rcIn.left;
		const int cyIn = rcIn.bottom - rcIn.top;
		const auto outWidth = static_cast<std::int64_t>(rcOut.right) - rcOut.left;
		const auto outHeight = static_cast<std::int64_t>(rcOut.bottom) - rcOut.top;
		if (outWidth <= 0 || outHeight <= 0 || outWidth > INT_MAX || outHeight > INT_MAX)
		{
			pStatus->SetError(L"Invalid image scaling extent.");
			return false;
		}
		const int cxOut = static_cast<int>(outWidth);
		const int cyOut = static_cast<int>(outHeight);
		double (*FilterFunction)(double, double) = filters[nFilterType].function;
		// Box Point declares zero support: without a floor of half a pixel every
		// non-integral centre selects no source samples at all and comes out black.
		const double dFilterSupport = filters[nFilterType].support < 0.5 ? 0.5 : filters[nFilterType].support;

		const double contributionWidth = (std::max)(
			(dScaleX < 1.0 ? dFilterSupport / dScaleX : dFilterSupport) * 2 + 1,
			(dScaleY < 1.0 ? dFilterSupport / dScaleY : dFilterSupport) * 2 + 1);
		if (!std::isfinite(contributionWidth) || contributionWidth >= INT_MAX)
		{
			pStatus->SetError(L"The image scaling filter exceeds the memory limit.");
			return false;
		}
		const int nContribListWidth = static_cast<int>(contributionWidth);
		size_t temporaryCount{}, contributionCountX{}, contributionCountY{};
		if (!ScaleBufferCount(cxOut, cyOut, sizeof(COLORREF), outputCount) ||
			!ScaleBufferCount(cxOut, cyIn, sizeof(DWORD), temporaryCount) ||
			!ScaleBufferCount(cxOut, nContribListWidth, sizeof(ImageContrib), contributionCountX) ||
			!ScaleBufferCount(cyOut, nContribListWidth, sizeof(ImageContrib), contributionCountY))
		{
			pStatus->SetError(L"The image scaling buffers exceed the memory limit.");
			return false;
		}

		IW::CBuffer<DWORD> pTemp(temporaryCount);
		IW::CBuffer<int> pContrCountX(cxOut);
		IW::CBuffer<int> pContrCountY(cyOut);
		IW::CBuffer<ImageContrib> pContrX(contributionCountX);
		IW::CBuffer<ImageContrib> pContrY(contributionCountY);

		// pre-calculate Filter pContributions for a row
		if (dScaleX < 1.0)
		{
			dWidth = dFilterSupport / dScaleX;
			dScale = 1.0 / dScaleX;

			int nRoundFix = (cxOut / cxIn) >> 1;

			for (int i = 0; i < cxOut; ++i)
			{
				pContrCountX[i] = 0;
				dCenter = static_cast<double>(i - nRoundFix) / dScaleX;
				nLeft = static_cast<int>(ceil(dCenter - dWidth));
				nRight = static_cast<int>(floor(dCenter + dWidth));
				nTotal = 0;
				for (int j = nLeft; j <= nRight; ++j)
				{
					dWeight = dCenter - static_cast<double>(j);
					dWeight = (*FilterFunction)(dWeight / dScale, dFilterSupport) / dScale;
					int k = pContrCountX[i]++ + i * nContribListWidth;
					pContrX[k].nPixel = Clamp(j, 0, cxIn - 1);
					nTotal += pContrX[k].dWeight = DoubleToFixed(dWeight);
				}

				if (nTotal == 0) return false;
				// Normalize
				for (int j = 0; j < pContrCountX[i]; ++j)
				{
					int k = j + i * nContribListWidth;
					pContrX[k].dWeight = FixedDiv(pContrX[k].dWeight, nTotal);
				}
			}
		}
		else
		{
			int nRoundFix = (cxOut / cxIn) >> 1;

			for (int i = 0; i < cxOut; ++i)
			{
				pContrCountX[i] = 0;
				dCenter = static_cast<double>(i - nRoundFix) / dScaleX;
				nLeft = static_cast<int>(ceil(dCenter - dFilterSupport));
				nRight = static_cast<int>(floor(dCenter + dFilterSupport));
				nTotal = 0;
				for (int j = nLeft; j <= nRight; ++j)
				{
					dWeight = dCenter - static_cast<double>(j);
					dWeight = (*FilterFunction)(dWeight, dFilterSupport);
					int k = pContrCountX[i]++ + i * nContribListWidth;
					pContrX[k].nPixel = Clamp(j, 0, cxIn - 1);
					nTotal += pContrX[k].dWeight = DoubleToFixed(dWeight);
				}

				if (nTotal == 0) return false;
				// Normalize
				for (int j = 0; j < pContrCountX[i]; ++j)
				{
					int k = j + i * nContribListWidth;
					pContrX[k].dWeight = FixedDiv(pContrX[k].dWeight, nTotal);
				}
			}
		}

		// pre-calculate Filter pContributions for a column
		if (dScaleY < 1.0)
		{
			int nRoundFix = (cyOut / cyIn) >> 1;

			dWidth = dFilterSupport / dScaleY;
			dScale = 1.0 / dScaleY;
			nTotal = 0;
			for (int i = 0; i < cyOut; ++i)
			{
				pContrCountY[i] = 0;
				dCenter = static_cast<double>(i - nRoundFix) / dScaleY;
				nLeft = static_cast<int>(ceil(dCenter - dWidth));
				nRight = static_cast<int>(floor(dCenter + dWidth));
				nTotal = 0;
				for (int j = nLeft; j <= nRight; ++j)
				{
					dWeight = dCenter - static_cast<double>(j);
					dWeight = (*FilterFunction)(dWeight / dScale, dFilterSupport) / dScale;
					int k = pContrCountY[i]++ + i * nContribListWidth;
					pContrY[k].nPixel = Clamp(j, 0, cyIn - 1) * cxOut;
					nTotal += pContrY[k].dWeight = DoubleToFixed(dWeight);
				}

				if (nTotal == 0) return false;
				// Normalize
				for (int j = 0; j < pContrCountY[i]; ++j)
				{
					int k = j + i * nContribListWidth;
					pContrY[k].dWeight = FixedDiv(pContrY[k].dWeight, nTotal);
				}

				if (pStatus->QueryCancel())
				{
					return false;
				}
			}
		}
		else
		{
			int nRoundFix = (cyOut / cyIn) >> 1;

			for (int i = 0; i < cyOut; ++i)
			{
				pContrCountY[i] = 0;
				dCenter = static_cast<double>(i - nRoundFix) / dScaleY;
				nLeft = static_cast<int>(ceil(dCenter - dFilterSupport));
				nRight = static_cast<int>(floor(dCenter + dFilterSupport));
				nTotal = 0;
				for (int j = nLeft; j <= nRight; ++j)
				{
					dWeight = dCenter - static_cast<double>(j);
					dWeight = (*FilterFunction)(dWeight, dFilterSupport);
					int k = pContrCountY[i]++ + i * nContribListWidth;
					pContrY[k].nPixel = Clamp(j, 0, cyIn - 1) * cxOut;
					nTotal += pContrY[k].dWeight = DoubleToFixed(dWeight);
				}

				if (nTotal == 0) return false;
				// Normalize
				for (int j = 0; j < pContrCountY[i]; ++j)
				{
					int k = j + i * nContribListWidth;
					pContrY[k].dWeight = FixedDiv(pContrY[k].dWeight, nTotal);
				}

				if (pStatus->QueryCancel())
				{
					return false;
				}
			}
		}

		// we have to loop for every pane
		if (pStatus->QueryCancel())
		{
			return false;
		}


		const bool bAlpha = HasSourceAlpha(pageIn);
		IW::PixelFormat pf(bAlpha ? IW::PixelFormat::PF32Alpha : IW::PixelFormat::PF24);
		IW::Page pageOut = imageOut.CreatePage(rcOut, pf);

		// Scale the line
		RefPtr<IW::IImageSurfaceLock> pLockOut = pageOut.GetSurfaceLock();
		IW::CBuffer<DWORD> pLineBuffer(IW::Max(cxIn, cxOut));

		for (int y = 0; (y < cyIn); y++)
		{
			LPDWORD pLineOut = pTemp + (cxOut * y);

			if (bAlpha)
			{
				ReadStraightLine(pageIn, pLockIn, pLineBuffer, y, 0, cxIn);

				for (int x = 0; x < cxIn; x++)
					pLineBuffer[x] = PremultiplyPixel(pLineBuffer[x]);
			}
			else
			{
				// Neither call promises an alpha byte for a format that has
				// none, and the accumulator below weights by it.
				pLockIn->RenderLine(pLineBuffer, y, 0, cxIn);

				for (int x = 0; x < cxIn; x++)
					pLineBuffer[x] |= 0xFF000000;
			}

			for (int x = 0; x < cxOut; x++)
			{
				int r = 0, g = 0, b = 0, a = 0;

				for (int i = 0; i < pContrCountX[x]; ++i)
				{
					int k = i + x * nContribListWidth;
					DWORD color = pLineBuffer[pContrX[k].nPixel];
					int weight = pContrX[k].dWeight;

					r += weight * IW::GetB(color);
					g += weight * IW::GetG(color);
					b += weight * IW::GetR(color);
					a += weight * IW::GetA(color);
				}

				// Check for overflow
				assert(pLineOut < pTemp + (cxOut * (y+1)));

				// Place result in destination pixel
				*pLineOut = IW::SaturateRGBA(FixedToInt(b),
				                         FixedToInt(g),
				                         FixedToInt(r),
				                         FixedToInt(a));

				pLineOut++;
			}

			progress();
			if (pStatus->QueryCancel()) return false;
		}

		// apply Filter to zoom vertically from pImage to pDst
		for (int y = 0; (y < cyOut); y++)
		{
			auto pLineOut = (LPCBYTE)pLineBuffer.data();

			for (int x = 0; x < cxOut; x++)
			{
				int r = 0, g = 0, b = 0, a = 0;
				int k = y * nContribListWidth;

				for (int i = 0; i < pContrCountY[y]; ++i)
				{
					DWORD color = pTemp[pContrY[k].nPixel + x];
					int nWeight = pContrY[k].dWeight;

					r += nWeight * IW::GetR(color);
					g += nWeight * IW::GetG(color);
					b += nWeight * IW::GetB(color);
					a += nWeight * (color >> 24);

					k += 1;
				}

				*((LPDWORD)pLineOut) = IW::SaturateRGBA(FixedToInt(r),
				                                    FixedToInt(g),
				                                    FixedToInt(b),
				                                    FixedToInt(a)); // Place result in destination pixel

				if (bAlpha)
					*((LPDWORD)pLineOut) = UnpremultiplyPixel(*((LPDWORD)pLineOut));

				pLineOut += 4;
			}

			pLockOut->SetLine(pLineBuffer, y, 0, cxOut);

			progress();
			if (pStatus->QueryCancel())
				return false;
		}

		// copy over page attributes
		pageOut.CopyExtraInfo(pageIn);
	}

	imageOut.Normalize();
	IterateImageMetaData(imageIn, imageOut, pStatus);

	return true;
};



inline bool IW::Scale(const IW::Image& imageIn, IW::Image& imageOut, CSize size, IW::IStatus* pStatus)
{
	const CRect rcBounding = imageIn.GetBoundingRect();
	const auto boundingWidth = static_cast<std::int64_t>(rcBounding.right) - rcBounding.left;
	const auto boundingHeight = static_cast<std::int64_t>(rcBounding.bottom) - rcBounding.top;
	size_t outputCount{};
	if (boundingWidth <= 0 || boundingHeight <= 0 || boundingWidth > INT_MAX || boundingHeight > INT_MAX ||
		!ScaleBufferCount(size.cx, size.cy, sizeof(COLORREF), outputCount))
	{
		pStatus->SetError(L"Invalid or excessive image scaling dimensions.");
		return false;
	}
	const int cxBounding = static_cast<int>(boundingWidth);
	const int cyBounding = static_cast<int>(boundingHeight);

	for (auto pageIn = imageIn.Pages.begin(); pageIn != imageIn.Pages.end(); ++pageIn)
	{
		if (pStatus->QueryCancel()) return false;
		CRect rcIn = pageIn->GetPageRect();

		CRect rcOut(
			MulDiv(rcIn.left, size.cx, cxBounding),
			MulDiv(rcIn.top, size.cy, cyBounding),
			MulDiv(rcIn.right, size.cx, cxBounding),
			MulDiv(rcIn.bottom, size.cy, cyBounding));

		IW::PixelFormat pf(HasSourceAlpha(*pageIn) ? IW::PixelFormat::PF32Alpha : IW::PixelFormat::PF24);
		IW::Page& pageOut = imageOut.CreatePage(rcOut, pf);
		const CRect sourceRect(0, 0, pageIn->GetWidth(), pageIn->GetHeight());

		// RenderImage is already a template on its canvas, and only two formats
		// ever reach here, so the canvas is named rather than dispatched.
		WithBlitter([&](auto& blitter)
		{
			typedef std::remove_reference_t<decltype(blitter)> TBlitter;

			if (pf.HasAlpha())
			{
				typedef SurfaceLock<IW::PixelFormat::PF32Alpha> TLock;
				TLock lockOut(pageOut);
				RenderImage<TLock, TBlitter> render(lockOut, blitter);
				render.DrawImage(*pageIn, rcOut, sourceRect);
			}
			else
			{
				typedef SurfaceLock<IW::PixelFormat::PF24> TLock;
				TLock lockOut(pageOut);
				RenderImage<TLock, TBlitter> render(lockOut, blitter);
				render.DrawImage(*pageIn, rcOut, sourceRect);
			}
		});

		pageOut.CopyExtraInfo(*pageIn);
	}

	imageOut.Normalize();
	IterateImageMetaData(imageIn, imageOut, pStatus);

	return true;
};

// ---- ImagingSimd ----
// ImageWalker by Zac Walker
//
// Purpose: CPU feature detection for the blitters - checks that the OS has
//          agreed to save YMM state before reporting AVX2.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#include <iw/imaging/PixelOps.h>
#include "LegacyPch.h"

inline IW::SimdLevel IW::GetSimdLevel()
{
	return iw::imaging::has_avx2() ? IW::SimdLevel::Avx2 : IW::SimdLevel::Sse2;
}

// ---- LibfilesDocument ----
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// Purpose: Native image storage and presentation adapters, without a format parser.

#include "LegacyPch.h"
#include "LibfilesDocument.h"
#include "MetadataExif.h"
#include <tiffio.h>

#include <array>

namespace iw::imaging::detail
{
namespace iw::legacy_detail::source_libfilesdocument
{
		inline LPCTSTR GetCompressionString(int nCompression)
		{
			LPCTSTR szCompression = g_szEmptyString;

			switch (nCompression)
			{
			case COMPRESSION_CCITTRLE:
				szCompression = _T("CCITTRLE");
				break;
			case COMPRESSION_CCITTFAX3:
				szCompression = _T("CCITTFAX3");
				break;
			case COMPRESSION_CCITTFAX4:
				szCompression = _T("CCITTFAX4");
				break;
			case COMPRESSION_LZW:
				szCompression = _T("LZW");
				break;
			case COMPRESSION_OJPEG:
				szCompression = _T("OJPEG");
				break;
			case COMPRESSION_JPEG:
				szCompression = _T("JPEG");
				break;
			case COMPRESSION_NEXT:
				szCompression = _T("NEXT");
				break;
			case COMPRESSION_CCITTRLEW:
				szCompression = _T("CCITTRLEW");
				break;
			case COMPRESSION_PACKBITS:
				szCompression = _T("PACKBITS");
				break;
			case COMPRESSION_THUNDERSCAN:
				szCompression = _T("THUNDERSCAN");
				break;
			case COMPRESSION_IT8CTPAD:
				szCompression = _T("IT8CTPAD");
				break;
			case COMPRESSION_IT8LW:
				szCompression = _T("IT8LW");
				break;
			case COMPRESSION_IT8MP:
				szCompression = _T("IT8MP");
				break;
			case COMPRESSION_IT8BL:
				szCompression = _T("IT8BL");
				break;
			case COMPRESSION_PIXARFILM:
				szCompression = _T("PIXARFILM");
				break;
			case COMPRESSION_PIXARLOG:
				szCompression = _T("PIXARLOG");
				break;
			case COMPRESSION_DEFLATE:
				szCompression = _T("DEFLATE");
				break;
			case COMPRESSION_ADOBE_DEFLATE:
				szCompression = _T("ADOBE_DEFLATE");
				break;
			case COMPRESSION_DCS:
				szCompression = _T("DCS");
				break;
			case COMPRESSION_JBIG:
				szCompression = _T("JBIG");
				break;
			case COMPRESSION_SGILOG:
				szCompression = _T("SGILOG");
				break;
			case COMPRESSION_SGILOG24:
				szCompression = _T("SGILOG24");
				break;
			}

			return szCompression;
		}

		inline LPCTSTR GetPhotometricString(int nPhotometric)
		{
			LPCTSTR szPhotometric = g_szEmptyString;

			switch (nPhotometric)
			{
			case PHOTOMETRIC_MINISWHITE:
				szPhotometric = _T("MINISWHITE");
				break;
			case PHOTOMETRIC_MINISBLACK:
				szPhotometric = _T("MINISBLACK");
				break;
			case PHOTOMETRIC_RGB:
				szPhotometric = _T("RGB");
				break;
			case PHOTOMETRIC_PALETTE:
				szPhotometric = _T("PALETTE");
				break;
			case PHOTOMETRIC_MASK:
				szPhotometric = _T("MASK");
				break;
			case PHOTOMETRIC_SEPARATED:
				szPhotometric = _T("SEPARATED");
				break;
			case PHOTOMETRIC_YCBCR:
				szPhotometric = _T("YCBCR");
				break;
			case PHOTOMETRIC_CIELAB:
				szPhotometric = _T("CIELAB");
				break;
			case PHOTOMETRIC_LOGL:
				szPhotometric = _T("LOGL");
				break;
			case PHOTOMETRIC_LOGLUV:
				szPhotometric = _T("LOGLUV");
				break;
			}

			return szPhotometric;
		}

		// A damaged later directory ends the document after its complete pages.

		inline IW::PixelFormat pixel_format(const libfiles::document_storage format)
		{
			constexpr IW::PixelFormat::Format formats[] = {
				IW::PixelFormat::PF1, IW::PixelFormat::PF2, IW::PixelFormat::PF4,
				IW::PixelFormat::PF8, IW::PixelFormat::PF8Alpha, IW::PixelFormat::PF8GrayScale,
				IW::PixelFormat::PF555, IW::PixelFormat::PF565, IW::PixelFormat::PF24,
				IW::PixelFormat::PF32, IW::PixelFormat::PF32Alpha};
			const auto index = static_cast<std::size_t>(format);
			if (index >= std::size(formats)) throw std::invalid_argument("Invalid decoded pixel format.");
			return formats[index];
		}

		class Input final : public libfiles::stream
		{
		public:
			inline explicit Input(IW::IStreamIn& input) : input_(input) {}
			inline std::uint64_t size() override { return input_.GetFileSize(); }
			inline std::size_t read(std::uint64_t offset, void* data, std::size_t size) override
			{
				if (offset > LONG_MAX || input_.Seek(IW::IStreamCommon::eBegin,
					static_cast<LONG>(offset)) != offset) return 0;
				DWORD count{};
				input_.Read(data, static_cast<DWORD>((std::min)(size, size_t{MAXDWORD})), &count);
				return count;
			}
		private:
			IW::IStreamIn& input_;
		};

		class Output final : public libfiles::document_sink
		{
		public:
			Output(IW::IImageStream& output, IW::IStatus& status) : output_(output), status_(status) {}
			inline bool begin_frame(const libfiles::document_frame& frame) override
			{
				if (first_size_.cx == 0) first_size_ = CSize(frame.width, frame.height);
				const auto right = static_cast<std::int64_t>(frame.left) + frame.width;
				const auto bottom = static_cast<std::int64_t>(frame.top) + frame.height;
				if (right > INT_MAX || bottom > INT_MAX) throw std::bad_alloc();
				output_.CreatePage(CRect(frame.left, frame.top, static_cast<int>(right),
					static_cast<int>(bottom)), pixel_format(frame.storage), frame.sequential);
				if (!frame.palette.empty())
				{
					std::array<COLORREF, 256> palette{};
					if (frame.palette.size() > palette.size())
						throw std::invalid_argument("Invalid decoded palette.");
					std::copy(frame.palette.begin(), frame.palette.end(), palette.begin());
					output_.SetPalette(palette.data());
				}
				return true;
			}
			inline void row(std::uint32_t y, std::span<const std::uint8_t> bytes) override
			{
				output_.SetBitmap(static_cast<int>(y), bytes.data());
			}
			inline void end_frame(const libfiles::document_frame& frame) override
			{
				if (frame.transparent) output_.SetTransparent(*frame.transparent);
				if (frame.background) output_.SetBackGround(*frame.background);
				if (frame.delay_ms) output_.SetTimeDelay(*frame.delay_ms);
				if (frame.disposal) output_.SetDisposalMethod(*frame.disposal);
				output_.Flush();
			}
			inline bool cancelled() override { return status_.QueryCancel(); }
			inline void progress(std::uint32_t current, std::uint32_t total) override
			{
				status_.Progress(static_cast<int>(current), static_cast<int>(total));
			}
			inline void metadata(libfiles::document_metadata kind, std::span<const std::uint8_t> bytes) override
			{
				if (bytes.size() > INT_MAX) throw std::bad_alloc();
				if (kind == libfiles::document_metadata::iptc)
					iptc_.CopyData(bytes.data(), static_cast<int>(bytes.size()));
				else if (kind == libfiles::document_metadata::xmp)
					xmp_.CopyData(bytes.data(), static_cast<int>(bytes.size()));
				else
				{
					DWORD type = IW::MetaDataTypes::PROFILE_COMMENT;
					switch (kind)
					{
					case libfiles::document_metadata::exif:
						type = IW::MetaDataTypes::PROFILE_EXIF;
						Exif::Parse(&camera_, bytes.data(), static_cast<unsigned>(bytes.size()));
						break;
					case libfiles::document_metadata::icc: type = IW::MetaDataTypes::PROFILE_ICC; break;
					case libfiles::document_metadata::source_icc: type = IW::MetaDataTypes::PROFILE_SOURCE_ICC; break;
					case libfiles::document_metadata::author: type = IW::MetaDataTypes::PROFILE_AUTHOR; break;
					case libfiles::document_metadata::png_text: type = IW::MetaDataTypes::PROFILE_PNG_TEXT; break;
					case libfiles::document_metadata::photoshop: type = IW::MetaDataTypes::PROFILE_PHOTOSHOP; break;
					case libfiles::document_metadata::comment: break;
					default: throw std::invalid_argument("Unsupported native metadata kind.");
					}
					IW::MetaData blob(type, bytes.data(), static_cast<DWORD>(bytes.size()));
					output_.AddBlob(blob);
				}
			}
			inline void metadata_complete() override
			{
				output_.AddMetaDataBlob(iptc_, xmp_);
				iptc_ = IW::MetaData(IW::MetaDataTypes::PROFILE_IPTC);
				xmp_ = IW::MetaData(IW::MetaDataTypes::PROFILE_XMP);
			}
			inline IW::CameraSettings camera_settings() const { return camera_; }
			inline CSize first_size() const { return first_size_; }
		private:
			IW::IImageStream& output_;
			IW::IStatus& status_;
			IW::MetaData iptc_{IW::MetaDataTypes::PROFILE_IPTC};
			IW::MetaData xmp_{IW::MetaDataTypes::PROFILE_XMP};
			IW::CameraSettings camera_;
			CSize first_size_{0, 0};
		};
	}
using namespace iw::legacy_detail::source_libfilesdocument;

	inline bool read_libfiles_document(const libfiles::image_format format, const CString& key,
		IW::IStreamIn* input, IW::IImageStream* output, IW::IStatus* status)
	{
		Input source(*input);
		const auto memory = input->GetMemory();
		libfiles::reader reader = memory.empty() ? libfiles::reader(source) : libfiles::reader(memory);
		Output destination(*output, *status);
		libfiles::document_options options;
		const auto thumbnail = output->GetThumbnailSize();
		options.thumbnail_width = static_cast<std::uint32_t>((std::max)(0L, thumbnail.cx));
		options.thumbnail_height = static_cast<std::uint32_t>((std::max)(0L, thumbnail.cy));
		options.pixels = output->WantBitmap();
		// Historical loaders retain native pages under the old 1 GiB ceiling;
		// keep the sink and codec scratch budgets distinct.
		options.maximum_working_bytes = std::uint64_t{1} << 30;
		options.maximum_retained_bytes = std::uint64_t{1} << 30;
		options.count_palette_in_retained_bytes = false;
		options.legacy_tiff_storage = format == libfiles::image_format::tiff;
		const auto result = libfiles::decode_document(reader, format, destination, options);
		for (const auto& warning : result.warnings) status->SetWarning(CString(CA2W(warning.c_str(), CP_UTF8)));
		if (!result.success)
		{
			const CString message = result.cancelled ? CString(IW::Text::CANCELED) :
				result.refused ? CString(L"The decoded image exceeds the destination limit.") :
				CString(CA2W(result.error.c_str(), CP_UTF8));
			if (result.resource_limit) status->SetResourceError(message);
			else status->SetError(message);
			return false;
		}
		if (format == libfiles::image_format::wpg) return true;
		auto settings = destination.camera_settings();
		settings.OriginalImageSize = CSize(result.info.width, result.info.height);
		settings.OriginalBpp = pixel_format(result.info.storage);
		if (result.info.has_resolution)
		{
			settings.XPelsPerMeter = result.info.pixels_per_meter_x;
			settings.YPelsPerMeter = result.info.pixels_per_meter_y;
		}
		output->SetCameraSettings(settings);
		output->SetLoaderName(key);
		CString statistics;
		if (format == libfiles::image_format::pcx)
			statistics.Format(L"%dx%dx%d PCX", result.info.width, result.info.height, result.info.bits_per_pixel);
		else if (format == libfiles::image_format::bmp)
			statistics.Format(IW::Text::BMP_FMT, result.info.width, result.info.height, result.info.bits_per_pixel);
		else if (format == libfiles::image_format::png)
		{
			if (result.info.interlace_passes > 1)
				statistics.Format(IW::Text::PNG_INTERLACED_FMT, result.info.width, result.info.height,
					result.info.bits_per_pixel, result.info.interlace_passes);
			else
				statistics.Format(IW::Text::PNG_FMT, result.info.width, result.info.height, result.info.bits_per_pixel);
			if (output->WantRawImage()) output->AddImageData(input, IW::MetaDataTypes::PNG_IMAGE);
		}
		else if (format == libfiles::image_format::jpeg)
		{
			if (result.info.version)
				statistics.Format(L"%dx%dx%d Jpeg JFIF %d.%d", result.info.width, result.info.height,
					result.info.bits_per_pixel, result.info.version >> 8, result.info.version & 255);
			else
				statistics.Format(L"%dx%dx%d Jpeg", result.info.width, result.info.height, result.info.bits_per_pixel);
			if (output->WantRawImage()) output->AddImageData(input, IW::MetaDataTypes::JPEG_IMAGE);
		}
		else if (format == libfiles::image_format::gif)
		{
			statistics.Format(L"%dx%dx%d GIF", result.info.width, result.info.height, result.info.bits_per_pixel);
			if (result.info.interlace_passes > 1) statistics += CString(L" ") + CString(IW::Text::INTERLACED);
			if (result.info.frames > 1) statistics += CString(L" ") + CString(IW::Text::ANIMATED);
			output->SetImageFlags(IW::ImageFlags::Animate);
			output->AddImageData(input, IW::MetaDataTypes::GIF_IMAGE);
		}
		else if (format == libfiles::image_format::tiff)
		{
			const auto first = destination.first_size();
			statistics.Format(IW::Text::TIFF_FMT, first.cx, first.cy, result.info.bits_per_pixel,
				GetCompressionString(result.info.encoding), GetPhotometricString(result.info.color_model));
		}
		else if (format == libfiles::image_format::psd)
		{
			constexpr const wchar_t* modes[] = {L"Bitmap", L"Grayscale", L"Indexed", L"RGB", L"CMYK",
				L"??", L"??", L"Multichannel", L"Duotone", L"Lab"};
			const auto* mode = result.info.color_model < std::size(modes) ? modes[result.info.color_model] : L"??";
			statistics.Format(IW::Text::PSD_FMT, result.info.width, result.info.height, result.info.bits_per_pixel,
				result.info.encoding == 1 ? L" RLE" : L" ", mode);
		}
		output->SetStatistics(statistics);
		return true;
	}
}

// ---- Metadata ----
// ImageWalker by Zac Walker
//
// Purpose: MetadataProperties implementation and the field lookups
//          ImageMetaData exposes.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#include "LegacyPch.h"
#include "Metadata.h"


inline void IW::MetadataProperties::Begin(const CString& strSection)
{
	sections.push_back(MetadataSection());
	sections.back().name = strSection;
}

inline void IW::MetadataProperties::Add(const CString& strTitle, const CString& strValue)
{
	Add(g_szEmptyString, strTitle, g_szEmptyString, strValue);
}

inline void IW::MetadataProperties::Add(const CString& strKey, const CString& strTitle,
                                 const CString& strDescription, const CString& strValue)
{
	if (sections.empty())
		Begin(g_szEmptyString);

	MetadataProperty property;
	property.key = strKey;
	property.title = strTitle;
	property.description = strDescription;
	property.value = strValue;

	sections.back().properties.push_back(property);
}

inline const IW::MetadataProperty* IW::MetadataProperties::Find(const CString& strTitle) const
{
	for (const auto& section : sections)
	{
		for (const auto& property : section.properties)
		{
			if (property.title == strTitle)
				return &property;
		}
	}

	return nullptr;
}

inline ImageMetaData::ImageMetaData(const IW::Image& image) :
	_iptc(image.GetMetaData(IW::MetaDataTypes::PROFILE_IPTC)),
	_xmp(image.GetMetaData(IW::MetaDataTypes::PROFILE_XMP))
{
}

inline ImageMetaData::ImageMetaData(const IW::MetaData& iptc, const IW::MetaData& xmp) :
	_iptc(iptc),
	_xmp(xmp)
{
}

namespace iw::legacy_detail::metadata_schema
{
inline const CString photoshop = _T("http://ns.adobe.com/photoshop/1.0/");
inline const CString dc = _T("http://purl.org/dc/elements/1.1/");
inline const CString imagewalker = _T("http://www.imagewalker.com/1.0/");
}

inline CString ImageMetaData::GetTitle() const
{
	using namespace ::iw::legacy_detail::metadata_schema;
	CString str;
	if (!_xmp.Read(photoshop, _T("Headline"), str))
	{
		_iptc.Read(2, 105, str);
	}
	return str;
}

inline CString ImageMetaData::GetTags() const
{
	using namespace ::iw::legacy_detail::metadata_schema;
	CString str;
	if (!_xmp.ReadArray(dc, _T("subject"), str))
	{
		_iptc.Read(2, 25, str);
	}
	return str;
}

inline CString ImageMetaData::GetDescription() const
{
	using namespace ::iw::legacy_detail::metadata_schema;
	CString str;
	if (!_xmp.ReadAltText(dc, _T("description"), str))
	{
		_iptc.Read(2, 120, str);
	}
	return str;
}

inline CString ImageMetaData::GetCaptionWriter() const
{
	using namespace ::iw::legacy_detail::metadata_schema;
	CString str;
	if (!_xmp.Read(photoshop, _T("CaptionWriter"), str))
	{
		_iptc.Read(2, 122, str);
	}
	return str;
}

inline CString ImageMetaData::GetSpecialInstructions() const
{
	using namespace ::iw::legacy_detail::metadata_schema;
	CString str;
	if (!_xmp.Read(photoshop, _T("Instructions"), str))
	{
		_iptc.Read(2, 40, str);
	}
	return str;
}

inline CString ImageMetaData::GetByLine() const
{
	using namespace ::iw::legacy_detail::metadata_schema;
	CString str;
	if (!_xmp.ReadArray(dc, _T("creator"), str))
	{
		_iptc.Read(2, 80, str);
	}
	return str;
}

inline CString ImageMetaData::GetByLineTitle() const
{
	using namespace ::iw::legacy_detail::metadata_schema;
	CString str;
	if (!_xmp.Read(photoshop, _T("AuthorsPosition"), str))
	{
		_iptc.Read(2, 85, str);
	}
	return str;
}

inline CString ImageMetaData::GetCredit() const
{
	using namespace ::iw::legacy_detail::metadata_schema;
	CString str;
	if (!_xmp.Read(photoshop, _T("Credit"), str))
	{
		_iptc.Read(2, 110, str);
	}
	return str;
}

inline CString ImageMetaData::GetSource() const
{
	using namespace ::iw::legacy_detail::metadata_schema;
	CString str;
	if (!_xmp.Read(photoshop, _T("Source"), str))
	{
		_iptc.Read(2, 115, str);
	}
	return str;
}

inline CString ImageMetaData::GetCopyright() const
{
	using namespace ::iw::legacy_detail::metadata_schema;
	CString str;
	if (!_xmp.ReadAltText(dc, _T("rights"), str))
	{
		_iptc.Read(2, 116, str);
	}
	return str;
}

inline CString ImageMetaData::GetCategory() const
{
	using namespace ::iw::legacy_detail::metadata_schema;
	CString str;
	if (!_xmp.Read(photoshop, _T("Category"), str))
	{
		_iptc.Read(2, 15, str);
	}
	return str;
}

inline CString ImageMetaData::GetSubCategory() const
{
	using namespace ::iw::legacy_detail::metadata_schema;
	CString str;
	if (!_xmp.ReadArray(photoshop, _T("SupplementalCategory"), str))
	{
		_iptc.Read(2, 20, str);
	}
	return str;
}

inline CString ImageMetaData::GetObjectName() const
{
	using namespace ::iw::legacy_detail::metadata_schema;
	CString str;
	if (!_xmp.ReadAltText(dc, _T("title"), str))
	{
		_iptc.Read(2, 5, str);
	}
	return str;
}

inline CString ImageMetaData::GetDateCreated() const
{
	using namespace ::iw::legacy_detail::metadata_schema;
	CString str;
	if (!_xmp.Read(photoshop, _T("DateCreated"), str))
	{
		_iptc.Read(2, 55, str);
	}
	return str;
}

inline CString ImageMetaData::GetCity() const
{
	using namespace ::iw::legacy_detail::metadata_schema;
	CString str;
	if (!_xmp.Read(photoshop, _T("City"), str))
	{
		_iptc.Read(2, 90, str);
	}
	return str;
}

inline CString ImageMetaData::GetProvenceState() const
{
	using namespace ::iw::legacy_detail::metadata_schema;
	CString str;
	if (!_xmp.Read(photoshop, _T("State"), str))
	{
		_iptc.Read(2, 95, str);
	}
	return str;
}

inline CString ImageMetaData::GetCountryName() const
{
	using namespace ::iw::legacy_detail::metadata_schema;
	CString str;
	if (!_xmp.Read(photoshop, _T("Country"), str))
	{
		_iptc.Read(2, 101, str);
	}
	return str;
}

inline CString ImageMetaData::GetOriginalTR() const
{
	using namespace ::iw::legacy_detail::metadata_schema;
	CString str;
	if (!_xmp.Read(photoshop, _T("TransmissionReference"), str))
	{
		_iptc.Read(2, 103, str);
	}
	return str;
}

// ---- MetadataExif ----
// ImageWalker by Zac Walker
//
// Purpose: EXIF through libfiles - the camera settings, date taken and orientation
//          a loader reads, the numbers a lossless turn rewrites, and the
//          property reader: every IFD plus maker notes, with each tag name,
//          title and description, as libfiles lists them.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#include "LegacyPch.h"
#include "Metadata.h"
#include "MetadataExif.h"

#include <libfiles/exif.h>
#include <libfiles/exif_listing.h>
#include <libfiles/properties.h>
#include <libfiles/scan.h>

#include <cmath>

namespace iw::legacy_detail::source_metadataexif
{
	// The TIFF stream after the APP1 segment's "Exif\0\0", or nothing.
	inline std::span<std::uint8_t> TiffOf(unsigned char* d, const unsigned int size)
	{
		static constexpr unsigned char signature[] = {'E', 'x', 'i', 'f', 0, 0};

		if (d == nullptr || size < 14 || memcmp(d, signature, sizeof signature) != 0)
			return {};

		return {d + sizeof signature, size - sizeof signature};
	}

	// value as a fraction over denominator, or false when the numerator would not fit.
	inline bool ToRational(const double value, const IW::UInt32 denominator, IW::Rational& out)
	{
		const double numerator = value * denominator;
		if (!(numerator >= 0.0 && numerator <= 4294967295.0))
			return false;

		out.numerator = static_cast<IW::UInt32>(std::llround(numerator));
		out.denominator = denominator;
		return true;
	}
}
using iw::legacy_detail::source_metadataexif::TiffOf;
using iw::legacy_detail::source_metadataexif::ToRational;

inline void Exif::Parse(IW::CameraSettings* pSettings, const unsigned char* d, const unsigned int size)
{
	const auto tiff = TiffOf(const_cast<unsigned char*>(d), size);
	if (tiff.empty())
		return;

	const auto exif = libfiles::read_exif(tiff);

	if (exif.orientation)
		pSettings->Orientation = *exif.orientation;

	if (exif.f_number >= 1.0f)
		ToRational(2.0 * std::log2(exif.f_number), 10000, pSettings->Aperture);

	// Under a second as 1/n, which FormatExposureTime shows as it shows a camera's.
	if (exif.exposure_time > 0.0f && exif.exposure_time < 1.0f)
	{
		IW::Rational time;
		if (ToRational(1.0 / exif.exposure_time, 1, time))
		{
			time.denominator = time.numerator;
			time.numerator = 1;
			pSettings->ExposureTime = time;
		}
	}
	else if (exif.exposure_time >= 1.0f)
	{
		ToRational(exif.exposure_time, 1000, pSettings->ExposureTime);
	}

	if (exif.iso_speed != 0)
		pSettings->IsoSpeed = exif.iso_speed;

	if (exif.focal_length > 0.0f)
		ToRational(exif.focal_length, 1000, pSettings->FocalLength);

	if (exif.focal_length_35mm != 0)
		pSettings->FocalLength35mmEquivalent = exif.focal_length_35mm;

	// When it was taken, by libfiles' one date rule, as the camera's clock showed it: an offset the block
	// states is not applied, so a photo shows the time its camera showed, as Diffractor shows it.
	if (const auto taken = libfiles::properties_from_exif(exif).created; taken.valid() && taken.has_time)
		pSettings->DateTaken.SetLocalClock(taken.year, taken.month, taken.day, taken.hour, taken.minute, taken.second);
}

inline int Exif::ReadOrientation(const unsigned char* d, const unsigned int size)
{
	const auto tiff = TiffOf(const_cast<unsigned char*>(d), size);
	const auto orientation = tiff.empty() ? std::nullopt : libfiles::summarize_exif(tiff).orientation;

	if (!orientation || *orientation < IW::Orientation::TopLeft || *orientation > IW::Orientation::LeftBottom)
		return IW::Orientation::TopLeft;

	return *orientation;
}

inline void Exif::FixExif(unsigned char* d, const unsigned int size, const int cx, const int cy)
{
	const libfiles::exif_number numbers[] = {
		{libfiles::exif_directory::exif, 0xa002, static_cast<std::uint32_t>(cx)}, // PixelXDimension
		{libfiles::exif_directory::exif, 0xa003, static_cast<std::uint32_t>(cy)}, // PixelYDimension
	};

	libfiles::set_exif_numbers(TiffOf(d, size), numbers);
}

inline void Exif::SetOrientation(unsigned char* d, const unsigned int size, const int nOrientation)
{
	if (nOrientation < IW::Orientation::TopLeft || nOrientation > IW::Orientation::LeftBottom)
		return;

	const libfiles::exif_number numbers[] = {
		{libfiles::exif_directory::primary, 0x0112, static_cast<std::uint32_t>(nOrientation)},
		{libfiles::exif_directory::thumbnail, 0x0112, static_cast<std::uint32_t>(nOrientation)},
	};

	libfiles::set_exif_numbers(TiffOf(d, size), numbers);
}

// libexif's defaults, which the property pages always listed with: a tag it does not
// name in its directory is left out, and so is a maker note value it does not know.
inline MetadataExif::MetadataExif(const IW::MetaData& data) : _listing(std::make_unique<libfiles::exif_listing>())
{
	if (!data.IsEmpty())
		*_listing = libfiles::list_exif({data.GetData(), data.GetDataSize()}, {.ignore_unknown_tags = true});
}

inline MetadataExif::~MetadataExif() = default;

// The listing's text, converted as the property pages always converted libexif's.
namespace iw::legacy_detail::metadata_view
{
	inline CString Text(const std::optional<std::string>& text)
	{
		return text ? CString(text->c_str()) : CString();
	}

	inline LPCTSTR SectionName(const libfiles::exif_directory directory)
	{
		switch (directory)
		{
		case libfiles::exif_directory::primary: return g_szIFD0;
		case libfiles::exif_directory::thumbnail: return g_szIFD1;
		case libfiles::exif_directory::exif: return g_szIFD;
		case libfiles::exif_directory::gps: return g_szIFDGPS;
		case libfiles::exif_directory::interoperability: return g_szIFDInteroperability;
		}

		return g_szEmptyString;
	}
}

inline void MetadataExif::Load(IW::MetadataProperties& propertiesOut) const
{
	for (const auto& directory : _listing->directories)
	{
		propertiesOut.Begin(iw::legacy_detail::metadata_view::SectionName(directory.directory));

		for (const auto& e : directory.entries)
		{
			propertiesOut.Add(iw::legacy_detail::metadata_view::Text(e.name),
			                  iw::legacy_detail::metadata_view::Text(e.title),
			                  iw::legacy_detail::metadata_view::Text(e.description),
			                  e.bytes.empty() ? CString() : CString(e.value.c_str()));
		}
	}

	if (_listing->maker_note.empty())
		return;

	propertiesOut.Begin(_T("Maker Notes"));

	for (const auto& v : _listing->maker_note)
	{
		if (!v.name || v.name->empty())
			continue;

		propertiesOut.Add(iw::legacy_detail::metadata_view::Text(v.name),
		                  iw::legacy_detail::metadata_view::Text(v.title),
		                  iw::legacy_detail::metadata_view::Text(v.description),
		                  CString(v.value.c_str()));
	}
}

// ---- MetadataIPTC ----
// ImageWalker by Zac Walker
//
// Purpose: IPTC implementation - the dataset tag table, the datasets libfiles
//          reads, and the grouping the property page shows.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#include "LegacyPch.h"
#include "Metadata.h"

#include <libfiles/iptc.h>
#include <libfiles/scan.h>

inline IptcSpec g_iptc_tags[] = {
	{0, IW::Text::IPTC_RECORDVERSION},
	{5, IW::Text::IPTC_OBJECTNAME},
	{7, IW::Text::IPTC_EDITSTATUS},
	{8, IW::Text::IPTC_EDITORIALUPDATE},
	{10, IW::Text::IPTC_URGENCY},
	{12, IW::Text::IPTC_SUBJECTREFERENCE},
	{15, IW::Text::IPTC_CATEGORY},
	{20, IW::Text::IPTC_SUPPLEMENTALCATEGORY},
	{22, IW::Text::IPTC_FIXTUREIDENTIFIER},
	{25, IW::Text::IPTC_KEYWORDS},
	{26, IW::Text::IPTC_CONTENTLOCATIONCODE},
	{27, IW::Text::IPTC_CONTENTLOCATIONNAME},
	{30, IW::Text::IPTC_RELEASEDATE},
	{35, IW::Text::IPTC_RELEASETIME},
	{37, IW::Text::IPTC_EXPIRATIONDATE},
	{38, IW::Text::IPTC_EXPIRATIONTIME},
	{40, IW::Text::IPTC_SPECIALINSTRUCTIONS},
	{42, IW::Text::IPTC_ACTIONADVISED},
	{45, IW::Text::IPTC_REFERENCESERVICE},
	{47, IW::Text::IPTC_REFERENCEDATE},
	{50, IW::Text::IPTC_REFERENCENUMBER},
	{55, IW::Text::IPTC_DATECREATED},
	{60, IW::Text::IPTC_TIMECREATED},
	{62, IW::Text::IPTC_DIGITALCREATIONDATE},
	{63, IW::Text::IPTC_DIGITALCREATIONTIME},
	{65, IW::Text::IPTC_ORIGINATINGPROGRAM},
	{70, IW::Text::IPTC_PROGRAMVERSION},
	{75, IW::Text::IPTC_OBJECTCYCLE},
	{80, IW::Text::IPTC_BYLINE},
	{85, IW::Text::IPTC_BYLINETITLE},
	{90, IW::Text::IPTC_CITY},
	{92, IW::Text::IPTC_SUBLOCATION},
	{95, IW::Text::IPTC_PROVINCESTATE},
	{100, IW::Text::IPTC_COUNTRYPRIMARYLOCATIONCODE},
	{101, IW::Text::IPTC_COUNTRYPRIMARYLOCATIONNAME},
	{103, IW::Text::IPTC_ORIGINALTRANSMISSIONREFERENCE},
	{105, IW::Text::IPTC_HEADLINE},
	{110, IW::Text::IPTC_CREDIT},
	{115, IW::Text::IPTC_SOURCE},
	{116, IW::Text::IPTC_COPYRIGHTNOTICE},
	{118, IW::Text::IPTC_CONTACT},
	{120, IW::Text::IPTC_CAPTIONABSTRACT},
	{122, IW::Text::IPTC_WRITEREDITOR},
	{125, IW::Text::IPTC_RASTERIZEDCAPTION},
	{130, IW::Text::IPTC_IMAGETYPE},
	{131, IW::Text::IPTC_IMAGEORIENTATION},
	{135, IW::Text::IPTC_LANGUAGEIDENTIFIER},
	{-1, nullptr}
};

// libfiles gives the text as UTF-8: as stored where that is valid UTF-8, and
// read as Windows-1252 where it is not.
inline CString FromUtf8(const std::string& text)
{
	CString str;
	const int cch = ::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);

	if (cch > 0)
	{
		::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), str.GetBuffer(cch), cch);
		str.ReleaseBuffer(cch);
	}

	return str;
}

inline MetadataIPTC::MetadataIPTC(const IW::MetaData& data)
{
	const auto iim = GetIim(data);

	for (const auto& entry : libfiles::list_iptc(iim))
		_datasets.push_back({entry.record, entry.dataset, FromUtf8(entry.value)});
}

inline std::vector<BYTE> MetadataIPTC::GetIim(const IW::MetaData& data)
{
	if (data.IsEmpty())
		return {};

	const std::span<const std::uint8_t> blob(data.GetData(), data.GetDataSize());

	if (blob[0] == 0x1c)
		return {blob.begin(), blob.end()};

	constexpr std::string_view signature("Photoshop 3.0\0", 14);
	const bool bSigned = blob.size() >= signature.size() && memcmp(blob.data(), signature.data(), signature.size()) == 0;

	libfiles::metadata_blocks blocks;
	libfiles::parse_photoshop_resources(blocks, blob, bSigned);
	return std::move(blocks.iptc);
}

inline CString MetadataIPTC::GetKey(int nDataset, int nRecord)
{
	for (int i = 0; g_iptc_tags[i].nId != -1; ++i)
	{
		if (g_iptc_tags[i].nId == nRecord)
			return CString(g_iptc_tags[i].name);
	}

	CString str;
	str.Format(_T("IPTC:%d:%d"), nDataset, nRecord);
	return str;
}

inline bool MetadataIPTC::Read(int nDataset, int nRecord, CString& strOut) const
{
	strOut.Empty();

	for (const auto& dataset : _datasets)
	{
		if (dataset.nRecord != nDataset || dataset.nNumber != nRecord)
			continue;

		if (!strOut.IsEmpty()) strOut += _T("; ");
		strOut += dataset.strValue;
	}

	return true;
}

// The panel groups the records the way every other IPTC editor does, so a
// photographer can find a field without knowing its dataset number. Anything
// the file carries that is not in the list still shows, under Other.
inline void MetadataIPTC::Load(IW::MetadataProperties& propertiesOut) const
{
	struct Group
	{
		LPCTSTR* pszSection;
		std::initializer_list<int> records;
	};

	const Group groups[] = {
		{&g_szCaption, {120, 122, 105, 40}},
		{&g_szKeywordsCategories, {25, 15, 20}},
		{&g_szCredits, {80, 85, 110, 115}},
		{&g_szOrigin, {5, 55, 60, 62, 63, 65, 70, 75, 90, 92, 95, 100, 101, 103}},
		{&g_szCopyright, {116, 118}},
		{&g_szEditorial, {7, 8, 10, 12, 22, 26, 27, 30, 35, 37, 38, 42, 45, 47, 50}},
		{&g_szImage, {130, 131, 135}},
	};

	std::map<int, CString> values;
	std::vector<int> order;

	for (const auto& dataset : _datasets)
	{
		if (dataset.nRecord != 2)
			continue;

		auto it = values.find(dataset.nNumber);

		if (it == values.end())
		{
			values[dataset.nNumber] = dataset.strValue;
			order.push_back(dataset.nNumber);
		}
		else
		{
			it->second += _T("; ");
			it->second += dataset.strValue;
		}
	}

	if (values.empty())
		return;

	std::set<int> shown;

	for (const auto& group : groups)
	{
		bool bSectionStarted = false;

		for (int record : group.records)
		{
			auto it = values.find(record);

			if (it == values.end() || it->second.IsEmpty())
				continue;

			if (!bSectionStarted)
			{
				propertiesOut.Begin(*group.pszSection);
				bSectionStarted = true;
			}

			propertiesOut.Add(GetKey(2, record), it->second);
			shown.insert(record);
		}
	}

	bool bOtherStarted = false;

	for (int record : order)
	{
		if (shown.contains(record) || values[record].IsEmpty())
			continue;

		if (!bOtherStarted)
		{
			propertiesOut.Begin(_T("Other"));
			bOtherStarted = true;
		}

		propertiesOut.Add(GetKey(2, record), values[record]);
	}
}

// ---- MetadataXMP ----
// ImageWalker by Zac Walker
//
// Purpose: XMP packet parsing, through libfiles, and the property lookups and
//          listing built on it.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#include "LegacyPch.h"
#include "Metadata.h"

#include <libfiles/xmp.h>

namespace iw::legacy_detail::source_metadataxmp
{
	namespace xmp = libfiles::xmp;
	using Nodes = std::vector<xmp::property_info>;

	inline std::string Narrow(const CString& text)
	{
		return std::string(CW2A(text, CP_UTF8));
	}

	inline CString Wide(const std::string& text)
	{
		return CString(CA2W(text.c_str(), CP_UTF8));
	}

	// Values are shown and searched without the whitespace around them, as they always were.
	inline std::string Trim(const std::string& s)
	{
		const size_t b = s.find_first_not_of(" \t\r\n");
		if (b == std::string::npos) return std::string();
		const size_t e = s.find_last_not_of(" \t\r\n");
		return s.substr(b, e - b + 1);
	}

	inline std::string Qualified(const std::string& schema, const std::string& name)
	{
		std::string prefix;
		return xmp::namespace_prefix(schema, &prefix) ? prefix + name : name;
	}

	// Whether `node` lies under the node at `base`: one of its fields, items or
	// qualifiers, or theirs.
	inline bool Under(const xmp::property_info& base, const xmp::property_info& node)
	{
		const auto& b = base.path;
		const auto& p = node.path;
		return node.schema == base.schema && p.size() > b.size() && p.compare(0, b.size(), b) == 0 &&
			(p[b.size()] == '/' || p[b.size()] == '[');
	}

	inline size_t Find(const Nodes& nodes, const std::string& schema, const std::string& path)
	{
		for (size_t i = 0; i < nodes.size(); ++i)
		{
			if (nodes[i].schema == schema && nodes[i].path == path)
				return i;
		}

		return nodes.size();
	}

	// A struct, or an array within one, shown as the text of its fields in
	// document order. Qualifiers describe a value rather than being one.
	inline std::string Composite(const Nodes& nodes, const size_t at)
	{
		std::string out;

		for (size_t i = at + 1; i < nodes.size() && Under(nodes[at], nodes[i]); ++i)
		{
			if (nodes[i].path.find("/?", nodes[at].path.size()) != std::string::npos)
				continue;

			const auto value = Trim(nodes[i].value);

			if (!xmp::is_simple(nodes[i].options) || value.empty())
				continue;

			if (!out.empty()) out += "; ";
			out += value;
		}

		return out;
	}

	inline std::vector<std::string> Items(const Nodes& nodes, const size_t at)
	{
		std::vector<std::string> items;
		const auto& base = nodes[at].path;

		for (size_t i = at + 1; i < nodes.size() && Under(nodes[at], nodes[i]); ++i)
		{
			const auto& path = nodes[i].path;

			if (path[base.size()] != '[' || path.find(']', base.size()) + 1 != path.size())
				continue;

			items.push_back(xmp::is_simple(nodes[i].options) ? Trim(nodes[i].value) : Composite(nodes, i));
		}

		return items;
	}

	// Array items joined with "; ", quoting any item that contains the separator
	// so the join stays reversible by eye.
	inline std::string Join(const std::vector<std::string>& items)
	{
		const std::string sep = "; ";
		std::string out;

		for (size_t i = 0; i < items.size(); ++i)
		{
			if (i != 0) out += sep;

			if (items[i].find(sep) != std::string::npos)
				out += '"' + items[i] + '"';
			else
				out += items[i];
		}

		return out;
	}

	// The alt-text item for English, then the default, then the first; and its language.
	inline bool English(const xmp::meta& meta, const std::string& schema, const std::string& name, std::string& value,
	             std::string& lang)
	{
		if (!meta.get_localized_text(schema, name, {}, "en", &lang, &value))
			return false;

		value = Trim(value);
		return true;
	}

	// A top-level property's value as the property view shows it.
	inline std::string Shown(const xmp::meta& meta, const Nodes& nodes, const size_t at)
	{
		const auto& node = nodes[at];

		if (xmp::is_alt_text(node.options))
		{
			std::string value, lang;

			if (!English(meta, node.schema, node.path.substr(node.path.find(':') + 1), value, lang))
				return {};

			return lang.empty() ? value : value + " (" + lang + ")";
		}

		if (xmp::is_array(node.options))
			return Join(Items(nodes, at));

		if (xmp::is_struct(node.options))
			return Composite(nodes, at);

		return Trim(node.value);
	}
}
using namespace iw::legacy_detail::source_metadataxmp;

inline MetadataXMP::MetadataXMP(const IW::MetaData& data) : _meta(std::make_unique<xmp::meta>())
{
	const int size = data.GetDataSize();

	if (size <= 16)
		return;

	auto sz = reinterpret_cast<LPCSTR>(data.GetData());
	const size_t nLength = strnlen(sz, size);

	// The <?xpacket?> wrapper is OPTIONAL in XMP -- a writer that emits a bare
	// <x:xmpmeta>, or leads with a BOM or whitespace, is still valid. Requiring
	// it made those files look as though they carried no XMP at all.
	if (memchr(sz, '<', nLength) == nullptr)
		return;

	// Malformed XMP in a file is common and must not stop the load: libfiles keeps
	// whatever precedes the first fault, as Adobe's toolkit does.
	_meta->parse(std::string_view(sz, nLength));
}

inline MetadataXMP::~MetadataXMP() = default;

// A namespace the packet never declared is unregistered, and asking about one is
// misuse that libfiles reports by throwing, as the toolkit does: here it is a
// property the image does not have.
inline bool MetadataXMP::Read(const CString& schemaNS, const CString& propName, CString& value) const
try
{
	const auto schema = Narrow(schemaNS);
	const auto name = Narrow(propName);
	std::string text;
	xmp::option_bits options = 0;

	if (!_meta->get_property(schema, name, &text, &options) || xmp::is_array(options))
		return false;

	text = Trim(text);

	if (xmp::is_struct(options))
	{
		const auto nodes = _meta->properties();
		const auto at = Find(nodes, schema, Qualified(schema, name));
		text = at < nodes.size() ? Composite(nodes, at) : std::string();
	}

	if (text.empty())
		return false;

	value = Wide(text);
	return true;
}
catch (const xmp::error&)
{
	return false;
}

inline bool MetadataXMP::ReadArray(const CString& schemaNS, const CString& propName, CString& value) const
try
{
	const auto schema = Narrow(schemaNS);
	const auto name = Narrow(propName);
	std::string text;
	xmp::option_bits options = 0;

	if (!_meta->get_property(schema, name, &text, &options))
		return false;

	text = Trim(text);

	if (!xmp::is_simple(options))
	{
		const auto nodes = _meta->properties();
		const auto at = Find(nodes, schema, Qualified(schema, name));
		text = at >= nodes.size() ? std::string() : xmp::is_array(options) ? Join(Items(nodes, at)) : Composite(nodes, at);
	}

	value = Wide(text);
	return !value.IsEmpty();
}
catch (const xmp::error&)
{
	return false;
}

inline bool MetadataXMP::ReadAltText(const CString& schemaNS, const CString& propName, CString& value) const
try
{
	const auto schema = Narrow(schemaNS);
	const auto name = Narrow(propName);
	xmp::option_bits options = 0;

	if (!_meta->get_property(schema, name, nullptr, &options) || !xmp::is_alt_text(options))
		return false;

	std::string text, lang;

	if (!English(*_meta, schema, name, text, lang))
		return false;

	value = Wide(text);
	return true;
}
catch (const xmp::error&)
{
	return false;
}

inline void MetadataXMP::Load(IW::MetadataProperties& propertiesOut) const
try
{
	const auto nodes = _meta->properties();
	const std::string* pending = nullptr;

	for (size_t i = 0; i < nodes.size(); ++i)
	{
		const auto& node = nodes[i];

		if (xmp::is_schema(node.options))
		{
			pending = &node.schema;
			continue;
		}

		// Top-level properties only: their fields, items and qualifiers are shown in them.
		if (node.path.find_first_of("/[") != std::string::npos)
			continue;

		if (pending != nullptr)
		{
			// The prefix is what the photographer recognises; the URI is what makes
			// it unambiguous, so the section carries both.
			std::string prefix;
			xmp::namespace_prefix(*pending, &prefix);
			if (!prefix.empty() && prefix.back() == ':') prefix.pop_back();
			CString strSection;
			strSection.Format(_T("%hs (%hs)"), prefix.c_str(), pending->c_str());
			propertiesOut.Begin(strSection);
			pending = nullptr;
		}

		propertiesOut.Add(Wide(node.path.substr(node.path.find(':') + 1)),
		                  Wide(Shown(*_meta, nodes, i)));
	}
}
catch (const xmp::error&)
{
	// What was listed before the fault stays listed.
}

// ---- Support ----
#include "LegacyPch.h"

inline SIZE_T IW::MemoryUsage = 0;

inline IW::CNullStatus iw_legacy_nullStatus;
inline IW::CNullStatus* IW::CNullStatus::Instance = &iw_legacy_nullStatus;


#ifdef _DEBUG // use debug heap

inline LPVOID IW::Alloc(SIZE_T dwBytes, LPCSTR szFile, int nLine)
{
	assert(dwBytes > 0);
	LPVOID p = _malloc_dbg(dwBytes, _NORMAL_BLOCK, szFile, nLine);
	if (p == nullptr) throw std::bad_alloc();
	MemoryUsage += dwBytes;
	return p;
}

inline LPVOID IW::ReAlloc(LPVOID lpMem, SIZE_T dwBytes, LPCSTR szFile, int nLine)
{
	assert(dwBytes > 0);
	if (lpMem) MemoryUsage -= _msize_dbg(lpMem, _NORMAL_BLOCK);
	LPVOID p = _realloc_dbg(lpMem, dwBytes, _NORMAL_BLOCK, szFile, nLine);
	if (p == nullptr) throw std::bad_alloc();
	MemoryUsage += dwBytes;
	return p;
}


inline void IW::Free(LPVOID lpMem)
{
	if (lpMem) MemoryUsage -= _msize_dbg(lpMem, _NORMAL_BLOCK);
	_free_dbg(lpMem, _NORMAL_BLOCK);
}


#else

inline LPVOID IW::Alloc(SIZE_T dwBytes)
{
	assert(dwBytes > 0);
	LPVOID p = malloc(dwBytes);
	if (p == NULL) throw std::bad_alloc();
	MemoryUsage += dwBytes;
	return p;
}

inline LPVOID IW::ReAlloc(LPVOID lpMem, SIZE_T dwBytes)
{
	assert(dwBytes > 0);
	if (lpMem) MemoryUsage -= _msize(lpMem);
	LPVOID p = realloc(lpMem, dwBytes);
	if (p == NULL) throw std::bad_alloc();
	MemoryUsage += dwBytes;
	return p;
}


inline void IW::Free(LPVOID lpMem)
{
	if (lpMem) MemoryUsage -= _msize(lpMem);
	free(lpMem);
}

#endif


inline void CopyInc(LPTSTR& szOut, LPCTSTR szSource)
{
	while (*szSource != 0)
	{
		*szOut++ = *szSource++;
	}
}

inline void CopyInc(LPTSTR& szOut, LPCTSTR szSource, int nCount)
{
	while (nCount-- > 0 && *szSource != 0)
	{
		*szOut++ = *szSource++;
	}
}

inline CString IW::TextToHtmlFriendly(const CString& strIn, int nLength)
{
	if (nLength == -1)
		nLength = strIn.GetLength();

	CString str;
	constexpr int nTempBufferSize = 20;
	TCHAR szTemp[nTempBufferSize];
	LPTSTR szOutFinal = str.GetBuffer(10 + nLength * 6);
	LPTSTR szOut = szOutFinal;
	*szOut = 0;
	LPCTSTR szIn = strIn;

	for (int i = 0; i < nLength; i++)
	{
		switch (szIn[i])
		{
		case _T('&'):
			CopyInc(szOut, _T("&amp;"));
			break;
		case _T('<'):
			CopyInc(szOut, _T("&lt;"));
			break;
		case _T('>'):
			CopyInc(szOut, _T("&gt;"));
			break;

		case _T('\"'):
			CopyInc(szOut, _T("&quot;"));
			break;
		case 9:
		case 10:
		case 13:
		case _T('\''):
			_stprintf_s(szTemp, nTempBufferSize, _T("&#%d;"), szIn[i]);
			CopyInc(szOut, szTemp);
			break;

		default:
			*szOut++ = szIn[i];
			break;
		}
	}

	*szOut++ = 0;
	str.ReleaseBuffer();

	return str;
}

inline CString IW::HtmlFriendlyToText(const CString& strIn, int nLength)
{
	if (nLength == 0)
		return g_szEmptyString;

	if (nLength == -1)
		nLength = strIn.GetLength();

	CString str;
	LPTSTR szOutFinal = str.GetBuffer(nLength + 10);
	LPTSTR szOut = szOutFinal;
	*szOut = 0;
	LPCTSTR szIn = strIn;


	int nLast = IW::LowerLimit<0>(nLength - 2);
	int i = 0;

	for (; i < nLast; i++)
	{
		if (szIn[i] == '<')
		{
			if (_tcsnicmp(_T("<BR/>"), szIn + i, 5) == 0)
			{
				*szOut++ = '\r';
				*szOut++ = '\n';
				i += 4;
			}
			else if (_tcsnicmp(_T("<SQ/>"), szIn + i, 5) == 0)
			{
				*szOut++ = '\'';
				i += 4;
			}
			else if (_tcsnicmp(_T("<DQ/>"), szIn + i, 5) == 0)
			{
				*szOut++ = '\"';
				i += 4;
			}
			else
			{
				*szOut++ = szIn[i]; //??
			}
		}
		else if (szIn[i] == '&')
		{
			if (szIn[i + 1] == '#')
			{
				// Wide, not char: &#8217; (a curly apostrophe) is common in
				// descriptions and truncated to 0x11 in a signed char.
				unsigned int ch = 0;

				if (szIn[i + 2] == 'x')
				{
					for (i = i + 3; szIn[i] && szIn[i] != ';'; i++)
					{
						if (szIn[i] >= 'a')
						{
							ch = (ch * 16) + (10 + szIn[i] - 'a');
						}
						else if (szIn[i] >= 'A')
						{
							ch = (ch * 16) + (10 + szIn[i] - 'A');
						}
						else
						{
							ch = (ch * 16) + szIn[i] - '0';
						}
					}
				}
				else
				{
					for (i = i + 2; szIn[i] && szIn[i] != ';'; i++)
					{
						ch = (ch * 10) + szIn[i] - '0';
					}
				}

				if (ch != 0 && ch <= 0xffff)
					*szOut++ = static_cast<TCHAR>(ch);
			}
			else if (_tcsnicmp(_T("&amp;"), szIn + i, 5) == 0)
			{
				*szOut++ = '&';
				i += 4;
			}
			else if (_tcsnicmp(_T("&lt;"), szIn + i, 4) == 0)
			{
				*szOut++ = '<';
				i += 3;
			}
			else if (_tcsnicmp(_T("&gt;"), szIn + i, 4) == 0)
			{
				*szOut++ = '>';
				i += 3;
			}
			else if (_tcsnicmp(_T("&quot;"), szIn + i, 6) == 0)
			{
				*szOut++ = '\"';
				i += 5;
			}
			else
			{
				*szOut++ = szIn[i]; //??
			}
		}
		else
		{
			*szOut++ = szIn[i];
		}
	}

	// Complete the last chars
	CopyInc(szOut, szIn + i, nLength - nLast);
	*szOut++ = 0;

	str.ReleaseBuffer();
	return str;
}

inline CString IW::MakeURLSafe(const CString& strIn)
{
	CString str(strIn);
	str.Replace(g_szSpace, _T("%20"));
	str.Replace(_T("&"), _T("%26"));
	str.Replace(_T("'"), _T("%27"));
	str.Replace(_T("\""), _T("%22"));
	str.Replace(_T("\\"), _T("/"));
	return str;
}

inline CString IW::MakeURLUnSafe(const CString& strIn)
{
	CString str(strIn);
	str.Replace(_T("%20"), g_szSpace);
	str.Replace(_T("%26"), _T("&"));
	str.Replace(_T("%27"), _T("'"));
	str.Replace(_T("%22"), _T("\""));
	return str;
}

// ---- UtilStrings ----
// ImageWalker by Zac Walker
//
// Purpose: The shared string constants, defined.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#include "LegacyPch.h"

inline auto g_szAnnotations = _T("Annotations");
inline auto g_szBackGround = _T("BackGround");
inline auto g_szBlue = _T("Blue");
inline auto g_szBrightness = _T("Brightness");
inline auto g_szCaption = _T("Caption");
inline auto g_szCaptionAbstract = _T("Caption/Abstract");
inline auto g_szCapture = _T("Capture");
inline auto g_szColor = _T("Color");
inline auto g_szColorBackGround = _T("ColorBackGround");
inline auto g_szColorFrame = _T("ColorFrame");
inline auto g_szColumns = _T("Columns");
inline auto g_szContrast = _T("Contrast");
inline auto g_szCopyright = _T("Copyright");
inline auto g_szCredits = _T("Credits");
inline auto g_szCrop = _T("Crop");
inline auto g_szDegrees = _T("Degrees");
inline auto g_szDelay = _T("Delay");
inline auto g_szDescription = _T("Description");
inline auto g_szDoubleClickShowsFullScreen = _T("DoubleClickShowsFullScreen");
inline auto g_szDropEdge = _T("DropEdge");
inline auto g_szEditorial = _T("Editorial");
inline auto g_szFileExt = _T("FileExt");
inline auto g_szFilter = _T("Filter");
inline auto g_szFolder = _T("Folder");
inline auto g_szFolderOut = _T("FolderOut");
inline auto g_szFooter = _T("Footer");
inline auto g_szFrame = _T("Frame");
inline auto g_szFrameType = _T("FrameType");
inline auto g_szFrameWidth = _T("FrameWidth");
inline auto g_szGIF = _T("GIF");
inline auto g_szGreen = _T("Green");
inline auto g_szGreyScale = _T("GreyScale");
inline auto g_szHeader = _T("Header");
inline auto g_szHeight = _T("Height");
inline auto g_szHome = _T("Home");
inline auto g_szHue = _T("Hue");
inline auto g_szIFD = _T("IFD");
inline auto g_szIFD0 = _T("IFD 0");
inline auto g_szIFD1 = _T("IFD 1");
inline auto g_szIFDGPS = _T("IFD GPS");
inline auto g_szIFDInteroperability = _T("IFD Interoperability");
inline auto g_szImage = _T("Image");
inline auto g_szImageColumns = _T("ImageColumns");
inline auto g_szImageRows = _T("ImageRows");
inline auto g_szIndexCX = _T("IndexCX");
inline auto g_szIndexCY = _T("IndexCY");
inline auto g_szIPTC219 = _T("IPTC:2:219");
inline auto g_szIPTC105 = _T("IPTC:2:105");
inline auto g_szIPTC120 = _T("IPTC:2:120");
inline auto g_szIPTC25 = _T("IPTC:2:25");
inline auto g_szIPTC15 = _T("IPTC:2:15");
inline auto g_szIPTC20 = _T("IPTC:2:20");
inline auto g_szKeepAspect = _T("KeepAspect");
inline auto g_szKey = _T("Key");
inline auto g_szKeyword = _T("Keyword");
inline auto g_szKeywordsCategories = _T("Keywords and Categories");
inline auto g_szLightness = _T("Lightness");
inline auto g_szLoader = _T("Loader");
inline auto g_szLowerCase = _T("LowerCase");
inline auto g_szMirrorLR = _T("MirrorLR");
inline auto g_szMirrorTB = _T("MirrorTB");
inline auto g_szMode = _T("Mode");
inline auto g_szModeFlags = _T("ModeFlags");
inline auto g_szOptimize = _T("Optimize");
inline auto g_szOrigin = _T("Origin");
inline auto g_szOutputFile = _T("OutputFile");
inline auto g_szOutputFolder = _T("OutputFolder");
inline auto g_szOverwrite = _T("Overwrite");
inline auto g_szPosition = _T("Position");
inline auto g_szPrinterDefaults = _T("PrinterDefaults");
inline auto g_szPrintLandscape = _T("PrintLandscape");
inline auto g_szPrintMargin = _T("PrintMargin");
inline auto g_szPrintOnePerPage = _T("PrintOnePerPage");
inline auto g_szPrintRotateBest = _T("PrintRotateBest");
inline auto g_szPrintSelected = _T("PrintSelected");
inline auto g_szPrintSplitterPos = _T("PrintSplitterPos");
inline auto g_szProgressive = _T("Progressive");
inline auto g_szQuality = _T("Quality");
inline auto g_szRecurse = _T("Recurse");
inline auto g_szRecursSubFolders = _T("RecursSubFolders");
inline auto g_szRed = _T("Red");
inline auto g_szRemoveIllegal = _T("RemoveIllegal");
inline auto g_szRepeat = _T("Repeat");
inline auto g_szRotate180 = _T("Rotate180");
inline auto g_szRotate270 = _T("Rotate270");
inline auto g_szRotate90 = _T("Rotate90");
inline auto g_szRotateX = _T("RotateX");
inline auto g_szLossLess = _T("LossLess");
inline auto g_szRotations = _T("Rotations");
inline auto g_szRows = _T("Rows");
inline auto g_szSaturation = _T("Saturation");
inline auto g_szVibrance = _T("Vibrance");
inline auto g_szTemperature = _T("Temperature");
inline auto g_szTint = _T("Tint");
inline auto g_szDarks = _T("Darks");
inline auto g_szMidtones = _T("Midtones");
inline auto g_szLights = _T("Lights");
inline auto g_szRotate = _T("Rotate");
inline auto g_szStraighten = _T("Straighten");
inline auto g_szPerspectiveH = _T("PerspectiveH");
inline auto g_szPerspectiveV = _T("PerspectiveV");
inline auto g_szScale = _T("Scale");
inline auto g_szScaleDown = _T("ScaleDown");
inline auto g_szSection = _T("Section");
inline auto g_szShadow = _T("Shadow");
inline auto g_szShadowWidth = _T("ShadowWidth");
inline auto g_szShowButtons = _T("ShowButtons");
inline auto g_szShowFooter = _T("ShowFooter");
inline auto g_szShowHeader = _T("ShowHeader");
inline auto g_szShowInformation = _T("ShowInformation");
inline auto g_szShowPageNumbers = _T("ShowPageNumbers");
inline auto g_szShuffle = _T("Shuffle");
inline auto g_szSizeCX = _T("SizeCX");
inline auto g_szSizeCY = _T("SizeCY");
inline auto g_szViewMode = _T("ViewMode");
inline auto g_szSplitterPos = _T("SplitterPos");
inline auto g_szStyleSheet = _T("StyleSheet Selection");
inline auto g_szStyleSheetSelection = _T("StyleSheetSelection");
inline auto g_szSubFolder = _T("SubFolder");
inline auto g_szTemplate = _T("Template");
inline auto g_szTemplateSelection = _T("TemplateSelection");
inline auto g_szThumbCX = _T("ThumbCX");
inline auto g_szThumbCY = _T("ThumbCY");
inline auto g_szThumbSize = _T("ThumbSize");
inline auto g_szTitle = _T("Title");
inline auto g_szToolPlugins = _T("Tool Plugins");
inline auto g_szTool = _T("Tool");
inline auto g_szType = _T("Type");
inline auto g_szUseEffects = _T("UseEffects");
inline auto g_szViewAnnotations = _T("ViewAnnotations");
inline auto g_szViewColumns = _T("ViewColumns");
inline auto g_szWebDefaults = _T("WebDefaults");
inline auto g_szWebSplitterPos = _T("WebSplitterPos");
inline auto g_szWidth = _T("Width");
inline auto g_szWindowRect = _T("WindowRect");
inline auto g_szWrite = _T("Write");
inline auto g_szList = _T("List");
inline auto g_szDefaults = _T("Defaults");
inline auto g_szCodec = _T("Codec");
inline auto g_szIPTC = _T("IPTC");
inline auto g_szSpace = _T(" ");
inline auto g_szEmpty = _T("Empty");
inline auto g_szEmptyString = _T("");
inline auto g_szCRLF = _T("\r\n");
inline auto g_szCR = _T("\r");
inline auto g_szLF = _T("\n");
inline auto g_szJPG = _T("JPG");
inline auto g_szDLL = _T("Dll");
inline auto g_szSettings = _T("Settings");
inline auto g_szProperty = _T("Property");
inline auto g_szName = _T("Name");
inline auto g_szTrue = _T("True");
inline auto g_szFalse = _T("False");
inline auto g_szImageWalker = _T("ImageWalker");
inline auto g_szCHM = _T("CHM");
inline auto g_szYear = _T("Year");
inline auto g_szMonth = _T("Month");
inline auto g_szDayOfWeek = _T("Day Of Week");
inline auto g_szDay = _T("Day");
inline auto g_szHour = _T("Hour");
inline auto g_szMinute = _T("Minute");
inline auto g_szSecond = _T("Second");
inline auto g_szFileSize = _T("File Size");
inline auto g_szLoadAny = _T("Load Any");
inline auto g_szResolution = _T("Resolution");
inline auto g_szScaleFullScreen = _T("ScaleFullScreen");
inline auto g_szXPelsPerMeter = _T("XPelsPerMeter");
inline auto g_szYPelsPerMeter = _T("YPelsPerMeter");
inline auto g_szOptions = _T("Options");
inline auto g_szWizard = _T("Wizard");
inline auto g_szFilterX = _T("Filter %d");
inline auto g_szToolTips = _T("ToolTips");
inline auto g_szMarkers = _T("Markers");
inline auto g_szHidden = _T("Hidden");
inline auto g_szCopyToList = _T("CopyToList");
inline auto g_szDefaultFolder = _T("DefaultFolder");
inline auto g_szShowMode = _T("ShowMode");
inline auto g_szSettingsKey = _T("Settings");
inline auto g_szClassName = _T("ImageWalker.ImageWalker.1");
inline auto g_szPrintOptions = _T("PrintOptions");
inline auto g_szNormal = _T("Normal");
inline auto g_szEdit = _T("Edit");
inline auto g_szRotateLeft = _T("RotateLeft");
inline auto g_szRotateRight = _T("RotateRight");
inline auto g_szFolderView = _T("FolderView");
inline auto g_szSearch = _T("Search");
inline auto g_szText = _T("Text");
inline auto g_szOnlyShowImages = _T("OnlyShowImages");
inline auto g_szSize = _T("Size");
inline auto g_szDateModified = _T("DateModified");
inline auto g_szDateTaken = _T("DateTaken");
inline auto g_szSizeOption = _T("SizeOption");
inline auto g_szSizeKB = _T("SizeKB");
inline auto g_szNumberOfDays = _T("NumberOfDays");
inline auto g_szDefaultImageId = _T("DefaultImageId");
inline auto g_szCredit = _T("Credit");
inline auto g_szSource = _T("Source");
inline auto g_szTags = _T("Tags");
inline auto g_szRename = _T("Rename");
