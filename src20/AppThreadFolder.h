// ImageWalker by Zac Walker
//
// Purpose: The two folder workers: one watches the current folder for
//          changes, the other decodes thumbnails and runs searches.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#pragma once

#include "UtilThreads.h"

class WaitForFolderToChangeThread : public IW::Thread
{
private:
	typedef WaitForFolderToChangeThread ThisClass;
	CEvent _eventFolderChange;	

public:

	Coupling *_pCoupling;
	State &_state;

	WaitForFolderToChangeThread(Coupling *pCoupling, State &state) : 
		_pCoupling(pCoupling),
		_state(state),
		_eventFolderChange(FALSE, FALSE)
	{
	}	

	~WaitForFolderToChangeThread()
	{
		StopThread();
	}

	void ResetThread()
	{
		_eventFolderChange.Set();
	}

	void Process()
	{
		App.Log(_T("Wait For Directory To Change thread started"));		

		HANDLE objects[3];

		objects[0] = _eventThreadExit;
		objects[1] = _eventFolderChange;
		objects[2] = INVALID_HANDLE_VALUE;

		int nWaitFailures = 0;

		while(!_bExit)
		{
			DWORD dw;
			if (objects[2] == INVALID_HANDLE_VALUE)
			{
				dw = WaitForMultipleObjects(2, objects, FALSE, INFINITE);
			}
			else
			{
				dw = WaitForMultipleObjects(3, objects, FALSE, INFINITE);
			}

			if (dw != WAIT_FAILED)
				nWaitFailures = 0;

			switch(dw)
			{
			case WAIT_OBJECT_0 + 1:
				{
					if(objects[2] != INVALID_HANDLE_VALUE)
						::FindCloseChangeNotification (objects[2]);

					objects[2] = INVALID_HANDLE_VALUE;

					IW::FolderPtr pFolder = _state.Folder.GetFolder();

					CString strPath = pFolder->GetFolderPath();

					if (!strPath.IsEmpty())
					{
						objects[2] = ::FindFirstChangeNotification (strPath,
							FALSE, FILE_NOTIFY_CHANGE_ATTRIBUTES | FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME);
					}

				}
				break;

			case WAIT_OBJECT_0 + 2:

				_state.Folder.RefreshInOneSecond();

				// Unchecked, a failed re-arm leaves the handle signalled: the
				// wait returns at once every time and the resulting refresh
				// storm starves the decode worker.
				if (!::FindNextChangeNotification (objects[2]))
				{
					IW::Logging::LastError(_T("FindNextChangeNotification"));
					::FindCloseChangeNotification (objects[2]);
					objects[2] = INVALID_HANDLE_VALUE;
				}

				break;

			case WAIT_OBJECT_0:
				_bExit = true;
				break;

			case WAIT_FAILED:
				// objects[2] is closed and reopened on every folder change, so a
				// bad handle here is recoverable - disarm it and carry on. Give
				// up after a few consecutive failures, or a permanently bad
				// handle spins the thread.
				IW::Logging::LastError(_T("WaitForMultipleObjects (folder watch)"));

				if (objects[2] != INVALID_HANDLE_VALUE)
				{
					::FindCloseChangeNotification(objects[2]);
					objects[2] = INVALID_HANDLE_VALUE;
				}

				if (++nWaitFailures > 8)
				{
					IW::Logging::Error(_T("Folder watch gave up after repeated wait failures"));
					_bExit = true;
				}
				break;
			}
		}

		if(objects[2] != INVALID_HANDLE_VALUE)
		{
			::FindCloseChangeNotification (objects[2]);
		}
	}	

};



class DecodeThumbsThread : public IW::Thread
{
private:
	typedef DecodeThumbsThread ThisClass;

	// Answers QueryCancel for the thumbnail loaders, which otherwise get
	// CNullStatus and can never be interrupted
	class CThumbStopper : public IW::CNullStatus
	{
	public:
		volatile bool &_bExit;
		std::atomic_bool &_bAbort;

