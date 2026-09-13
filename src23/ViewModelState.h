// ImageWalker by Zac Walker
//
// Purpose: State: the one object the frame, the views and the workers all
//          share - folder, image, favourites and history.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#pragma once

#include "UtilDelegate.h"
#include "ViewModelImage.h"
#include "ViewModelFolder.h"
#include "FileFormatList.h"
#include "ViewShellMenu.h"

#define ID_HISTORY_BACK_FIRST              5000
#define ID_HISTORY_BACK_LAST               5099

#define ID_HISTORY_FORWARD_FIRST            5100
#define ID_HISTORY_FORWARD_LAST             5199

#define ID_COPYTO_FIRST            5200
#define ID_COPYTO_LAST             5299

#define ID_MOVETO_FIRST            5300
#define ID_MOVETO_LAST             5399

#define ID_GOTO_FIRST            5400
#define ID_GOTO_LAST             5499


class FavouriteState
{
public:

	enum { MaxSize = 8 };
	IW::ShellItemList _items;
	Coupling *_pCoupling;

	Delegate::List0 ChangedDelegates;

	FavouriteState(Coupling *pCoupling) : _pCoupling(pCoupling)
	{
	}

	void DefaultFavorites()
	{
		// Default the copy to list
		DWORD dwLocations [ ] = { CSIDL_MYPICTURES, CSIDL_DESKTOP, CSIDL_DRIVES, CSIDL_PERSONAL };

		for(int i = 0; i < (sizeof(dwLocations)/sizeof(DWORD)); i++)
		{
			try
			{
				IW::CShellItem item;
				if (item.Open(IW::GetMainWindow(), dwLocations[i]))
				{
					Add(item);
				}
			}
			catch (std::exception &)
			{
			}
		}
	}

	void Add(IW::CShellItem &item)
	{
		for(int i = 0; i < _items.GetSize(); i++)
		{
			if (_items[i] == item)
			{
				_items.RemoveAt(i);
				break;
			}		
		}

		if (_items.GetSize() > MaxSize)
		{
			_items.RemoveAt(0);
		}

		_items.Add(item);
		ChangedDelegates.Invoke();
	}

	void RemoveAll()
	{
		_items.RemoveAll();
		ChangedDelegates.Invoke();
	}

	bool IsEmpty() const
	{
		return _items.GetSize() == 0;				
	}

	IW::CShellItem GetTop() const
	{
		return _items[_items.GetSize() - 1];
	}

	void Load()
	{
		_items.RemoveAll();

		for (const auto &strPath : App.Settings.Favourites)
		{
			IW::CShellItem item;

			if (item.Open(strPath))
				_items.Add(item);
		}

		if (_items.GetSize() == 0)
			DefaultFavorites();
	}

	void Save()
	{
		App.Settings.Favourites.clear();

		for (int i = 0; i < _items.GetSize(); i++)
			App.Settings.Favourites.push_back(IW::CShellDesktop().GetDisplayNameOf(_items[i], SHGDN_FORPARSING));
	}

	bool UseItem(int nItem, IW::CShellItem &itemOut)
	{
		if (_items.GetSize() > nItem && nItem >= 0)
		{
			itemOut = _items[nItem];

			_items.RemoveAt(nItem);
			_items.Add(itemOut);
			ChangedDelegates.Invoke();

			return true;
		}

		return false;
	}

	void GetCopyToMenu(IW::CShellMenu &cmdbar)
	{
		for(int i = _items.GetSize() - 1; i >= 0; i--)
			cmdbar.AddItem(ID_COPYTO_FIRST + i, _items[i], ID_COPYTO_PRE);

		cmdbar.AddSeparator();
		cmdbar.AddItem(ID_COPYTO_NEWLOCATION, IDS_MOVECOPY_NEWLOCATION);
		cmdbar.AddItem(ID_MOVECOPY_CLEARLOCATIONS, IDS_MOVECOPY_CLEARLOCATIONS);
	}

	void GetGotoMenu(IW::CShellMenu &cmdbar)
	{
		for(int i = _items.GetSize() - 1; i >= 0; i--)
			cmdbar.AddItem(ID_GOTO_FIRST + i, _items[i], ID_GOTO_PRE);

		cmdbar.AddSeparator();
		cmdbar.AddItem(ID_GOTO_NEWLOCATION, IDS_GOTO_NEWLOCATION);
		cmdbar.AddItem(ID_MOVECOPY_CLEARLOCATIONS, IDS_MOVECOPY_CLEARLOCATIONS);
		cmdbar.AddItem(ID_GOTO_ADDCURRENT, IDS_GOTO_ADDCURRENT);
	}

