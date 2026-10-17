// ImageWalker by Zac Walker
//
// Purpose: The dialog helpers - resizing, the scrolling dialog and the
//          shared controls.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#pragma once

#include <iw/ColorButton.h>
#include "ImagingStreams.h"

class ImageLoaders;

namespace IW
{

////////////////////////////////////////////////////////////////////////////
// Focus helper

////////////////////////////////////////////////////////////////////////////
// Resize

template<class T>
class CDialogResizer
{
protected:
   CRect _rectOriginal;

   class CDialogItem
   {
   public:
      CDialogItem(const UINT nItemId,
         const UINT nFlags,
            const CRect &rect) :
         _id(nItemId),
         _flags(nFlags),
         _rect(rect)
      {
      }

	  const int Width() const
	  {
		  return _rect.right - _rect.left;
	  }

      UINT _id;
      UINT _flags;
      CRect _rect;
   };

   CSimpleArray<CDialogItem> _items;
   bool _bInit;
public:

   enum { 
      eLeft = 0x01,
      eRight = 0x02,
      eTop = 0x04,
      eBottom = 0x08,
	  eAlignBottom = 0x10
   };

   CDialogResizer()
   {
	   _bInit = false;
   }

   void _DialogInit(HWND hWnd)
   {
      //T *pT = static_cast<T*>(this);
      ::GetClientRect(hWnd, &_rectOriginal);
   }

   void ResizeAddItem(UINT nItemId, UINT nFlags)
   {
      T *pT = static_cast<T*>(this);

	  HWND hWndCtrl = pT->GetDlgItem(nItemId);

	  // Is this really a dialog item?
      ATLASSERT(hWndCtrl);

      CRect r;
      ::GetWindowRect(hWndCtrl, &r);

	  HWND hWndParent = GetParent(hWndCtrl);
      ::ScreenToClient(hWndParent, (LPPOINT)&r);
	  ::ScreenToClient(hWndParent, (LPPOINT)&r + 1);

      _items.Add(
         CDialogItem(
            nItemId,
            nFlags,
            r));

	  if (!_bInit)
	  {
		  _DialogInit(hWndParent);
		  _bInit = false;
	  }
   }