		CThumbStopper(volatile bool &bExit, std::atomic_bool &bAbort) : _bExit(bExit), _bAbort(bAbort) {};

		bool QueryCancel() { return _bExit || _bAbort; }
	};

	CEvent _eventFolderChange;
	mutable CCriticalSection _csSearch;
	Search::Spec _searchSpec;
	Search::Type _searchType;
	IW::FolderPtr _pendingSearchFolder;
	UINT_PTR _searchGeneration = 0;

	int _timeLastStatus;
	std::atomic_bool _bAbort;

	void FinishSearch(UINT_PTR generation)
	{
		IW::CAutoLockCS lock(_csSearch);
		if (generation == _searchGeneration)
		{
			_state.Folder.IsSearching = false;
			_pCoupling->SignalSearchingComplete(generation);
		}
	}

public:

	Coupling *_pCoupling;
	State &_state;

	enum { statusUpdateTicks = 500 };

	DecodeThumbsThread(Coupling *pCoupling, State &state) : 
		_pCoupling(pCoupling),
		_state(state),
		_eventFolderChange(FALSE, FALSE),
		_searchType(Search::Current),
		_bAbort(false),
		_timeLastStatus(0)
	{
	}	

	~DecodeThumbsThread()
	{
		StopThread();
	}

	void ResetThread()
	{
		_eventFolderChange.Set();
	}

	bool Abort()
	{
		IW::CAutoLockCS lock(_csSearch);
		_bAbort = true;
		_pendingSearchFolder = nullptr;
		if (!_state.Folder.IsSearchMode.exchange(false))
			return false;

		_state.Folder.IsSearching = false;
		++_searchGeneration;
		return true;
	}	

	UINT_PTR StartSearch(Search::Type type, const Search::Spec &ss)
	{
		const auto folder = _state.Folder.GetFolder();
		UINT_PTR generation;
		{
			IW::CAutoLockCS lock(_csSearch);
			_searchType = type;
			_searchSpec = ss;
			_pendingSearchFolder = folder;
			generation = ++_searchGeneration;
			_state.Folder.IsSearchMode = true;
			_state.Folder.IsSearching = true;
		}
		_eventFolderChange.Set();
		return generation;
	}

	bool IsCurrentSearch(UINT_PTR generation) const
	{
		IW::CAutoLockCS lock(_csSearch);
		return generation != 0 && generation == _searchGeneration;
	}

	bool PublishSearchResult(UINT_PTR generation, IW::Folder *pFolder, IW::FolderItem *pThumb)
	{
		// Abort and publication must share a lock. A new search clears the
		// same folder immediately after Abort, not after the decode returns.
		IW::CAutoLockCS lock(_csSearch);
		if (generation != _searchGeneration || !_state.Folder.IsSearchMode || _bExit)
			return false;

		pThumb->ModifyFlags(0, THUMB_IS_SEARCH_RESULT | THUMB_INVALIDATE);
		pFolder->InsertThumb(pThumb);
		return true;
	}

