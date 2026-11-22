#pragma once

// The background jobs.
//
// Each job is an IW::CWorkItem held by a shared_ptr for its whole life: Work()
// runs on the worker, then the item goes onto IW::UiQueue() and the message
// loop calls Complete() on the UI thread. No pointer ever travels through a
// message parameter. See iw/workqueue.h.

#include "Thumb.h"
#include "Status.h"

#include "iw/workqueue.h"

#include <atomic>

class CItemsView;
class CImageView;
class CDibScale;
class CLoadAny;
class CJobDirChanged;

// One decoder per worker thread. The format loaders hold decode state, so they
// cannot be shared, and building one per job would throw that state away
// between every thumbnail.
CLoadAny& ThreadLoader();

class CJob : public IW::CWorkItem, public CStatus
{
public:
	CJob(LONG* pJobNo, LPCSTR szName) : CStatus(pJobNo, szName)
	{
	}
};

// Decode one thumbnail for the items view.
//
// The item is named by its folder-map key rather than by address: a folder
// switch or a refresh frees every CThumb, and a raw pointer would outlive one
// and can be handed straight back by the allocator.
class CJobThumb : public CJob
{
public:
	CJobThumb(CItemsView* pWnd, const CString& strName, UINT uKey, LONG* pJobNo, int edge = IMAGE_X)
		: CJob(pJobNo, nullptr), m_pWnd(pWnd), m_uKey(uKey), m_strName(strName), m_nEdge(edge)
	{
	}

	void Work() override;
	void Complete() override;

	CItemsView* m_pWnd;
	UINT m_uKey;
	CString m_strName;
	int m_nEdge;
	CDib m_dib;
};

// Decode one full-size image for the image view.
class CJobLoad : public CJob
{
public:
	CJobLoad(CImageView* pWnd, const CString& strName, LONG* pJobNo, LPCSTR sz)
		: CJob(pJobNo, sz), m_pWnd(pWnd), m_strName(strName)
	{
	}

	void Work() override;
	void Complete() override;

	CImageView* m_pWnd;
	CString m_strName;
	CDib m_dib;
	BOOL m_bLoaded = FALSE;
};

// Build the scaling table for the image view.
class CJobScale : public CJob
{
public:
	CJobScale(CImageView* pWnd, const CDib& dibSrc, CSize size, LONG* pJobNo, LPCSTR sz,
	          BOOL bPromote)
		: CJob(pJobNo, sz), m_pWnd(pWnd), m_Size(size), m_bPromote(bPromote)
	{
		// The job owns its source. It used to alias a member of the image view,
		// which the UI thread reallocates on the next load.
		dibSrc.CopyTo(m_dibSrc);
	}

	~CJobScale() override;

	void Work() override;
	void Complete() override;

	CImageView* m_pWnd;
	CDib m_dibSrc;
	CSize m_Size;
	CDibScale* m_pScale = nullptr;

	// The source is a newly loaded image, so it becomes the displayed picture.
	BOOL m_bPromote;
};

// Decode one cell of the image view's collage.
//
// The full image is loaded and reduced on the worker and only the small copy
// comes back: two dozen photographs at full size would not fit in memory, and
// the cell is a few hundred pixels across.
class CJobCell : public CJob
{
public:
	CJobCell(CImageView* pWnd, const CString& strName, int nCell, CSize sizeBox, LONG* pJobNo)
		: CJob(pJobNo, "Loading %d%%"), m_pWnd(pWnd), m_strName(strName), m_nCell(nCell),
		  m_sizeBox(sizeBox)
	{
	}

	void Work() override;
	void Complete() override;

	CImageView* m_pWnd;
	CString m_strName;
	int m_nCell;
	CSize m_sizeBox;
	CDib m_dib;

	// The file's own pixels, which is what packs the collage layout.
	CSize m_sizeSource{0, 0};
};

// Watch a folder for changes until the worker is stopped or another job is
// queued behind it. It waits on both the worker's wake handle and the change
// notification, and queues at most one pending refresh of this folder.
class CJobWatch : public CJob
{
public:
	CJobWatch(const CString& strPath, HANDLE hWake, const std::shared_ptr<CJobDirChanged>& change)
		: CJob(nullptr, nullptr), m_strPath(strPath), m_hWake(hWake), m_change(change)
	{
	}

	void Work() override;

private:
	CString m_strPath;
	HANDLE m_hWake;
	std::shared_ptr<CJobDirChanged> m_change;
};

// Each watch has its own request so the items pane can reject a refresh
// queued for a folder it has already left.
class CJobDirChanged : public IW::CWorkItem
{
public:
	explicit CJobDirChanged(CItemsView* pWnd) : m_pWnd(pWnd)
	{
	}

	void Work() override
	{
	}

	static void Post(const std::shared_ptr<CJobDirChanged>& change);
	void Complete() override;

private:
	CItemsView* m_pWnd;
	std::atomic_bool m_pending{false};
};
