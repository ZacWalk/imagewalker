#pragma once

// Handing work between threads without putting a raw pointer in a message
// parameter.
//
// The releases all used the same trick: allocate a job, PostThreadMessage or
// PostMessage its address, and have the far end cast it back and delete it.
// Nothing owns the job in between, so a queue that is never drained leaks it, a
// window destroyed at the wrong moment leaks it or frees it twice, and a worker
// that answers with SendMessage deadlocks against a UI thread that is waiting
// for the worker to finish.
//
// CWorkQueue is a mutex-guarded queue of std::shared_ptr, so the item is owned
// the whole way across. It carries a manual-reset event that stays signalled
// while there is something to take, which is the part that matters for the UI
// side: a HANDLE can go into MsgWaitForMultipleObjectsEx next to the thread's
// own input queue, so a finished job wakes the message loop directly instead of
// having to travel as a window message.

#include <windows.h>
#include <objbase.h>

#include <deque>
#include <memory>
#include <mutex>

#include "iw/crashreport.h"

namespace IW
{
	// A closable queue of shared_ptr<T> with a waitable "not empty" event.
	//
	// Once closed the queue drops its contents, refuses further pushes and
	// leaves the event signalled, so every consumer wakes and keeps waking.
	// That is how a worker is asked to stop: there is no separate exit flag to
	// get out of step with the queue.
	template <class T>
	class CWorkQueue
	{
	public:
		typedef std::shared_ptr<T> Item;

		CWorkQueue() : _ready(::CreateEvent(nullptr, TRUE, FALSE, nullptr)), _closed(false)
		{
		}

		~CWorkQueue()
		{
			Close();

			if (_ready != nullptr)
				::CloseHandle(_ready);
		}

		CWorkQueue(const CWorkQueue &) = delete;
		CWorkQueue &operator=(const CWorkQueue &) = delete;

		// Manual-reset, signalled while Pop would return something (or the
		// queue is closed). Valid for the lifetime of the queue.
		//
		// A closed queue leaves this signalled for good, so nothing may still be
		// waiting on it at that point or the waiter spins.
		HANDLE Handle() const { return _ready; }

		// False when the event could not be created, which makes the queue unable
		// to block or to wake anyone.
		bool IsValid() const { return _ready != nullptr; }

		bool Push(Item item)
		{
			if (!item)
				return false;

			// The event is touched only under the lock, so its state is always a
			// function of the queue's. Signalling outside left a window where the
			// two disagreed in both directions.
			std::lock_guard<std::mutex> lock(_mutex);

			if (_closed)
				return false;

			_items.push_back(std::move(item));
			::SetEvent(_ready);
			return true;
		}

		// Returns an empty pointer when there is nothing queued. Never blocks.
		Item Pop()
		{
			std::lock_guard<std::mutex> lock(_mutex);

			if (_items.empty())
			{
				if (!_closed)
					::ResetEvent(_ready);

				return Item();
			}

			Item item = std::move(_items.front());
			_items.pop_front();

			if (_items.empty() && !_closed)
				::ResetEvent(_ready);

			return item;
		}

		// Blocks until an item arrives or the queue is closed, in which case the
		// result is empty. A worker loop is "while (Item i = q.Take())".
		Item Take()
		{
			for (;;)
			{
				Item item = Pop();

				if (item || IsClosed())
					return item;

				// An invalid event is not waited on at all, so retrying here
				// would spin the worker at 100%. Closing makes IsStopping() tell
				// the truth instead of leaving a dead worker looking live.
				if (::WaitForSingleObject(_ready, INFINITE) == WAIT_FAILED)
				{
					Close();
					return Item();
				}
			}
		}

		void Close()
		{
			// Swapped out under the lock and destroyed outside it: _mutex is not
			// recursive, and a ~T that touches the queue would deadlock on it.
			std::deque<Item> doomed;

			{
				std::lock_guard<std::mutex> lock(_mutex);

				if (_closed)
					return;

				_closed = true;
				doomed.swap(_items);
				::SetEvent(_ready);
			}
		}

		bool IsClosed() const
		{
			std::lock_guard<std::mutex> lock(_mutex);
			return _closed;
		}

		size_t Size() const
		{
			std::lock_guard<std::mutex> lock(_mutex);
			return _items.size();
		}

	private:
		mutable std::mutex _mutex;
		std::deque<Item> _items;
		HANDLE _ready;
		bool _closed;
	};

	// One unit of background work. Work() runs on the worker; Complete() runs on
	// whichever thread drains the dispatch queue, which for these apps is always
	// the thread that owns the windows.
	class CWorkItem
	{
	public:
		virtual ~CWorkItem() = default;

		virtual void Work() = 0;
		virtual void Complete() {}

		// The owner keeps the token alive. A job whose owner has gone is still
		// drained, so it is destroyed on the UI thread like any other, but it is
		// not completed.
		void SetOwner(const std::shared_ptr<void> &owner)
		{
			_owner = owner;
			_hasOwner = true;
		}

		bool OwnerIsAlive() const { return !_hasOwner || !_owner.expired(); }