	void Process()
	{		
		App.Log(_T("Decode Thumbs thread started"));	

		CLoadAny loader(_state.Loaders);
		CThumbStopper stopper(_bExit, _bAbort);

		DWORD dw;
		HANDLE objectsRefresh[2];
		int nWaitFailures = 0;

		objectsRefresh[0] = _eventFolderChange;
		objectsRefresh[1] = _eventThreadExit;

		while(!_bExit)
		{
			UINT_PTR generation = 0;
			// One failed pass must not retire the worker: the catch is inside the
			// loop, not around it. Outside, a single unreadable file ends background
			// thumbnailing for the rest of the session while ResetThread keeps
			// signalling an event nobody is waiting on.
			try
			{
			// Cleared before the folder is captured, not after: GetFolder blocks
			// on FolderState::_cs, which is the lock StopLoading holds while it
			// raises the abort -- clearing afterwards erases it and the pass runs
			// the folder the user has already navigated away from.
			_bAbort = false;
			IW::FolderPtr pFolder = _state.Folder.GetFolder();

			Search::Type searchType = Search::Current;
			Search::Spec searchSpec;
			bool bSearchMode;

			{
				IW::CAutoLockCS lock(_csSearch);
				bSearchMode = _state.Folder.IsSearchMode;
				if (_pendingSearchFolder)
				{
					pFolder = _pendingSearchFolder;
					_pendingSearchFolder = nullptr;
					generation = _searchGeneration;
					searchType = _searchType;
					searchSpec = _searchSpec;
				}
			}

			// What shall we load?
			if (generation)
			{
				_pCoupling->SignalSearching(generation);

				if (searchType == Search::Current)
				{
					DecodeFolder(generation, pFolder, &loader, searchSpec, pFolder->GetFolderItem(), pFolder->GetShellFolder());
				}
				else if (searchType == Search::MyPictures)
				{
					try
					{
						IW::CShellItem item;
						if (item.Open(nullptr, CSIDL_MYPICTURES))
						{
							IW::CShellFolder pMyPicsFolder;
							if (SUCCEEDED(pMyPicsFolder.Open(item)))
							{
								DecodeFolder(generation, pFolder, &loader, searchSpec, item, pMyPicsFolder);
							}
						}
					}
					catch (std::exception &)
					{
					}
				}

				FinishSearch(generation);
			}
			else if (!bSearchMode)
			{
				IW::ScopeLockedBool isThumbnailing(_state.Folder.IsThumbnailing);
				_pCoupling->SignalThumbnailing();

				while(!_bAbort && !_bExit)
				{
					IW::FolderItemPtr pItem = pFolder->GetNextThumbToLoad(pFolder);

					if (pItem == 0)
						break;

					// Really load
					IW::FolderItemLoader job;
					job.SetStatus(&stopper);

					// One unreadable file must not end the whole pass
					try
					{
						if (pFolder->LoadJobBegin(job, pItem))
						{
							job.LoadImage(&loader, Search::Any);
							job.RenderAndScale();						
						}
					}
					catch (const std::exception &e)
					{
						IW::Logging::Error(_T("Failed to thumbnail %s: %hs"), (LPCTSTR)job.GetFilePath(), e.what());
					}
					catch (...)
					{
						IW::Logging::Error(_T("Failed to thumbnail %s"), (LPCTSTR)job.GetFilePath());
					}

					pFolder->LoadJobEnd(job, pItem);

					int t = GetTickCount();

					// May fire status update if it is time
					// 2 times a second?
					if (_timeLastStatus < t)
					{
						_timeLastStatus = t + statusUpdateTicks;
						_pCoupling->SignalThumbnailing();
					}
				}

				_pCoupling->SignalThumbnailingComplete();				
			}
			}
			catch (const std::exception &e)
			{
				IW::Logging::Error(_T("Exception escaped a thumbnail pass: %hs"), e.what());
				if (generation) FinishSearch(generation);
				else _pCoupling->SignalThumbnailingComplete();
			}
			catch (...)
			{
				IW::Logging::Error(_T("Unknown exception escaped a thumbnail pass"));
				if (generation) FinishSearch(generation);
				else _pCoupling->SignalThumbnailingComplete();
			}

			// No more images?
			dw = WaitForMultipleObjects(2, objectsRefresh, FALSE, INFINITE);

			if (dw == WAIT_FAILED)
			{
				// Not a graceful exit. But the handles are fixed for the life of
				// this object, so a failure here is almost always permanent -
				// retrying without a bound would re-run a whole pass, recursive
				// search included, ten times a second forever.
				IW::Logging::LastError(_T("WaitForMultipleObjects(folder load)"));

				if (++nWaitFailures > 10)
					return;

				Sleep(100);
				continue;
			}

			nWaitFailures = 0;

			if (dw != WAIT_OBJECT_0)
			{
				return;
			}
		}
	}	

