// ImageWalker by Zac Walker
//
// Purpose: The options dialog.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

//
// ViewOptionsDlg.h : Declaration of the CViewOptions

#pragma once

#include "ViewDialogs.h"

// One list of metadata properties with Add / Remove / Move Up / Move Down.
// The options dialog holds two of them, so the control ids come in from the
// caller rather than being fixed the way IW::CPropertyDlgImpl fixes them.
class CPropertyListEditor
{
private:
	std::map<long, CString> _names;
	CListViewCtrl _list;
	int _nProperty;

public:
	CPropertyListEditor() : _nProperty(0)
	{
	}

	void Init(HWND hwndList, const IW::CArrayDWORD &values)
	{
		for (const auto &type : App.GetMetaDataTypes())
			_names[type.Id] = type.Title;

		_list = hwndList;
		_list.SetImageList(App.GetGlobalBitmap(), LVSIL_SMALL);
		_list.DeleteAllItems();

		for (int i = 0; i < values.GetSize(); i++)
			Insert(values[i]);
	}

	void Apply(IW::CArrayDWORD &values) const
	{
		values.RemoveAll();

		const int nCount = _list.GetItemCount();

		for (int i = 0; i < nCount; i++)
			values.Add(static_cast<DWORD>(_list.GetItemData(i)));
	}

	void Add()
	{
		IW::CPropertyAddDlg<IDD_PROPERTY_ADD> dlg;
		dlg._nProperty = _nProperty;

		if (IDOK == dlg.DoModal())
		{
			_nProperty = dlg._nProperty;
			Insert(_nProperty);
		}
	}

	void Remove()
	{
		int n;

		while (-1 != (n = _list.GetNextItem(-1, LVNI_SELECTED)))
			_list.DeleteItem(n);
	}

	void Move(int nStep)
	{
		const int n = _list.GetSelectionMark();
		const int nTo = n + nStep;

		if (n < 0 || nTo < 0 || nTo >= _list.GetItemCount())
			return;

		const int id = static_cast<int>(_list.GetItemData(n));

		_list.DeleteItem(n);
		Insert(id, nTo);
		_list.SetItemState(nTo, LVIS_FOCUSED | LVIS_SELECTED, 0x000F);
		_list.SetSelectionMark(nTo);
	}

private:

	void Insert(int nId, int nLoc = -1)
	{
		if (nLoc == -1)
			nLoc = _list.GetItemCount();

		LVITEM lvItem;
		IW::MemZero(&lvItem, sizeof(LVITEM));

		lvItem.mask = LVIF_TEXT | LVIF_IMAGE | LVIF_PARAM;
		lvItem.iItem = nLoc;
		lvItem.iSubItem = 0;
		lvItem.pszText = (LPTSTR)(LPCTSTR)_names[nId];
		lvItem.iImage = ImageIndex::Property;
		lvItem.lParam = nId;

		_list.InsertItem(&lvItem);
	}
};

// Every option on one dialog. This was six property sheet pages, which is more
// chrome than the settings behind it need.
class CViewOptions :
	public CDialogImpl<CViewOptions>,
	public IW::CImageLoaderDlgImpl<CViewOptions>
{
public:

	typedef IW::CImageLoaderDlgImpl<CViewOptions> LoaderBase;

	State &_state;
	CPropertyListEditor _annotations;
	CPropertyListEditor _columns;

	CViewOptions(State &state) : LoaderBase(state.Loaders), _state(state)
	{
		_strDefaultSelection = App.Settings.CaptureFormat;
	}

	enum { IDD = IDD_OPTIONS };

	// The loader mixin runs its own WM_INITDIALOG and leaves the message
	// unhandled, so the chain has to come before this dialog's handler.
	BEGIN_MSG_MAP(CViewOptions)
		CHAIN_MSG_MAP_ALT(LoaderBase, 0)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		COMMAND_ID_HANDLER(IDOK, OnOK)
		COMMAND_ID_HANDLER(IDCANCEL, OnCancelCmd)
		COMMAND_ID_HANDLER(IDHELP, OnHelpCmd)
		COMMAND_ID_HANDLER(IDC_ANNOTATION_ADD, OnAnnotationAdd)
		COMMAND_ID_HANDLER(IDC_ANNOTATION_REMOVE, OnAnnotationRemove)
		COMMAND_ID_HANDLER(IDC_ANNOTATION_MOVE_UP, OnAnnotationMoveUp)
		COMMAND_ID_HANDLER(IDC_ANNOTATION_MOVE_DOWN, OnAnnotationMoveDown)
		COMMAND_ID_HANDLER(IDC_COLUMN_ADD, OnColumnAdd)
		COMMAND_ID_HANDLER(IDC_COLUMN_REMOVE, OnColumnRemove)
		COMMAND_ID_HANDLER(IDC_COLUMN_MOVE_UP, OnColumnMoveUp)
		COMMAND_ID_HANDLER(IDC_COLUMN_MOVE_DOWN, OnColumnMoveDown)
	END_MSG_MAP()

	void OnChange()
	{
	}

	void OnHelp()
	{
		App.InvokeHelp(IW::GetMainWindow(), HELP_VIEW_OPTIONS);
	}

	LRESULT OnInitDialog(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled);
	LRESULT OnOK(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL &bHandled);

	LRESULT OnCancelCmd(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL & /*bHandled*/)
	{
		EndDialog(wID);
		return 0;
	}

	LRESULT OnHelpCmd(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL & /*bHandled*/)
	{
		OnHelp();
		return 0;
	}

	LRESULT OnAnnotationAdd(WORD, WORD, HWND, BOOL&) { _annotations.Add(); return 0; }
	LRESULT OnAnnotationRemove(WORD, WORD, HWND, BOOL&) { _annotations.Remove(); return 0; }
	LRESULT OnAnnotationMoveUp(WORD, WORD, HWND, BOOL&) { _annotations.Move(-1); return 0; }
	LRESULT OnAnnotationMoveDown(WORD, WORD, HWND, BOOL&) { _annotations.Move(1); return 0; }
	LRESULT OnColumnAdd(WORD, WORD, HWND, BOOL&) { _columns.Add(); return 0; }
	LRESULT OnColumnRemove(WORD, WORD, HWND, BOOL&) { _columns.Remove(); return 0; }
	LRESULT OnColumnMoveUp(WORD, WORD, HWND, BOOL&) { _columns.Move(-1); return 0; }
	LRESULT OnColumnMoveDown(WORD, WORD, HWND, BOOL&) { _columns.Move(1); return 0; }
};