   void ResizeDialog()
   {
      T *pT = static_cast<T*>(this);
	  
	  if (_items.GetSize())
	  {
		  // turn on WS_CLIPCHILDREN
		  // This will stop all that flicker
		  //pT->ModifyStyle(0, WS_CLIPCHILDREN);

		  // We will be deferring four windows.
		  HDWP hdwp = ::BeginDeferWindowPos(_items.GetSize());

		  if (hdwp == NULL)
			  return;

		  CRect rectClient;
		  pT->GetClientRect(&rectClient);

		  for(int i = 0; i < _items.GetSize(); i++)
		  {
			 CRect r = _items[i]._rect;

			 if (_items[i]._flags & eAlignBottom)
			 {
				 r.top = (_items[i]._rect.top - _rectOriginal.bottom) + rectClient.bottom;
				 r.bottom = (_items[i]._rect.bottom - _rectOriginal.bottom) + rectClient.bottom;
			 }

			 switch(_items[i]._flags & 0x0f)
			 {
			 case eRight:
				r.right = (_items[i]._rect.right - _rectOriginal.right) + rectClient.right;
				r.left = r.right - _items[i].Width();
				break;

			 case eLeft:
				r.left = (_items[i]._rect.left - _rectOriginal.left) + rectClient.left;
				r.right = r.left + _items[i].Width();
				break;

			 case eLeft | eRight:
				r.right = (_items[i]._rect.right - _rectOriginal.right) + rectClient.right;
				r.left = (_items[i]._rect.left - _rectOriginal.left) + rectClient.left;
				break;

			 case eLeft | eRight | eBottom:
				r.right = (_items[i]._rect.right - _rectOriginal.right) + rectClient.right;
				r.left = (_items[i]._rect.left - _rectOriginal.left) + rectClient.left;
				r.bottom = (_items[i]._rect.bottom - _rectOriginal.bottom) + rectClient.bottom;
				break;

			case eBottom:
				r.bottom = (_items[i]._rect.bottom - _rectOriginal.bottom) + rectClient.bottom;
				break;

			case 0:
				break; // Nothing to do


			 default:
				// Need to implement this 
				// sizing case?
				ATLASSERT(0);
			 }
         
			 ::DeferWindowPos(hdwp, 
				 pT->GetDlgItem(_items[i]._id),
				NULL,
				r.left,
				r.top,
				r.right - r.left,
				r.bottom - r.top,
				SWP_NOZORDER);
		  }

		  ::EndDeferWindowPos(hdwp);

		  // force repaint now
		  //pT->UpdateWindow();
		  
		  // turn off WS_CLIPCHILDREN
		  //pT->ModifyStyle(WS_CLIPCHILDREN, 0);
	  }
   }

};

////////////////////////////////////////////////////////////////////////////
// Properties

template<int tID>
class CPropertyAddDlg : 
	public CDialogImpl<CPropertyAddDlg<tID> >
{
public:
	CPropertyAddDlg()
	{
		_nProperty = 0;
		_bAscending = 0;
	}

	enum { IDD = tID };
	long _nProperty;
	bool _bAscending;

	BEGIN_MSG_MAP(CPropertyAddDlg)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		COMMAND_ID_HANDLER(IDOK, OnCloseCmd)
		COMMAND_ID_HANDLER(IDCANCEL, OnCloseCmd)
	END_MSG_MAP()

	LRESULT OnInitDialog(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
	{
		CenterWindow(GetParent());

		HWND hwndCombo = GetDlgItem(IDC_PROPERTY);

		// Set the image list for the combo box.
		SendMessage(hwndCombo , CBEM_SETIMAGELIST, 0L, (LPARAM)(HIMAGELIST)App.GetGlobalBitmap());


		for (const auto &type : App.GetMetaDataTypes())
			AddMetaDataType(type.Id, type.Title);

		int nCount = static_cast<int>(SendMessage(hwndCombo , CB_GETCOUNT, 0, 0));
		
		for(int i = 0; i < nCount; i++)
		{
			int nId = static_cast<int>(SendMessage(hwndCombo, CB_GETITEMDATA, i, 0L));
			
			if (_nProperty == nId)
			{
				SendMessage(hwndCombo , CB_SETCURSEL, i, 0);
				break;
			}
		}		

		CheckDlgButton(IDC_ASCENDING, _bAscending ? BST_CHECKED : BST_UNCHECKED);
		

        bHandled = false;
		return (LRESULT)TRUE;
	}
	
	bool AddMetaDataType(DWORD dwId, const CString &strTitle)
	{
		HWND hCombo = GetDlgItem(IDC_PROPERTY);

		// We need to sort!!
		int nCount = static_cast<int>(SendMessage(hCombo , CB_GETCOUNT, 0, 0));
		int nPos = -1;
		
		for(int i = 0; i < nCount; i++)
		{
			// CB_GETLBTEXT has no buffer size, so the length must be asked for first
			int nLen = static_cast<int>(SendMessage(hCombo, CB_GETLBTEXTLEN, static_cast<WPARAM>(i), 0));

			if (nLen == CB_ERR)
				continue;

			CString strItem;
			SendMessage(hCombo, CB_GETLBTEXT, static_cast<WPARAM>(i), reinterpret_cast<LPARAM>(strItem.GetBuffer(nLen + 1)));
			strItem.ReleaseBuffer();
			
			if (_tcsicmp(strItem, strTitle) > 0)
			{
				nPos = i;
				break;
			}
		}
		
		COMBOBOXEXITEM cbI;
		
		// Each item has text, an lParam with extra data, and an image.
		cbI.mask = CBEIF_TEXT | CBEIF_LPARAM | CBEIF_IMAGE | CBEIF_SELECTEDIMAGE;    
		cbI.pszText = (LPTSTR)(LPCTSTR)strTitle;
		cbI.cchTextMax = strTitle.GetLength();
		cbI.lParam = dwId;
		cbI.iItem = nPos;          // Add the item to the end of the list.
		cbI.iSelectedImage = cbI.iImage = ImageIndex::Property;
		
		// Add the item to the combo box drop-down list.
		SendMessage(hCombo, CBEM_INSERTITEM, 0L,(LPARAM)&cbI);

		return true;
	}

	LRESULT OnCloseCmd(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled)
	{
		HWND hCombo = GetDlgItem(IDC_PROPERTY);

		int n = static_cast<int>(::SendMessage(hCombo, CB_GETCURSEL, 0, 0));
		_nProperty = static_cast<long>(::SendMessage(hCombo, CB_GETITEMDATA, n, 0));
		_bAscending  = BST_CHECKED == IsDlgButtonChecked(IDC_ASCENDING);

		EndDialog(wID);
		return 0;
	}
};



/////////////////////////////////////////////////////////////////////////////
// CPropertyDlg

template <class T>
class CPropertyDlgImpl 
{
public:
	CPropertyDlgImpl()
	{
		_nProperty = 0;
	}

	~CPropertyDlgImpl()
	{
	}

	int InsertItem(HWND hLV, int nId, int nLoc = -1)
	{
		if (nLoc == -1)
			nLoc = ListView_GetItemCount( hLV );

        LVITEM lvItem;
		IW::MemZero(&lvItem,sizeof(LVITEM));

        lvItem.mask = LVIF_TEXT|LVIF_IMAGE|LVIF_PARAM;
        lvItem.iItem = nLoc;
        lvItem.iSubItem = 0;
        lvItem.pszText = (LPTSTR)(LPCTSTR)m_mapNames[nId];
        lvItem.iImage = ImageIndex::Property;
		lvItem.lParam = nId;

        return ListView_InsertItem(hLV,&lvItem);

	}

	std::map<long, CString> m_mapNames;
	int _nProperty;

	

BEGIN_MSG_MAP(CPropertyDlgImpl)

	COMMAND_ID_HANDLER(IDC_ADD, OnAdd)
	COMMAND_ID_HANDLER(IDC_REMOVE, OnRemove)
	COMMAND_ID_HANDLER(IDC_MOVE_UP, OnMoveUp)
	COMMAND_ID_HANDLER(IDC_MOVE_DOWN, OnMoveDown)

END_MSG_MAP()
// Handler prototypes:
//  LRESULT MessageHandler(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
//  LRESULT CommandHandler(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
//  LRESULT NotifyHandler(int idCtrl, LPNMHDR pnmh, BOOL& bHandled);

	void OnInitProperties(IW::CArrayDWORD *pAnnotations)
	{
		T *pT = static_cast<T*>(this);	

		// Get list of annotation names
		for (const auto &type : App.GetMetaDataTypes())
			m_mapNames[type.Id] = type.Title;

		HWND hLV = pT->GetDlgItem(IDC_LIST);
        ListView_SetImageList(hLV, App.GetGlobalBitmap() ,LVSIL_SMALL);

		
        OnRevertProperties(pAnnotations);
	}

	void OnRevertProperties(IW::CArrayDWORD *pAnnotations)
    {
		T *pT = static_cast<T*>(this);

		// Assign image lists to control
		HWND hLV = pT->GetDlgItem(IDC_LIST);
		ListView_DeleteAllItems(hLV);

		for(int i = 0; i < pAnnotations->GetSize(); i++)
		{
			InsertItem(hLV, (*pAnnotations)[i]);
		}
	}

	bool OnApplyProperties(IW::CArrayDWORD *pAnnotations)
	{
		T *pT = static_cast<T*>(this);
		pAnnotations->RemoveAll();

		HWND hLV = pT->GetDlgItem(IDC_LIST);
		UINT    ix, cItems = ListView_GetItemCount( hLV );

		for ( ix = 0; ix < cItems; ix++ )
		{
			ListViewItem lvi(ix);
			
			if ( ListView_GetItem( hLV, &lvi ) )
			{
				pAnnotations->Add(static_cast<DWORD>(lvi.lParam));
			}
		}
		
		return true;
	}

	LRESULT OnAdd(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
	{
		T *pT = static_cast<T*>(this);
		CPropertyAddDlg<IDD_PROPERTY_ADD> dlg;
		dlg._nProperty = _nProperty;
		
		if (IDOK == dlg.DoModal())
		{
			_nProperty = dlg._nProperty;

			HWND hLV = pT->GetDlgItem(IDC_LIST);
			InsertItem(hLV, _nProperty);

			// todo
			pT->OnChange();
		}

		return 0;
	}

	

	LRESULT OnRemove(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
	{
		T *pT = static_cast<T*>(this);
		HWND hLV = pT->GetDlgItem(IDC_LIST);
		int n;
		
		while (-1 != (n = ListView_GetNextItem( hLV, -1, LVNI_SELECTED )))
		{
			ListView_DeleteItem(hLV, n);
		}

		pT->OnChange();

		return 0;
	}

	LRESULT OnMoveUp(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
	{
		T *pT = static_cast<T*>(this);
		HWND hLV = pT->GetDlgItem(IDC_LIST);
		int n = ListView_GetSelectionMark(hLV);

		if (n > 0)
		{
			ListViewItem lvi(n);
			
			if ( ListView_GetItem( hLV, &lvi ) )
			{
				ListView_DeleteItem(hLV, n);
				InsertItem(hLV, static_cast<int>(lvi.lParam), n - 1);
				ListView_SetItemState (hLV,  n - 1, LVIS_FOCUSED | LVIS_SELECTED, 0x000F);
				ListView_SetSelectionMark(hLV,  n - 1);
				
				// todo
				pT->OnChange();
				
			}
		}

		return 0;
	}

	LRESULT OnMoveDown(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
	{
		T *pT = static_cast<T*>(this);
		HWND hLV = pT->GetDlgItem(IDC_LIST);
		int n = ListView_GetSelectionMark(hLV);
		int nCount = ListView_GetItemCount (hLV);

		if (n < nCount - 1)
		{
			ListViewItem lvi(n);
			
			if ( ListView_GetItem( hLV, &lvi ) )
			{
				ListView_DeleteItem(hLV, n);
				InsertItem(hLV, static_cast<int>(lvi.lParam), n + 1);
				ListView_SetItemState (hLV,  n + 1, LVIS_FOCUSED | LVIS_SELECTED, 0x000F);
				ListView_SetSelectionMark(hLV,  n + 1);
				
				// todo
				pT->OnChange();
			}
		}

		return 0;
	}
};


template <class T>
class CImageLoaderDlgImpl
{
public:

	CComboBoxEx _combo;
	int _nDefaultSelection;
	CString _strDefaultSelection;

	ImageLoaders &_loaders;

	// Loaders and Filters
	IW::ImageLoaderInfoPtr m_pLoaderFactory;
	IW::RefPtr<IW::IImageLoader> m_pLoader;

	// This tool's own copy, so changing it here cannot move the app default.
	IW::CodecSettings Codec;

	// The hosted format settings panel, rebuilt whenever the format changes.
	CWindow _wndSettings;

	CImageLoaderDlgImpl(ImageLoaders &loaders) :
		_strDefaultSelection(g_szJPG), _loaders(loaders), Codec(App.Settings.Codec)
	{
	}

	~CImageLoaderDlgImpl() 
	{
	};


BEGIN_MSG_MAP(CImageLoaderDlgImpl<T>)
	MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
	COMMAND_HANDLER(IDC_COMBOBOXEX, CBN_SELCHANGE, OnImageLoaderChange)

END_MSG_MAP()

	void OnImageLoaderChange()
	{
		T *pT = static_cast<T*>(this);

		OnChangedLoader();
		UpdateSettingsPanel();
		pT->OnChange();
	}

	LRESULT OnImageLoaderChange(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
	{
		OnImageLoaderChange();
		return 0;
	}

	void UpdateSettingsPanel()
	{
		T *pT = static_cast<T*>(this);

		if (_wndSettings.IsWindow())
			_wndSettings.DestroyWindow();

		CWindow wndHost = pT->GetDlgItem(IDC_SETTINGS_HOST);

		if (wndHost.IsWindow() && m_pLoader != nullptr)
		{
			CRect rcHost;
			wndHost.GetWindowRect(rcHost);
			pT->ScreenToClient(rcHost);
			wndHost.ShowWindow(SW_HIDE);

			_wndSettings = m_pLoader->CreateSettingsWindow(pT->m_hWnd, Codec);

			if (_wndSettings.IsWindow())
			{
				// Without it the dialog manager will not tab into the panel.
				_wndSettings.ModifyStyleEx(0, WS_EX_CONTROLPARENT);
				_wndSettings.SetWindowPos(HWND_TOP, rcHost.left, rcHost.top, 0, 0, SWP_NOSIZE | SWP_SHOWWINDOW);
			}
		}

		CWindow wndNone = pT->GetDlgItem(IDC_NO_OPTIONS);

		if (wndNone.IsWindow())
			wndNone.ShowWindow(_wndSettings.IsWindow() ? SW_HIDE : SW_SHOW);
	}

	LRESULT OnInitDialog(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
	{
		T *pT = static_cast<T*>(this);

		_combo = pT->GetDlgItem(IDC_COMBOBOXEX);

		_combo.SetImageList(App.GetGlobalBitmap());
		_nDefaultSelection = 0;

		for (auto pFactory : _loaders.All())
			AddLoader(pFactory);

		_combo.SetCurSel(_nDefaultSelection);
		OnImageLoaderChange();

        bHandled = false;
		return 0;
	}

	void OnChange()
	{
	}

	void OnChangedLoader()
	{
		_nDefaultSelection = _combo.GetCurSel();
		IW::ImageLoaderInfoPtr pNewFactory = (IW::ImageLoaderInfoPtr)_combo.GetItemData(_nDefaultSelection);
		
		// Get ready for a new filter
		if (pNewFactory != m_pLoaderFactory)
		{
			m_pLoaderFactory = pNewFactory;
			m_pLoader = 0;
		}
		
		if (m_pLoader == 0)
		{
			m_pLoader = m_pLoaderFactory->Create();
		}
	}

	bool OnApplyLoader()
	{
		OnChangedLoader();
		return true;
	}
	
	
	// The ini stores the loader key; a built-in default names an extension.
	bool AddLoader(IW::ImageLoaderInfoPtr pFactory)
	{
		if (IW::ImageLoaderFlags::SAVE & pFactory->GetFlags())
		{
			CString str = pFactory->GetExtensionDefault();
			str += _T(" - ");
			str += pFactory->GetTitle();
			
			COMBOBOXEXITEM cbI;
			
			// Each item has text, an lParam with extra data, and an image.
			cbI.mask = CBEIF_TEXT | CBEIF_LPARAM | CBEIF_IMAGE | CBEIF_SELECTEDIMAGE;    
			cbI.pszText = (LPTSTR)(LPCTSTR)str;
			cbI.cchTextMax = str.GetLength();
			cbI.lParam = (LPARAM)pFactory;
			cbI.iItem = -1;          // Add the item to the end of the list.
			cbI.iSelectedImage = cbI.iImage = ImageIndex::Loader;
			
			// Add the item to the combo box drop-down list.
			int i = _combo.InsertItem(&cbI);

			// The ini stores the loader key; a built-in default names an extension.
			if (_tcsicmp(pFactory->GetKey(), _strDefaultSelection) == 0 ||
				_tcsicmp(pFactory->GetExtensionDefault(), _strDefaultSelection) == 0)
			{
				_nDefaultSelection = i;
			}
		}
		
		return true;
	}
	
};

///////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////////////
// CPropertyDlg


template<class TDialog, class TParent>
class CDialogHolder : public TDialog
{
public:

	TParent &_parent;	

	CDialogHolder(TParent &parent) : _parent(parent)
	{
	}

	template<class TInjection>
	CDialogHolder(TParent &parent, TInjection &injectThis) : TDialog(injectThis), _parent(parent)
	{		
	}

BEGIN_MSG_MAP(CDialogHolder)

	COMMAND_CODE_HANDLER(EN_SETFOCUS, OnSetFocus)
	COMMAND_CODE_HANDLER(CBN_SETFOCUS, OnSetFocus)
	COMMAND_CODE_HANDLER(BN_SETFOCUS, OnSetFocus)
	COMMAND_CODE_HANDLER(LBN_SETFOCUS, OnSetFocus)

	CHAIN_MSG_MAP(TDialog)

ALT_MSG_MAP(1)
	
	CHAIN_MSG_MAP_ALT(TDialog, 1)

END_MSG_MAP()

	LRESULT OnSetFocus(WORD /*wNotifyCode*/, WORD /*wID*/, HWND hWndCtl, BOOL& bHandled)
	{
		// The notification code is not evidence: BN_SETFOCUS is CBN_EDITUPDATE and
		// LBN_SETFOCUS is CBN_KILLFOCUS, so typing in a combo, or leaving one,
		// used to scroll the panel. The control that actually holds the focus is.
		if (hWndCtl != nullptr && hWndCtl == ::GetFocus())
			_parent.OnDlgItemFocus(hWndCtl);

		bHandled = FALSE;
		return 0;
	};
};

template<class TDialog>
class CDialogScroll :
	public CWindowImpl< CDialogScroll<TDialog> >,
	public CScrollImpl< CDialogScroll<TDialog> >
{
	typedef CDialogScroll	ThisClass;
	typedef CScrollImpl< CDialogScroll > BaseClass;
	
public:
		
	
	CDialogHolder<TDialog, CDialogScroll> m_dialog;
	CRect _rectClient;
	
	CDialogScroll() : m_dwIcon(IDR_MAINFRAME), m_dialog(*this)
	{		
	}

	template<class TInjection>
	CDialogScroll(TInjection &injectThis) : m_dwIcon(IDR_MAINFRAME), m_dialog(this, injectThis)
	{		
	}

	TDialog &GetDialog() { return m_dialog; };
	
	bool PreTranslateMessage(MSG* pMsg)
	{
		return m_dialog.PreTranslateMessage(pMsg);
	}

	void OnDlgItemFocus(HWND hWndCtl)
	{
		if (hWndCtl == nullptr || !::IsWindow(hWndCtl))
			return;

		CRect rc;
		::GetWindowRect(hWndCtl, &rc);
		ScreenToClient(&rc);
		MakeItemVisible(rc);
	}	
	
	// rc is in this window's client coordinates, so it is already relative to
	// wherever the content currently sits.
	void MakeItemVisible(const CRect &rc)
	{
		CRect rcClient;
		GetClientRect(&rcClient);

		int y = 0;

		// A control taller than the pane can only have its top brought into view.
		if (rc.top < 0 || rc.Height() >= rcClient.Height())
		{
			y = rc.top;
		}
		else if (rc.bottom > rcClient.bottom)
		{
			y = rc.bottom - rcClient.bottom;
		}	
		
		if (y != 0)
		{
			SetScrollOffset(0, m_ptOffset.y + y, TRUE);
		}		
	}
	
    DECLARE_WND_CLASS(_T("CDialogScroll"))

	BEGIN_MSG_MAP(ThisClass)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_PAINT, OnPaint)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBackground)
		
		CHAIN_MSG_MAP(BaseClass)

	ALT_MSG_MAP(1)

		CHAIN_MSG_MAP_ALT_MEMBER(m_dialog, 1)
	END_MSG_MAP()
		
	LRESULT OnSize(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled)
	{
		int cx = LOWORD(lParam);
		int cy = HIWORD(lParam);

		DoSize(cx, cy);

		bHandled = false;
		
		return 0;
	}

	void DoSize()
	{
		CRect rc;
		GetClientRect(&rc);
		DoSize(rc.right - rc.left, rc.bottom - rc.top);
	}

	void DoSize(int cx, int cy)
	{
		
		if (cy)
		{
			CPoint ptOffset = m_ptOffset;
			int nHeight = _rectClient.bottom - _rectClient.top;

			if (nHeight < cy)
			{
				nHeight = cy;
				ptOffset.y = 0;
			}
			else if ((nHeight - ptOffset.y) < cy)
			{
				ptOffset.y = nHeight - cy;
			}
			
			SetScrollSize(1, nHeight);
			SetScrollOffset(ptOffset);  

			// Re-read the width: SetScrollSize is what puts the vertical bar up,
			// and cx was measured before it took its 17 pixels. A panel that lays
			// itself out to the width it is given then ran its right-hand column
			// off the edge.
			CRect rcNow;
			GetClientRect(&rcNow);

			m_dialog.MoveWindow(0,-ptOffset.y, rcNow.Width(), nHeight);
		}
	}
	
	LRESULT OnPaint(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
	{
		CPaintDC dc(m_hWnd);
		return 0;
	}
	
	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
	{
		// The content is a child window, so scrolling has to move it. Without this
		// m_uScrollFlags stays 0 -- no SW_SCROLLCHILDREN and no SW_INVALIDATE --
		// and a scroll blitted the parent's pixels, left every control where it
		// was and let m_ptOffset drift out of step with what was on screen.
		SetScrollExtendedStyle(SCRL_SCROLLCHILDREN);

		HICON hIconSmall = (HICON)::LoadImage(App.GetBitmapResourceInstance(), MAKEINTRESOURCE(m_dwIcon), 
			IMAGE_ICON, ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);
		SetIcon(hIconSmall, FALSE);
		
		m_dialog.Create(m_hWnd);
		m_dialog.ShowWindow(SW_SHOW);
		m_dialog.GetWindowRect(&_rectClient);

		ModifyStyleEx(0,WS_EX_CONTROLPARENT,0);
		m_dialog.ModifyStyleEx(0,WS_EX_CONTROLPARENT,0);


		DoSize();
		
		return 0;
	}
	
	
	LRESULT OnEraseBackground(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
	{
		// handled, no background painting needed
		return 1;
	}
	
protected:
	DWORD m_dwIcon;
};



// Runs one filter off the UI thread. It knows how to call the filter and which
// control to invalidate when a frame is ready, and nothing about the dialog.
class CPreviewThread : public IW::IStatus
{
public :

	typedef std::function<bool (const IW::Image &, IW::Image &, IW::IStatus *)> PreviewFn;

	CPreviewThread() : 
		_hwndPreview(0),
		_hThread(0), 
		_bExit(false), 
		_bScaleIndependent(false),
		_event(FALSE,FALSE),
		_size(100, 100)
	{
	}

	~CPreviewThread() 
	{
		End();
	}

	void SetFilter(PreviewFn fn, bool bScaleIndependent)
	{
		_fn = fn;
		_bScaleIndependent = bScaleIndependent;
	}

	void Progress(int nCurrentStep, int TotalSteps) {  };
	bool QueryCancel() { return _bExit; }; 
	void SetStatusMessage(const CString &strMessage) { }; 
	void SetHighLevelProgress(int nCurrentStep, int TotalSteps) {  };
	void SetHighLevelStatusMessage(const CString &strMessage) {  };
	void SetMessage(const CString &strMessage) {  };
	void SetWarning(const CString &strWarning) {  };
	void SetError(const CString &strError) {  };
	void SetContext(const CString &strContext) {  };

	bool Create(HWND hwndPreview, const IW::Image &image, const CSize &size)
	{
		_hwndPreview = hwndPreview;

		{
			IW::CAutoLockCS lock(_cs);
			_size = size;
			_imagePreview = image;
		}

		Start();
		Refresh();

		return true; 
	};

	void Refresh()
	{
		_event.Set();
	}

	IW::Image GetFilteredImage() const
	{
		IW::CAutoLockCS lock(_cs);
		return _imageFiltered;
	}

	void Start() 
	{
		if (_hThread)
			return;

		// The worker builds CStrings and images, so it needs a CRT thread
		_hThread = (HANDLE)_beginthreadex(NULL, 0, CPreviewThreadProc, this, 0, NULL);
	}

	void End() 
	{
		_bExit = true;

		_event.Set();

		if (_hThread )
		{
			WaitForSingleObject(_hThread,INFINITE);
			CloseHandle(_hThread);
			_hThread = 0;
		}
	}

	void ApplyFilter()
	{
		IW::Image imageSource;
		CSize size;

		{
			// Only the handoff needs the lock, not the scale that follows it
			IW::CAutoLockCS lock(_cs);
			imageSource = _imagePreview;
			size = _size;
		}

		if (!imageSource.IsEmpty() && _fn)
		{
			IW::Image image, imageToFilter;
			bool bSuccess = false;			

			{
				CRect rc = imageSource.GetBoundingRect();

				if (_bScaleIndependent &&
					(rc.Width() > size.cx || rc.Height() > size.cy))
				{			
					IW::ImageStreamScale<IW::CNull> thumbnailStream(imageToFilter, Search::Any, size); 
					IW::IterateImage(imageSource, thumbnailStream, this);
				}
				else
				{
					imageToFilter = imageSource;
				}
			}


			bSuccess = _fn(imageToFilter, image, this);

			if (bSuccess)
			{
				CRect rc = image.GetBoundingRect();

				if ((rc.right - rc.left) > size.cx ||
					(rc.bottom - rc.top) > size.cy)
				{			
					IW::Image imageScaled;
					IW::ImageStreamScale<IW::CNull> thumbnailStream(imageScaled, Search::Any, size); 
					IW::IterateImage(image, thumbnailStream, this);
					image = imageScaled;
				}		

				SetImage(image);
			}
		}

		InvalidatePreview();
	}

	void SetImage(IW::Image &image)
	{
		{
			IW::CAutoLockCS lock(_cs);		
			_imageFiltered = image;	
		}

		// Outside the lock: nothing the UI does with the preview needs _cs held,
		// and the filter pass below runs unlocked for many seconds.
		InvalidatePreview();
	}

protected:

	void InvalidatePreview()
	{
		if (_hwndPreview != 0)
			::InvalidateRect(_hwndPreview, NULL, FALSE);
	}

	HWND _hwndPreview;
	PreviewFn _fn;
	bool _bScaleIndependent;
	IW::Image _imagePreview;
	IW::Image _imageFiltered;
	HANDLE _hThread;
	volatile bool _bExit;
	mutable CCriticalSection _cs;
	CEvent _event;
	CSize _size;

	static unsigned __stdcall CPreviewThreadProc(void *s) 
	{
		CPreviewThread *self = (CPreviewThread *)s;		

		// The filters and loaders this worker runs reach COM
		IW::CCoInit coInit(true);

		while (!self->_bExit)
		{
			// INFINITE, not a timeout: the previous 60s wait discarded its result,
			// so an idle dialog re-ran the whole filter every minute, and a
			// WAIT_FAILED became a full-speed loop.
			if (::WaitForSingleObject(self->_event, INFINITE) != WAIT_OBJECT_0)
				break;

			// A whole filter pass after End() has been called is what the
			// caller is waiting on
			if (self->_bExit)
				break;

			// An exception leaving a thread entry point terminates the process,
			// and this proc had no handler at all. Inside the loop, so one filter
			// that throws does not stop the preview updating ever again.
			try
			{
				self->ApplyFilter();
			}
			catch (const std::exception &e)
			{
				IW::Logging::Error(_T("Preview filter failed: %hs"), e.what());
			}
			catch (...)
			{
				IW::Logging::Error(_T("Preview filter failed"));
			}
		}

		return 0;
	}
};



template<class T>
class CSettingsDialogImpl : 
	public CDialogImpl<T>, 
	public IW::CDialogResizer<T>
{
	typedef CDialogImpl<T> BaseClass;
	typedef CSettingsDialogImpl<T> ThisClass;

public:

	CPreviewThread _thread;
	CSize _sizePreview;
	IW::Image _imagePreview;
	IW::CRender _render;
	bool m_bSetting;
	CPoint _originalSize;

	// The filter is whatever exposes CreatePreview and IsPreviewScaleIndependent;
	// it must outlive the dialog.
	template<class TFilter>
	CSettingsDialogImpl(const IW::Image &imagePreview, TFilter *pFilter) : _imagePreview(imagePreview)
	{ 
		m_bSetting = false;

		_thread.SetFilter(
			[pFilter](const IW::Image &imageIn, IW::Image &imageOut, IW::IStatus *pStatus)
			{
				return pFilter->CreatePreview(imageIn, imageOut, pStatus);
			},
			pFilter->IsPreviewScaleIndependent());
	}

	~CSettingsDialogImpl()
	{ 
		_thread.End();
	}

	BEGIN_MSG_MAP(ThisClass)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		MESSAGE_HANDLER(WM_GETMINMAXINFO, OnGetMinMaxInfo)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		MESSAGE_HANDLER(WM_DRAWITEM, OnDrawItem)
		COMMAND_ID_HANDLER(IDOK, OnCloseCmd)
		COMMAND_ID_HANDLER(IDCANCEL, OnCloseCmd)
		COMMAND_ID_HANDLER(IDC_RESET, OnReset)
		COMMAND_ID_HANDLER(IDHELP, OnHelp)
	END_MSG_MAP()

	LRESULT OnCloseCmd(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled)
	{
		EndDialog(wID);
		return 0;
	}	

	LRESULT OnHelp(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled)
	{
		T *pT = static_cast<T*>(this);
		pT->OnHelp();
		return 0;
	}

	LRESULT OnReset(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled)
	{
		T *pT = static_cast<T*>(this);
		pT->OnReset();
		return 0;
	}

	LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
	{
		bool bWindowPlaced = false;			

		CRect rc;
		GetDlgItem(IDC_PREVIEW).GetWindowRect(rc);
		_sizePreview = rc.Size();

		GetWindowRect(rc);
		_originalSize = rc.Size();
		_thread.Create(GetDlgItem(IDC_PREVIEW), _imagePreview, _sizePreview);

		// Templates
		ResizeAddItem(IDOK, eRight);
		ResizeAddItem(IDCANCEL, eRight);
		if (GetDlgItem(IDC_RESET) != NULL) ResizeAddItem(IDC_RESET, eRight);
		ResizeAddItem(IDHELP, eRight);
		ResizeAddItem(IDC_PREVIEW, eBottom);	
		ResizeAddItem(IDC_PREVIEW_FRAME, eBottom);	

		DoSize();

		if (!bWindowPlaced)
			CenterWindow();		

		return 0;
	}

	LRESULT OnSize(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled)
	{
		DoSize(wParam);
		bHandled = FALSE;
		return 1;
	}

	void DoSize(WPARAM wParam = 0)
	{
		ResizeDialog();
	}

	LRESULT OnGetMinMaxInfo(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& /*bHandled*/)
	{
		LPMINMAXINFO lpMMI = (LPMINMAXINFO)lParam;
		lpMMI->ptMinTrackSize = _originalSize;
		return 0;
	}

	LRESULT OnDestroy(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
	{
		ATLTRACE(_T("Destroy CFilterPropertyDlg\n")); 
		_thread.End();
		return 0;
	}

	LRESULT OnDrawItem(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled)
	{
		DrawItem((LPDRAWITEMSTRUCT)lParam);
		return (LRESULT)TRUE;
	}

	void DrawItem(LPDRAWITEMSTRUCT lpDrawItemStruct)
	{
		// must be implemented
		CDCHandle dc(lpDrawItemStruct->hDC);

		IW::Image image;

		if (IDC_PREVIEW == lpDrawItemStruct->CtlID)
		{
			image = _thread.GetFilteredImage();
		}

		if (!image.IsEmpty())
		{
			CPoint pointCenterWindow;
			pointCenterWindow.x = (lpDrawItemStruct->rcItem.right - lpDrawItemStruct->rcItem.left) / 2;
			pointCenterWindow.y = (lpDrawItemStruct->rcItem.bottom - lpDrawItemStruct->rcItem.top) / 2;

			const IW::Page page = image.GetFirstPage();
			const int x = pointCenterWindow.x - (page.GetWidth() / 2);
			const int y = pointCenterWindow.y - (page.GetHeight() / 2);

			_render.Create(dc, lpDrawItemStruct->rcItem);			
			_render.Fill(IW::Style::Color::Face);
			_render.DrawImage(page, x, y);
			_render.Flip();
		}
	}

	void OnChange()
	{
		_thread.Refresh();
	}
	
	void OnHelp()
	{
	}
	
	void OnReset()
	{
	}
};

}; // namespace IW