	// A junction cycle under the search root is otherwise unbounded, and this
	// runs on a worker with a default stack and several shell objects per frame.
	enum { kMaxSearchDepth = 32 };

	bool DecodeFolder(UINT_PTR generation, IW::Folder *pFolder, CLoadAny *pLoader, const Search::Spec &spec, const IW::CShellItem &itemFolder, IW::CShellFolder &pShellFolder, LPCITEMIDLIST pItemGap = 0, int nDepth = 0)
	{
		if (nDepth > kMaxSearchDepth || _bExit || !IsCurrentSearch(generation))
			return true;

		CString strPath = IW::CShellDesktop().GetDisplayNameOf(itemFolder, SHGDN_FORPARSING);
		_pCoupling->SignalSearchingFolder(strPath, generation);

		// Get the IEnumIDList object for the given folder.
		IW::CShellItemEnum enumItems;

		UINT dwFlags = SHCONTF_FOLDERS | SHCONTF_NONFOLDERS;
		if (App.Settings.m_bShowHidden) dwFlags |= SHCONTF_INCLUDEHIDDEN;

		// A worker must not give the shell the UI thread's window to put dialogs on
		HRESULT hr = enumItems.Create(nullptr, pShellFolder, dwFlags);

		if (SUCCEEDED(hr))
		{
			// Build up item map
			// Enumerate through the list of items.
			LPITEMIDLIST pItem = NULL;
			ULONG ulFetched = 0;

			while (!_bExit && IsCurrentSearch(generation) && enumItems && enumItems->Next(1, &pItem, &ulFetched) == S_OK)
			{
				IW::FolderItemPtr pThumb = new IW::RefObj<IW::FolderItem>;

				if (!pThumb->Init(pShellFolder, pItem, pItemGap))
					return false;

				if (spec.DoMatch(pThumb, pLoader))
				{
					if (!PublishSearchResult(generation, pFolder, pThumb))
						return true;

					int t = GetTickCount();

					// May fire status update if it is time
					// 2 times a second?
					if (_timeLastStatus < t)
					{
						_timeLastStatus = t + statusUpdateTicks;
						_pCoupling->SignalSearching(generation);
					}
				}			
			}
		}


		// Get the IEnumIDList object for the given folder.
		if (_bExit || !IsCurrentSearch(generation))
			return true;

		IW::CShellItemEnum enumFolders;

		dwFlags = SHCONTF_FOLDERS;
		if (App.Settings.m_bShowHidden) dwFlags |= SHCONTF_INCLUDEHIDDEN;

		hr = enumFolders.Create(nullptr, pShellFolder, dwFlags);

		if (SUCCEEDED(hr))
		{
			// Build up item map
			// Enumerate through the list of items.
			LPITEMIDLIST pItem = NULL;
			ULONG ulFetched = 0;

			while (!_bExit && IsCurrentSearch(generation) && enumFolders && enumFolders->Next(1, &pItem, &ulFetched) == S_OK)
			{
				IW::CShellItem item;
				item.Attach(pItem);

				IW::CShellFolder pSubFolder;
				hr = pShellFolder->BindToObject(pItem, NULL, IID_IShellFolder, (LPVOID*)pSubFolder.GetPtr());

				if (SUCCEEDED(hr))
				{
					IW::CShellItem itemSubFolder;
					itemSubFolder.Cat(itemFolder, pItem);

					
					if (pItemGap)
					{
						IW::CShellItem itemGap;
						itemGap.Cat(pItemGap, item);
						DecodeFolder(generation, pFolder, pLoader, spec, itemSubFolder, pSubFolder, itemGap, nDepth + 1);
					}
					else
					{
						DecodeFolder(generation, pFolder, pLoader, spec, itemSubFolder, pSubFolder, item, nDepth + 1);
					}
				}
			}
		}

		return true;	
	}

};

