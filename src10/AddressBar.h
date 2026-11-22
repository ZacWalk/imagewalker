#pragma once

#include "shell.h"
#include <vector>

class CAddressBar : public CWindow
{
public:
	HWND Create(HWND owner, int id)
	{
		const auto window = CreateControl(WC_COMBOBOXEXA, owner, {0, 0, 100, 300}, nullptr,
		                                  CBS_DROPDOWN | WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, 0, id);
		if (window) SendMessage(CBEM_SETEXTENDEDSTYLE, CBES_EX_NOSIZELIMIT, CBES_EX_NOSIZELIMIT);
		return window;
	}
	HWND Edit() const { return reinterpret_cast<HWND>(SendMessage(CBEM_GETEDITCONTROL)); }
	bool HasKey(const MSG& msg) const
	{
		if (!IsWindow() || !msg.hwnd) return false;
		if (msg.hwnd == m_hWnd || ::IsChild(m_hWnd, msg.hwnd)) return true;
		COMBOBOXINFO info{sizeof(info)};
		return ::GetComboBoxInfo(Combo(), &info) && msg.hwnd == info.hwndList;
	}
	void SetAddress(const char* label, int icon)
	{
		label_ = label;
		COMBOBOXEXITEMA item{};
		item.mask = CBEIF_TEXT | CBEIF_IMAGE | CBEIF_SELECTEDIMAGE;
		item.iItem = -1;
		item.pszText = const_cast<char*>(label);
		item.iImage = item.iSelectedImage = icon;
		SendMessage(CBEM_SETITEMA, 0, reinterpret_cast<LPARAM>(&item));
		RestoreAddress();
		Invalidate();
	}
	void RestoreAddress() const { ::SetWindowTextA(Edit(), label_); }

	void DropDown(const CShellItem& current)
	{
		CWaitCursor wait;
		SendMessage(CB_RESETCONTENT);
		items_.clear();
		const CShellDesktopItem desktop;
		int selection = Add(desktop, 0);
		AddChildren(desktop, 1);
		for (const int folder : {CSIDL_MYPICTURES, CSIDL_MYVIDEO, CSIDL_MYMUSIC, CSIDL_DRIVES})
		{
			CShellItem item;
			if (item.Open(GetParent(), folder))
			{
				Add(item, 1);
				if (folder == CSIDL_DRIVES) AddChildren(item, 2);
			}
		}
		if (!current.IsNull())
		{
			auto parent = current;
			std::vector<CShellItem> parents;
			while (!parent.IsDesktop())
			{
				parent.StripToParent();
				if (!parent.IsDesktop()) parents.push_back(parent);
			}
			for (auto i = parents.rbegin(); i != parents.rend(); ++i) Add(*i, i->Depth());
			selection = Add(current, current.Depth());
		}
		if (selection >= 0) SendMessage(CB_SETCURSEL, selection);
	}

	CShellItem SelectedFolder() const
	{
		const auto selected = SendMessage(CB_GETCURSEL);
		if (selected < 0 || static_cast<size_t>(selected) >= items_.size()) return {};
		return items_[selected];
	}

	template<class Navigate>
	bool HandleKey(const MSG& message, const CShellItem& current, Navigate navigate)
	{
		if (message.message != WM_KEYDOWN || !HasKey(message) ||
			(message.wParam != VK_RETURN && message.wParam != VK_ESCAPE))
			return false;
		const auto combo = Combo();
		if (combo.SendMessage(CB_GETDROPPEDSTATE))
		{
			// Capture the choice before closing the popup and restoring its edit text.
			const auto item = message.wParam == VK_RETURN ? SelectedFolder() : CShellItem{};
			combo.SendMessage(CB_SHOWDROPDOWN, FALSE);
			RestoreAddress();
			if (!item.IsNull()) navigate(item);
			return true;
		}
		if (message.wParam == VK_ESCAPE)
		{
			RestoreAddress();
			return true;
		}

		auto text = WindowText(Edit());
		text.Trim();
		if (text.GetLength() >= 2 && text[0] == '"' && text[text.GetLength() - 1] == '"')
			text = text.Mid(1, text.GetLength() - 2);
		if (text.IsEmpty()) return true;
		if (text == "..")
		{
			if (!current.IsNull() && !current.IsDesktop())
			{
				auto parent = current;
				parent.StripToParent();
				navigate(parent);
			}
			return true;
		}
		const CShellDesktop desktop;
		const auto open = [&](const char* path)
		{
			CShellItem item;
			if (FAILED(ShellParseDisplayName(desktop, GetParent(), path, item)) ||
				(!item.IsDesktop() && !(ShellItemAttributes(item, SFGAO_FOLDER) & SFGAO_FOLDER)))
				return false;
			navigate(item);
			return true;
		};
		CText folder;
		if (::PathIsRelativeA(text) && current.GetPath(folder))
		{
			char combined[MAX_PATH]{};
			if (::PathCombineA(combined, folder, text) && open(combined)) return true;
		}
		if (!open(text)) ::MessageBeep(MB_OK);
		return true;
	}

private:
	CWindow Combo() const { return reinterpret_cast<HWND>(SendMessage(CBEM_GETCOMBOCONTROL)); }

	int Add(const CShellItem& item, int indent)
	{
		if (item.IsNull()) return -1;
		const auto found = std::find(items_.begin(), items_.end(), item);
		if (found != items_.end()) return static_cast<int>(found - items_.begin());
		SHFILEINFOA info{};
		if (!::SHGetFileInfoA(reinterpret_cast<LPCSTR>(item.GetItem()), 0, &info, sizeof(info),
		                      SHGFI_PIDL | SHGFI_SYSICONINDEX | SHGFI_SMALLICON | SHGFI_DISPLAYNAME))
			return -1;
		COMBOBOXEXITEMA row{};
		row.mask = CBEIF_TEXT | CBEIF_IMAGE | CBEIF_SELECTEDIMAGE | CBEIF_INDENT;
		row.iItem = -1;
		row.pszText = info.szDisplayName;
		row.iImage = row.iSelectedImage = info.iIcon;
		row.iIndent = indent;
		items_.push_back(item);
		const auto index = static_cast<int>(SendMessage(CBEM_INSERTITEMA, 0, reinterpret_cast<LPARAM>(&row)));
		if (index < 0) items_.pop_back();
		return index;
	}
	void AddChildren(const CShellItem& item, int indent)
	{
		if (item.IsNull()) return;
		CShellFolder folder;
		CShellItemEnum entries;
		if (FAILED(folder.Open(item)) || FAILED(entries.Create(GetParent(), folder, SHCONTF_FOLDERS))) return;
		LPITEMIDLIST next{};
		while (entries && entries->Next(1, &next, nullptr) == S_OK)
		{
			CShellItem relative, child;
			relative.Attach(next);
			child.Cat(item, relative);
			Add(child, indent);
		}
	}

	CText label_;
	std::vector<CShellItem> items_;
};
