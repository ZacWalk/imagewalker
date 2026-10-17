// ImageWalker by Zac Walker
//
// Purpose: The viewer decode thread - takes the most recent request, decodes
//          it, and posts the result back to the frame.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#pragma once

#include "UtilThreads.h"
#include "FileFormatAny.h"

// Answers QueryCancel for a loader; everything else about IStatus is a no-op,
// which is what CNullStatus already is.
class CStopper : public IW::CNullStatus
{
public:
	volatile bool &m_bStopper;
	CStopper(volatile bool &bStopper) : m_bStopper(bStopper) {};

	bool QueryCancel() { return m_bStopper; }
};


// Above the thumbnail workers, below the UI, because the viewer shows a
// placeholder until this one lands.
class ImageLoaderThread : public IW::Thread
{
protected:

	CEvent _eventRefresh;
	CCriticalSection _cs;

	volatile bool _bStopLoading;

	IW::RefPtr<CImageLoad> _pInfo;

public:

	typedef ImageLoaderThread ThisClass;

	Coupling *_pCoupling;
	ImageLoaders &_loaders;
	State &_state;

	ImageLoaderThread(ImageLoaders &loaders, Coupling *pCoupling, State &state) : 
		IW::Thread(THREAD_PRIORITY_BELOW_NORMAL),
		_pCoupling(pCoupling),
		_eventRefresh(FALSE, FALSE),
		_bStopLoading(false),
		_loaders(loaders),
		_state(state)
	{
	}

	~ImageLoaderThread()
	{
		StopThread();
	}

	void SignalExit() override
	{
		_bStopLoading = true;
		IW::Thread::SignalExit();
	}

	void SetStopLoading()
	{
		_bStopLoading = true;
	}

	void ShowImage(CImageLoad *pInfo)
	{
		{
			IW::CAutoLockCS lock(_cs);

			SetStopLoading();

			_pInfo = pInfo;
			_eventRefresh.Set();
		}

		// Outside the lock: the status bar is owner-drawn and the worker's
		// PopInfo would queue behind the paint.
		CString str;
		str.Format(IDS_FMT_LOADING, IW::Path::FindFileName(pInfo->_path));
		_pCoupling->SetStatusText(str);
	}

	IW::RefPtr<CImageLoad> PopInfo()
	{
		IW::CAutoLockCS lock(_cs);
		IW::RefPtr<CImageLoad> pInfo = _pInfo;
		_pInfo = 0;
		// One invariant with _pInfo, so it is cleared under the same lock
		_bStopLoading = false;
		return pInfo;
	}

	void Process()
	{
		App.Log(_T("Decode And Scale thread started"));

		CLoadAny loader(_loaders);
		IW::CShellDesktop desktop;

		HANDLE objects[2];	
		objects[0] = _eventRefresh;
		objects[1] = _eventThreadExit;

		CStopper stopperLoading(_bStopLoading);	

		int nWaitFailures = 0;

		while(!_bExit)
		{
			DWORD dw = WaitForMultipleObjects(2, objects, FALSE, INFINITE);		

			if (dw != WAIT_FAILED)
				nWaitFailures = 0;

			if (dw == (WAIT_OBJECT_0 + 0)) // load
			{
				if (_bExit)
					return;

				// _eventRefresh is auto-reset, so the wait has already cleared
				// it. An explicit Reset here would discard a signal that
				// arrived between the two.
				IW::RefPtr<CImageLoad> pInfo = PopInfo();

				if (pInfo)
				{				
					IW::ScopeLockedBool isSearching(_state.Image.IsLoading);

					// libjpeg and libpng report a malformed file by throwing out of
					// their error callback. Catching that outside this loop retires
					// the decode thread, and every later ShowImage then signals
					// _eventRefresh with nobody waiting on it - the viewer stays on
					// the previous image for the rest of the session.
					try
					{
						pInfo->Load(loader, &stopperLoading);
					}
					catch (const std::exception &e)
					{
						IW::Logging::Error(_T("Failed to load %s: %hs"), (LPCTSTR)pInfo->_path, e.what());
					}
					catch (...)
					{
						IW::Logging::Error(_T("Failed to load %s"), (LPCTSTR)pInfo->_path);
					}

					ATLTRACE(_T("Load of %s complete with StopLoading=%d\n"), static_cast<LPCTSTR>(pInfo->_path), _bStopLoading);

					// Posted even when the decode failed: the completion is what
					// clears the UI's pending state and drives the next request.
					pInfo->_bWasStopped = _bStopLoading;
					_pCoupling->SignalImageLoadComplete(pInfo.Detach());
				}
			}
			else if (dw == WAIT_FAILED)
			{
				// The handles are fixed for the life of this object, so a failure
				// here is almost always permanent. Retry a few consecutive times
				// for a transient one, then give up rather than spin.
				IW::Logging::LastError(_T("WaitForMultipleObjects(image loader)"));

				if (++nWaitFailures > 10)
					return;

				Sleep(100);
			}
			else
			{
				return;
			}
		}
	}
};
