// ImageWalker by Zac Walker
//
// Purpose: IW::Thread: worker lifetime - start, signal, join - with COM
//          initialised and exceptions caught at the entry point.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#pragma once

#include "iw/logfile.h"

namespace IW
{
	class Thread
	{
	private:
		Thread(const Thread&);
		void operator=(const Thread&);


	protected:
		typedef Thread ThisClass;

		HANDLE _hThread;	
		volatile bool _bExit;
		int _nPriority;

		// Per instance, so shutting one worker down cannot exit every other
		CEvent _eventThreadExit;

	public:

		Thread(int nPriority = THREAD_PRIORITY_LOWEST) : 
		  _hThread(nullptr), 
			  _bExit(false), 
			  _nPriority(nPriority),
			  _eventThreadExit(TRUE, FALSE)
		  {
		  }

		  // Only a backstop, the join has to happen in the most derived
		  // destructor, before its own members go away
		  virtual ~Thread()
		  {
			  StopThread();
		  }

		  void StartThread()
		  {
			  if (_hThread != nullptr)
				  return;

			  // Cleared here rather than in the constructor so a Stop/Start pair
			  // gives a live worker. Left set, the new thread exits on its first
			  // test and then silently swallows every wake.
			  _bExit = false;
			  _eventThreadExit.Reset();

			  _hThread = (HANDLE)_beginthreadex(nullptr, 0, Start, static_cast<ThisClass*>(this), 0, nullptr);

			  if (_hThread == nullptr)
			  {
				  // ATLASSERT alone is compiled out of release, and the app then
				  // runs with no background loader and no indication of it
				  IW::Logging::Error(_T("Failed to start a worker thread"));
				  return;
			  }

			  SetThreadPriority(_hThread, _nPriority);
		  }

		  // Ask the worker to wind down, without waiting for it
		  virtual void SignalExit()
		  {
			  _bExit = true;
			  _eventThreadExit.Set();
		  }

		  void StopThread()
		  {
			  SignalExit();

			  if (_hThread != nullptr)
			  {
				  // The worker is still using this object, so the wait cannot give up
				  WaitForSingleObject(_hThread, INFINITE);
				  CloseHandle(_hThread);

				  _hThread = nullptr;
			  }
		  }

		  static unsigned __stdcall Start(void *lParam)
		  {
			  IW::CCoInit coInit(true);

			  try
			  {
				  reinterpret_cast<ThisClass*>(lParam)->Process();
			  }
			  catch (const std::exception &e)
			  {
				  IW::Logging::Error(_T("Exception escaped a worker thread: %hs"), e.what());
			  }
			  catch (...)
			  {
				  IW::Logging::Error(_T("Unknown exception escaped a worker thread"));
			  }

			  return 0;
		  }

		  virtual void Process() = 0;
	};
}