	private:
		std::weak_ptr<void> _owner;
		bool _hasOwner = false;
	};

	class CDispatchQueue : public CWorkQueue<CWorkItem>
	{
	public:
		// Runs everything queued when it was called. Items whose owner has been
		// destroyed are dropped rather than completed.
		void DrainAll()
		{
			// Bounded rather than "until empty": a Complete() that posts a follow-up
			// onto this same queue would otherwise starve the message loop.
			for (size_t remaining = Size(); remaining != 0; --remaining)
			{
				try
				{
					// Inside the try so that ~CWorkItem is covered too.
					Item item = Pop();

					if (!item)
						break;

					if (item->OwnerIsAlive())
						item->Complete();
				}
				catch (...)
				{
					// An escaping exception would unwind out of the message loop.
				}
			}
		}
	};

	// The queue that background work comes back on. It belongs to the UI thread:
	// the message loop waits on Handle() and calls DrainAll().
	inline CDispatchQueue &UiQueue()
	{
		static CDispatchQueue queue;
		return queue;
	}

	// Called from main() beside Logging::Close(). Closing the queue makes a
	// late Push from a worker that outlived its owner a no-op rather than the
	// first sign of it, which otherwise lands in static destruction.
	//
	// Must run after the last message loop has returned and after every worker
	// has been joined: a loop still waiting on UiQueue().Handle() spins once the
	// queue is closed, and a worker handing back after it dies on the worker.
	inline void ShutdownWorkQueues()
	{
		UiQueue().Close();
	}

	// A thread that runs CWorkItems from its own queue until the queue closes,
	// posting each one to the UI queue when Work() returns.
	//
	// Stop() only closes the queue and waits; it never sends a message to the UI
	// thread and never needs the UI thread to pump, so it is safe to call from a
	// destructor on the UI thread.
	class CWorkerThread
	{
	public:
		CWorkerThread() : _thread(nullptr)
		{
		}

		~CWorkerThread() { Stop(); }

		CWorkerThread(const CWorkerThread &) = delete;
		CWorkerThread &operator=(const CWorkerThread &) = delete;

		bool Start()
		{
			if (_thread != nullptr)
				return true;

			// Single use: the close is irreversible, so a worker started again
			// after Stop() would exit on its first Take() and report success.
			if (!_queue.IsValid() || _queue.IsClosed())
				return false;

			_thread = ::CreateThread(nullptr, 0, Proc, this, 0, nullptr);
			return _thread != nullptr;
		}

		bool Post(const std::shared_ptr<CWorkItem> &item) { return _queue.Push(item); }

		bool IsStopping() const { return _queue.IsClosed(); }

		// Signalled while the worker is stopping or another job is waiting. A
		// Work() that blocks - a directory watch, say - has to wait on this too,
		// or Stop() never returns and a newly posted job never starts.
		HANDLE WakeHandle() const { return _queue.Handle(); }

		void Stop()
		{
			_queue.Close();

			if (_thread != nullptr)
			{
				::WaitForSingleObject(_thread, INFINITE);
				::CloseHandle(_thread);
				_thread = nullptr;
			}
		}

		void SetPriority(int nPriority)
		{
			if (_thread != nullptr)
				::SetThreadPriority(_thread, nPriority);
		}

	private:
		static DWORD WINAPI Proc(LPVOID pv)
		{
			static_cast<CWorkerThread *>(pv)->Run();
			return 0;
		}

		void Run()
		{
			// set_terminate is per-thread in the Microsoft CRT.
			Crash::InstallForThread();

			// CoInitializeEx per worker: the loaders reach the shell, and 2.00
			// shipped three workers that never initialised COM at all. MTA because
			// this thread never pumps, and an STA that does not pump cannot service
			// an incoming call - it deadlocks whoever made it.
			const HRESULT hr = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);

			while (std::shared_ptr<CWorkItem> item = _queue.Take())
			{
				try
				{
					item->Work();
				}
				catch (...)
				{
					// A job that throws must still come back, or the UI waits
					// forever for a completion that never arrives - and an
					// exception escaping the thread entry point kills the process.
				}

				// Only holds while the UI queue is open; closing it before the
				// workers are joined destroys the item here, on the worker.
				UiQueue().Push(item);
			}

			if (SUCCEEDED(hr))
				::CoUninitialize();
		}

		CWorkQueue<CWorkItem> _queue;
		HANDLE _thread;
	};

	// Blocks until a message arrives or one of the handles is signalled.
	// MWMO_INPUTAVAILABLE matters: without it a message that was already peeked
	// but not removed does not count as new input and the loop hangs.
	inline DWORD WaitForMessageOrHandles(const HANDLE *handles, DWORD count, DWORD timeout = INFINITE)
	{
		const DWORD result = ::MsgWaitForMultipleObjectsEx(count, handles, timeout, QS_ALLINPUT, MWMO_INPUTAVAILABLE);

		// An invalid handle fails immediately rather than waiting, and every
		// caller loops on this, so without a pause the UI thread spins at 100%.
		if (result == WAIT_FAILED)
			::Sleep(10);

		return result;
	}
}