	void  GetMoveToMenu(IW::CShellMenu &cmdbar)
	{
		for(int i = _items.GetSize() - 1; i >= 0; i--)
			cmdbar.AddItem(ID_MOVETO_FIRST + i, _items[i], ID_MOVETO_PRE);

		cmdbar.AddSeparator();
		cmdbar.AddItem(ID_MOVETO_NEWLOCATION, IDS_MOVECOPY_NEWLOCATION);
		cmdbar.AddItem(ID_MOVECOPY_CLEARLOCATIONS, IDS_MOVECOPY_CLEARLOCATIONS);
	}

	void OnMoveTo(long nItem, const CString &strFileList)
	{
		IW::CShellItem item;

		if (UseItem(nItem, item))
		{
			Copy(strFileList, item, true);
		}
	}

	void OnCopyTo(long nItem, const CString &strFileList)
	{
		IW::CShellItem item;

		if (UseItem(nItem, item))
		{
			Copy(strFileList, item, false);			
		}
	}

	void Copy(const CString &strFileList, IW::CShellItem &item, bool bMove)
	{
		CString strPath;

		if (item.GetPath(strPath))
		{
			Copy(strFileList, strPath, bMove);
		}
	}

	void Copy(const CString &strFileList, const CString &strDestination, bool bMove)
	{
		IW::Focus preserveFocus;		
		IW::FileOperation operation(strFileList);

		if (operation.Copy(strDestination, bMove))
		{
			_pCoupling->AfterCopy(bMove);
		}
		else if (!operation.WasAborted())
		{
			IW::CMessageBoxIndirect mb;
			mb.Show(IDS_FAILEDTOCOPYORMOVE);
		}
	}	
	
};

class HistoryState
{
public:
	IW::ShellItemList m_history;
	int m_iHistoryPos;	

	
	HistoryState() : m_iHistoryPos(-1)
	{
	}

	bool CanBrowseForward() const
	{
		return m_iHistoryPos < m_history.GetSize() - 1; 
	};

	bool CanBrowseBack() const
	{
		return m_iHistoryPos > 0; 
	};

	bool SetHistoryPos(int n, IW::CShellItem &itemOut)
	{
		int i = m_iHistoryPos + n;

		if (i >= m_history.GetSize() || i < 0)
		{
			MessageBeep(MB_OK);
			return false;
		}

		m_iHistoryPos = i;
		itemOut = m_history[i];
		return true;
	}

	void HistoryAdd(const IW::CShellItem &item)
	{
		if (m_iHistoryPos != -1)
		{
			if (m_history[m_iHistoryPos] == item)
				return;

			// Erase tail objects if need be
			while (m_iHistoryPos != m_history.GetSize() - 1)
			{
				m_history.RemoveAt(m_history.GetSize() - 1);
			}
		}

		m_history.Add(item);

		if (m_history.GetSize() > 10)
			m_history.RemoveAt(0);

		m_iHistoryPos = m_history.GetSize() - 1;
	};

	void GetBrowseBackMenu(IW::CShellMenu &cmdbar)
	{
		int nCount = 1;
		int i = m_iHistoryPos - 1;

		for(; 0 <= i; --i)
		{
			cmdbar.AddItem(ID_HISTORY_BACK_FIRST + nCount, m_history[i]);
			nCount++;
		}
	}


	void GetBrowseForwardMenu(IW::CShellMenu &cmdbar)
	{
		int nCount = 1;
		int i = m_iHistoryPos + 1;
		int iEnd = m_history.GetSize();

		for(; iEnd > i; ++i)
		{
			cmdbar.AddItem(ID_HISTORY_FORWARD_FIRST + nCount, m_history[i]);
			nCount++;
		}
	}
};

class State
{
private:
	State(const State &other);
	void operator=(const State &other);

public:

	Delegate::List0 ResetFrames;

	template<class TCoupling>
	State(TCoupling *pCoupling, bool initializeFolder = true) :
		Image(Loaders, pCoupling, pCoupling),
		Folder(pCoupling, initializeFolder),
		Favourite(pCoupling)
	{
	}

	void Load()
	{
		Folder.Load();
		Favourite.Load();
	}

	void Save()
	{
		Folder.Save();
		Favourite.Save();
	}

	void ContextSwitch(Delegate::List0 &list);	

	ImageState Image;	
	FolderState Folder;
	HistoryState History;
	FavouriteState Favourite;
	ImageLoaders Loaders;
};
