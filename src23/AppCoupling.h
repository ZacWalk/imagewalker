// ImageWalker by Zac Walker
//
// Purpose: The interface the views and worker threads call the frame
//          through, so neither has to know CMainFrame.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#pragma once

class CImageLoad;

typedef std::vector<IW::FolderItemPtr> ITEMLIST;

namespace Search
{
	typedef enum { Current, MyPictures } Type;
	class Spec;
};

class Coupling
{
public:
	virtual CString GetItemPath(int nItem) const = 0;
	virtual HWND GetImageWindow() = 0;
	virtual ITEMLIST GetItemList() const = 0;
	virtual CSize GetThumbnailSize() const = 0;
	virtual bool HasSearchSpec() = 0;
	virtual bool IsFullScreen() const = 0;
	virtual bool IsItemFolder(long nItem) const = 0;	
	virtual bool SelectFolderItem(const CString &strFileName) = 0;		
	virtual bool ViewFullScreen(bool bFullScreen) = 0;
	virtual int GetFocusItem() const = 0;
	virtual int GetItemCount() const = 0;
	virtual void AfterCopy(bool bMove) = 0;
	virtual void BeginImageFade(int nSteps) = 0;

	// False when the view refuses to give up the picture it is showing.
	virtual bool CanChangeImage() = 0;
	virtual void Command(WORD id) = 0;		
	virtual void NewImage(bool bScrollToCenter) = 0;
	virtual void ResetThumbThread() = 0;
	virtual void ResetWaitForFolderToChangeThread() = 0;
	virtual void SetFocusItem(int nFocusItem, bool bSignalEvent) = 0;
	virtual void SetStatusText(const CString &str) = 0;
	virtual void SetStopLoading() = 0;
	virtual void ShowImage(CImageLoad *pInfo) = 0;
	virtual void ShowSearchPane() = 0;
	virtual void StartSearch(Search::Type type) = 0;
	virtual void SignalImageLoadComplete(CImageLoad *pInfo) = 0;
	virtual void SignalSearching() = 0;
	virtual void SignalSearchingComplete() = 0;
	virtual void SignalSearchingFolder(const CString &strPath) = 0;
	virtual void SignalThumbnailing() = 0;
	virtual void SignalThumbnailingComplete() = 0;
	virtual void SortOrderChanged(int order) = 0;
	virtual void StartSearchThread(Search::Type type, const Search::Spec &ss) = 0; 
	virtual void StartSearching() = 0;
	virtual void StopLoadingThumbs() = 0;
	virtual void TrackPopupMenu(HMENU hMenu, UINT uFlags, int x, int y) = 0;
	virtual void UpdateStatusText() = 0;
	virtual void UpdateToolbarsShown() = 0;
};
