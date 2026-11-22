#pragma once

#include <windows.h>
#include <shlobj.h>

#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace iw::recycle
{
	namespace detail
	{
		struct Release
		{
			void operator()(IUnknown* const item) const { item->Release(); }
		};

		template <class T> using ComPtr = std::unique_ptr<T, Release>;
	}

	// Call on a COM-initialized UI thread. A runtime failure may leave earlier
	// items in the Recycle Bin, but this operation never falls back to deletion.
	inline bool to_bin(const std::span<const std::filesystem::path> paths)
	{
		using detail::ComPtr;

		if (paths.empty()) return false;

		IShellItem* binItem = nullptr;
		const HRESULT binResult = SHCreateItemInKnownFolder(FOLDERID_RecycleBinFolder, 0, nullptr,
		                                                    IID_PPV_ARGS(&binItem));
		const ComPtr<IShellItem> bin(binItem);
		if (FAILED(binResult) || !bin) return false;

		struct Item
		{
			ComPtr<IShellItem> item;
			ComPtr<ITransferSource> source;
		};
		std::vector<Item> items;
		items.reserve(paths.size());

		for (const auto& path : paths)
		{
			const auto absolute = path.lexically_normal().make_preferred();
			const auto& name = absolute.native();
			if (name.size() < 4 || name[1] != L':' || name[2] != L'\\' ||
			    name.find_first_of(L"*?\"<>|\r\n") != std::wstring::npos || name.find(L':', 2) != std::wstring::npos ||
			    name.find(L'\0') != std::wstring::npos || !absolute.has_filename())
				return false;

			Item pending;
			IShellItem* sourceItem = nullptr;
			HRESULT result = SHCreateItemFromParsingName(name.c_str(), nullptr, IID_PPV_ARGS(&sourceItem));
			pending.item.reset(sourceItem);
			SFGAOF attrs = 0;
			if (FAILED(result) || !pending.item || pending.item->GetAttributes(SFGAO_FILESYSTEM, &attrs) != S_OK ||
			    !(attrs & SFGAO_FILESYSTEM))
				return false;

			IShellItem* parentItem = nullptr;
			result = pending.item->GetParent(&parentItem);
			const ComPtr<IShellItem> parent(parentItem);
			if (FAILED(result) || !parent) return false;

			ITransferSource* source = nullptr;
			result = parent->BindToHandler(nullptr, BHID_Transfer, IID_PPV_ARGS(&source));
			pending.source.reset(source);
			if (FAILED(result) || !pending.source) return false;

			items.push_back(std::move(pending));
		}

		// RecycleItem is distinct from the Shell transfer source's RemoveItem.
		for (const auto& pending : items)
		{
			IShellItem* recycledItem = nullptr;
			const HRESULT result = pending.source->RecycleItem(pending.item.get(), bin.get(), TSF_NORMAL,
			                                                   &recycledItem);
			const ComPtr<IShellItem> recycled(recycledItem);
			// The copy engine can return a success status other than S_OK.
			if (FAILED(result) || !recycled) return false;
		}
		return true;
	}

	inline bool to_bin(const std::filesystem::path& path)
	{
		return to_bin(std::span(&path, 1));
	}
}
