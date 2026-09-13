// ImageWalker by Zac Walker
// Loads optional OS media components from System32, never the working directory or PATH.

#include "MediaRuntime.h"
#include "Platform.h"

#include <bit>
#include <format>
#include <string>

namespace iw::media::native
{
	namespace
	{
		thread_local bool unavailableForThread{};

		HMODULE load_system_module(const std::wstring& directory, const wchar_t* name)
		{
			const auto path = directory + L"\\" + name;
			const HMODULE module = LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
			if (!module)
			{
				const DWORD error = GetLastError();
				platform::write_diagnostic(std::format(L"Optional media component {} is unavailable (Win32 {}).\n",
					name, error));
			}
			return module;
		}

		template<class Function>
		Function resolve(const HMODULE module, const char* name)
		{
			const auto address = GetProcAddress(module, name);
			if (!address)
			{
				const std::wstring exportName(name, name + std::char_traits<char>::length(name));
				platform::write_diagnostic(L"Optional media entry point is unavailable: " + exportName + L"\n");
				return nullptr;
			}
			return std::bit_cast<Function>(address);
		}
	}

	Api::Api()
	{
		std::wstring directory(MAX_PATH, L'\0');
		UINT length = GetSystemDirectoryW(directory.data(), static_cast<UINT>(directory.size()));
		if (length >= directory.size() && length <= 32768)
		{
			directory.resize(static_cast<size_t>(length) + 1);
			length = GetSystemDirectoryW(directory.data(), static_cast<UINT>(directory.size()));
		}
		if (!length || length >= directory.size())
		{
			platform::write_diagnostic(L"Unable to locate System32 for optional media playback.\n");
			return;
		}
		directory.resize(length);
		platform_.reset(load_system_module(directory, L"mfplat.dll"));
		if (!platform_) return;
		reader_.reset(load_system_module(directory, L"mfreadwrite.dll"));
		if (!reader_) return;
		play_.reset(load_system_module(directory, L"mfplay.dll"));
		if (!play_) return;
		startup = resolve<decltype(startup)>(platform_.get(), "MFStartup");
		shutdown = resolve<decltype(shutdown)>(platform_.get(), "MFShutdown");
		createAttributes = resolve<decltype(createAttributes)>(platform_.get(), "MFCreateAttributes");
		createFile = resolve<decltype(createFile)>(platform_.get(), "MFCreateFile");
		createMediaType = resolve<decltype(createMediaType)>(platform_.get(), "MFCreateMediaType");
		getStride = resolve<decltype(getStride)>(platform_.get(), "MFGetStrideForBitmapInfoHeader");
		createSourceReader = resolve<decltype(createSourceReader)>(reader_.get(), "MFCreateSourceReaderFromByteStream");
		createPlayer = resolve<decltype(createPlayer)>(play_.get(), "MFPCreateMediaPlayer");
	}

	Api::~Api() = default;

	void Api::ModuleCloser::operator()(const HMODULE module) const
	{
		if (module) FreeLibrary(module);
	}

	bool Api::available() const
	{
		return startup && shutdown && createAttributes && createFile && createMediaType &&
			getStride && createSourceReader && createPlayer;
	}

	std::shared_ptr<const Api> acquire()
	{
		if (unavailableForThread) return {};
		static const std::shared_ptr<const Api> cached = []
		{
			auto api = std::make_shared<Api>();
			return api->available() ? api : nullptr;
		}();
		return cached;
	}

	std::wstring_view unavailable_message()
	{
		return L"Audio/video support is unavailable because Windows media components could not be loaded. "
			L"On Windows N, enable the Media Feature Pack in Windows Optional Features, then restart ImageWalker. "
			L"Image browsing still works. No codec or feature is downloaded by ImageWalker.";
	}

	testing::ScopedUnavailableBackend::ScopedUnavailableBackend() : previous_(unavailableForThread)
	{
		unavailableForThread = true;
	}

	testing::ScopedUnavailableBackend::~ScopedUnavailableBackend()
	{
		unavailableForThread = previous_;
	}
}
