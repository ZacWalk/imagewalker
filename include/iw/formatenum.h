#pragma once

#include <atlbase.h>
#include <atlcom.h>
#include <cstddef>
#include <cstring>

namespace IW
{
	namespace Detail
	{
		struct FormatEtcCopy
		{
			static void init(FORMATETC* value) { *value = {}; }

			static HRESULT copy(FORMATETC* destination, const FORMATETC* source)
			{
				*destination = *source;
				destination->ptd = nullptr;
				if (!source->ptd) return S_OK;
				const DWORD size = source->ptd->tdSize;
				if (size < offsetof(DVTARGETDEVICE, tdData)) return DV_E_DVTARGETDEVICE;
				auto* device = static_cast<DVTARGETDEVICE*>(::CoTaskMemAlloc(size));
				if (!device) return E_OUTOFMEMORY;
				std::memcpy(device, source->ptd, size);
				destination->ptd = device;
				return S_OK;
			}

			static void destroy(FORMATETC* value)
			{
				::CoTaskMemFree(value->ptd);
				value->ptd = nullptr;
			}
		};
	}

	inline HRESULT CreateFormatEnumerator(UINT count, FORMATETC* formats, IEnumFORMATETC** result)
	{
		if (!result) return E_POINTER;
		*result = nullptr;
		if (count && !formats) return E_INVALIDARG;
		using Enumerator = ATL::CComEnum<IEnumFORMATETC, &IID_IEnumFORMATETC, FORMATETC, Detail::FormatEtcCopy>;
		ATL::CComObject<Enumerator>* raw = nullptr;
		HRESULT status = ATL::CComObject<Enumerator>::CreateInstance(&raw);
		if (FAILED(status)) return status;
		ATL::CComPtr<Enumerator> owned(raw);
		FORMATETC empty{};
		FORMATETC* first = count ? formats : &empty;
		status = owned->Init(first, first + count, nullptr, ATL::AtlFlagCopy);
		if (FAILED(status)) return status;
		return owned->QueryInterface(IID_IEnumFORMATETC, reinterpret_cast<void**>(result));
	}
}
