// The background jobs.

#include "stdafx.h"
#include "LoadAny.h"

#include "Jobs.h"
#include "ItemsView.h"
#include "ImageView.h"

#include "iw/logfile.h"

CLoadAny& ThreadLoader()
{
	static thread_local CLoadAny loader;
	return loader;
}

void CJobThumb::Work()
{
	// Inside the job, not around the worker loop: a malformed file must cost
	// one thumbnail, not the rest of the folder.
	try
	{
		ThreadLoader().Load(&m_dib, m_strName, this, TRUE);
	}
	catch (...)
	{
	}
}

void CJobThumb::Complete()
{
	// A stale job discards its pixels but must still advance the chain:
	// OnThumbLoaded is the only thing that posts the next thumbnail job, so
	// returning here stopped thumbnail loading for the whole folder.
	CDib* pDib = (!Exit() && m_dib.IsOpen()) ? &m_dib : nullptr;

	m_pWnd->OnThumbJobComplete(pDib, m_uKey);
}

void CJobLoad::Work()
{
	try
	{
		m_bLoaded = ThreadLoader().Load(&m_dib, m_strName, this, FALSE);
	}
	catch (...)
	{
		m_bLoaded = FALSE;
	}
}

void CJobLoad::Complete()
{
	m_pWnd->OnLoaded(*this);
}

extern CDibScale* CreateScale(CDib& dib, CSize sizeDst, CStatus* pStatus);

CJobScale::~CJobScale()
{
	// OnScaled clears this when it takes the table over. Anything still here
	// belongs to a job that was abandoned - stale, or its window has gone.
	delete m_pScale;
}

void CJobScale::Work()
{
	m_pScale = CreateScale(m_dibSrc, m_Size, this);
}

void CJobScale::Complete()
{
	m_pWnd->OnScaled(*this);
}

void CJobCell::Work()
{
	CDib dib;

	// Inside the job, not around the worker loop: a malformed file must cost
	// one cell, not the rest of the collage.
	try
	{
		if (!ThreadLoader().Load(&dib, m_strName, this, FALSE) || !dib.IsOpen())
			return;
	}
	catch (...)
	{
		return;
	}

	m_sizeSource = dib.Size();

	if (m_sizeSource.cx <= m_sizeBox.cx && m_sizeSource.cy <= m_sizeBox.cy)
	{
		m_dib.Attach(dib);
		return;
	}

	CDibScale scale;

	if (scale.Scale(dib, FitToBox(m_sizeSource, m_sizeBox), this))
		m_dib.Attach(scale.m_dib);
}

void CJobCell::Complete()
{
	m_pWnd->OnCellLoaded(*this);
}

void CJobWatch::Work()
{
	// A pathless folder still gets a job posted, so that queueing it stops the
	// previous folder's watcher. There is nothing to watch here.
	if (m_strPath.IsEmpty())
		return;

	HANDLE hChange = ::FindFirstChangeNotification(m_strPath, FALSE,
	                                               FILE_NOTIFY_CHANGE_FILE_NAME |
	                                               FILE_NOTIFY_CHANGE_DIR_NAME);

	if (hChange == INVALID_HANDLE_VALUE || hChange == nullptr)
	{
		IW::Logging::LastError(_T("FindFirstChangeNotification"));
		return;
	}

	// The wake handle goes first: WaitForMultipleObjects returns the lowest
	// signalled index, so a folder that changes continuously would otherwise
	// never let the stop request through and Stop() would wait forever.
	HANDLE handles[2] = {m_hWake, hChange};

	for (;;)
	{
		const DWORD r = ::WaitForMultipleObjects(2, handles, FALSE, INFINITE);

		if (r == WAIT_FAILED)
		{
			IW::Logging::LastError(_T("WaitForMultipleObjects"));
			break;
		}

		if (r != WAIT_OBJECT_0 + 1)
			break;

		// A tie is broken in favour of the wake handle above, but the change may
		// also have been signalled first.
		if (::WaitForSingleObject(m_hWake, 0) == WAIT_OBJECT_0)
			break;

		auto changed = std::make_shared<CJobDirChanged>(m_pWnd);
		changed->SetOwner(m_owner.lock());
		IW::UiQueue().Push(changed);

		if (!::FindNextChangeNotification(hChange))
		{
			IW::Logging::LastError(_T("FindNextChangeNotification"));
			break;
		}
	}

	::FindCloseChangeNotification(hChange);
}

void CJobDirChanged::Complete()
{
	m_pWnd->OnDirChanged();
}
