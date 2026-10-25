#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <objbase.h>
#include <wincodec.h>
#include <wincodecsdk.h>

namespace iw::imaging
{
	class ComApartment
	{
	public:
		ComApartment() : result_(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
		~ComApartment() { if (SUCCEEDED(result_)) CoUninitialize(); }
		bool ready() const { return SUCCEEDED(result_) || result_ == RPC_E_CHANGED_MODE; }
		ComApartment(const ComApartment&) = delete;
		ComApartment& operator=(const ComApartment&) = delete;
	private:
		HRESULT result_;
	};
}
