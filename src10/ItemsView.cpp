// The items pane.

#include "stdafx.h"
#include "ImageWalker.h"
#include "ItemsView.h"
#include "Jobs.h"

#include "iw/logfile.h"

#include <algorithm>
#include <libfiles/decode.h>
#include <libfiles/document.h>

static constexpr TCHAR szSection[] = _T("Default");
static constexpr TCHAR szFolderKey[] = _T("Folder");
static constexpr TCHAR szSortKey[] = _T("Sort By");
static constexpr TCHAR szDetailKey[] = _T("Detail View");
static constexpr TCHAR szThumbnailSizeKey[] = _T("Thumbnail Size");

namespace
{
	// Toolbar glyph indices into IDB_VIEWS, as 2.0 used them.
	enum { kGlyphThumbnails = 0, kGlyphDetails = 1, kGlyphOptions = 4 };

	CString FormatDate(const FILETIME& time)
	{
		SYSTEMTIME st = {0};
		FILETIME local = {0};

		if (!::FileTimeToLocalFileTime(&time, &local) || !::FileTimeToSystemTime(&local, &st))
			return CString();

		TCHAR szDate[64] = {0};
		TCHAR szTime[64] = {0};

		::GetDateFormat(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &st, nullptr, szDate, _countof(szDate));
		::GetTimeFormat(LOCALE_USER_DEFAULT, TIME_NOSECONDS, &st, nullptr, szTime, _countof(szTime));

		CString str;
		str.Format(_T("%s %s"), szDate, szTime);
		return str;
	}
}

CItemsView::CItemsView()
{
	m_alive = std::make_shared<int>(0);
	m_bDetail = Settings::GetInt(szSection, szDetailKey, 0) != 0;
	m_nThumbnailSize = std::clamp(Settings::GetInt(szSection, szThumbnailSizeKey, IMAGE_X),
	                             static_cast<int>(CThumbnailGrid::minEdge), static_cast<int>(CThumbnailGrid::maxEdge));

	SetSortCommand(Settings::GetInt(szSection, szSortKey, ID_VIEW_SORT_NAME));
}

CItemsView::~CItemsView()
{
	// Cancel first, or Stop() waits for whatever thumbnail is being decoded - a
	// big or slow image would leave the process running with no window at all.
	::InterlockedIncrement(&m_nThumbOrder);

	m_threadDirectory.Stop();
	m_threadLoader.Stop();
	if (m_font) ::DeleteObject(m_font);

	// Any job that came back after this point has nothing to complete into.
	m_alive.reset();

	if (m_pFolder)
		ShellItemProfileWrite(m_pFolder->m_Item, szSection, szFolderKey);

	Settings::SetInt(szSection, szSortKey, m_nSortCommand);
	Settings::SetInt(szSection, szDetailKey, m_bDetail ? 1 : 0);
	Settings::SetInt(szSection, szThumbnailSizeKey, m_nThumbnailSize);
}

LRESULT CItemsView::WindowMessage(UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
	case WM_CREATE: return OnCreate(reinterpret_cast<LPCREATESTRUCT>(lParam));
	case WM_DESTROY: OnDestroy(); break;
	case WM_SIZE: OnSize(static_cast<UINT>(wParam), CSize(LOWORD(lParam), HIWORD(lParam))); return 0;
	case WM_LBUTTONDOWN:
		OnLButtonDown(static_cast<UINT>(wParam), MessagePoint(lParam));
		return 0;
	case WM_LBUTTONDBLCLK: OnLButtonDblClk(0, MessagePoint(lParam)); return 0;
	case WM_MOUSEMOVE: OnMouseMove(0, MessagePoint(lParam)); break;
	case WM_KEYDOWN: if (OnKeyDown(static_cast<UINT>(wParam))) return 0; break;
	case WM_COMMAND: if (Command(LOWORD(wParam))) return 0; break;
	case WM_NOTIFY:
		if (auto* header = reinterpret_cast<NMHDR*>(lParam); header && header->code == TTN_GETDISPINFOA)
			return OnToolTipText(header);
		break;
	}
	return CViewBase<CItemsView>::WindowMessage(message, wParam, lParam);
}

bool CItemsView::Command(int id)
{
	switch (id)
	{
	case ID_ITEMS_OPTIONS: OnItemsOptions(0, id, {}); return true;
	case ID_BROWSE_PARENT: OnBrowseParent(0, id, {}); return true;
	case ID_BROWSE_BACK: OnBrowseBack(0, id, {}); return true;
	case ID_BROWSE_FORWARD: OnBrowseForward(0, id, {}); return true;
	case ID_EDIT_COPY: OnEditCopy(0, id, {}); return true;
	case ID_EDIT_SELECT_ALL: OnEditSelectAll(0, id, {}); return true;
	case ID_EDIT_INVERTSELECTION: OnEditInvertSelection(0, id, {}); return true;
	case ID_EDIT_TOGGLE_FOCUS:
		Select(m_selection.Focus(), true, false);
		return true;
	case ID_VIEW_REFRESH: OnViewRefresh(0, id, {}); return true;
	case ID_VIEW_SORT_NAME:
	case ID_VIEW_SORT_TYPE:
	case ID_VIEW_SORT_SIZE:
	case ID_VIEW_SORT_DATE: OnViewSort(0, id, {}); return true;
	case ID_ITEMS_THUMBNAILS:
	case ID_ITEMS_DETAILS:
		OnViewMode(0, id, {});
		::SendMessage(GetTopLevelParent(), WM_IW_SELECTIONCHANGED, 0, 0);
		return true;
	case ID_VIEW_NEXTIMAGE: OnNextImage(0, id, {}); return true;
	case ID_VIEW_PREVIOUSIMAGE: OnPreviousImage(0, id, {}); return true;
	}
	return false;
}

LRESULT CItemsView::OnCreate(LPCREATESTRUCT)
{
	if (!InitViewBase()) return -1;

	// The strip lives in the right scroll bar gutter, which is therefore always
	// present whether or not the bar itself is needed.
	m_sizeGutterMin.cx = m_cxBar;

	LOGFONT logfont = {0};

	if (!::SystemParametersInfo(SPI_GETICONTITLELOGFONT, sizeof(LOGFONT), &logfont, 0))
		::GetObject(::GetStockObject(DEFAULT_GUI_FONT), sizeof(logfont), &logfont);

	m_font = ::CreateFontIndirect(&logfont);

	{
		CClientDC dc(m_hWnd);
		HFONT hOld = dc.SelectFont(m_font ? m_font : DefaultGuiFont());
		TEXTMETRIC tm = {0};
		dc.GetTextMetrics(&tm);
		dc.SelectFont(hOld);

		m_nRowHeight = max(tm.tmHeight + 6, 20);
	}

	if (!m_toolBar.Create(m_hWnd, CRect(0, 0, 0, 0), nullptr,
	                 WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | CCS_VERT | CCS_NORESIZE |
	                 CCS_NOPARENTALIGN | CCS_NODIVIDER | TBSTYLE_TOOLTIPS | TBSTYLE_WRAPABLE,
	                 0, kToolBarId))
		return -1;

	m_toolBar.SetButtonStructSize(sizeof(TBBUTTON));

	// No TBSTYLE_EX_DRAWDDARROWS here, and no drop-down button style: the arrow
	// makes the options button wider than the scroll bar gutter, and a wrapable
	// bar then wraps it out of sight. The menu comes off its command instead.

	m_toolBar.SetImageList(LoadToolbarImages(IDB_VIEWS, 8));

	TBBUTTON buttons[] =
	{
		{kGlyphThumbnails, ID_ITEMS_THUMBNAILS, TBSTATE_ENABLED, BTNS_CHECKGROUP, {0, 0}, 0, 0},
		{kGlyphDetails, ID_ITEMS_DETAILS, TBSTATE_ENABLED, BTNS_CHECKGROUP, {0, 0}, 0, 0},
		{kGlyphOptions, ID_ITEMS_OPTIONS, TBSTATE_ENABLED, BTNS_BUTTON, {0, 0}, 0, 0},
	};

	m_toolBar.AddButtons(_countof(buttons), buttons);
	m_toolBar.SetBitmapSize(CSize(8, 8));
	m_toolBar.SetButtonSize(CSize(m_cxBar, m_cxBar));

	if (m_toolTip.Create(m_hWnd, TTS_ALWAYSTIP) && m_toolTip.AddTool(m_hWnd))
	{
		m_toolTip.SetMaxTipWidth(SHRT_MAX);
		m_toolTip.SetDelayTime(TTDT_AUTOPOP, SHRT_MAX);
		m_toolTip.SetDelayTime(TTDT_INITIAL, 200);
		m_toolTip.SetDelayTime(TTDT_RESHOW, 200);
	}

	// A failed Start still accepts posts, so the queue would fill with jobs
	// nothing consumes and the pane would look like it was still loading.
	if (!m_threadDirectory.Start() || !m_threadLoader.Start())
	{
		IW::Logging::Error(_T("Failed to start the items worker threads"));
		return -1;
	}

	const auto open = [this](const CShellItem& item)
	{
		try
		{
			Navigate(item);
			return true;
		}
		catch (const std::exception&)
		{
			return false;
		}
	};

	// A folder on the command line wins over the one we were last in.
	bool opened = false;
	if (!g_strCmdLine.IsEmpty())
	{
		CShellItem item;
		CShellDesktop desktop;

		if (SUCCEEDED(ShellParseDisplayName(desktop, m_hWnd, g_strCmdLine, item)))
		{
			CString selectedPath;
			if (ShellItemAttributes(item, SFGAO_LINK) & SFGAO_LINK)
				desktop.ResolveLink(item);

			if (!(ShellItemAttributes(item, SFGAO_FOLDER) & SFGAO_FOLDER))
			{
				item.GetPath(selectedPath);
				item.StripToParent();
			}

			opened = open(item);
			if (opened && !selectedPath.IsEmpty())
				Reselect(selectedPath, true);
		}
	}

	if (!opened)
	{
		CShellItem remembered;
		ShellItemProfileRead(remembered, szSection, szFolderKey);
		if (!IW::OpenStartupFolder(m_hWnd, remembered, open))
			IW::Logging::Error(_T("Failed to open a startup folder"));
	}

	UpdateBars();
	return 0;
}

void CItemsView::OnDestroy()
{
	m_alive.reset();
	m_dirChange.reset();
	::InterlockedIncrement(&m_nThumbOrder);
	m_items.clear();
	m_selection.Reset(0);
}

void CItemsView::OnSize(UINT, CSize)
{
	LayoutItems(false);
	Invalidate(FALSE);
}

//////////////////////////////////////////////////////////////////////////////
// Navigation

CShellItem CItemsView::FolderItem() const
{
	return m_pFolder ? m_pFolder->m_Item : CShellDesktopItem();
}

bool CItemsView::CanGoParent() const
{
	return m_pFolder && !m_pFolder->m_Item.IsDesktop();
}

void CItemsView::Navigate(const CShellItem& item)
{
	auto pFolder = std::make_unique<CFolder>(m_hWnd, item);

	// A new destination discards anything ahead of us in the history.
	if (!m_history.empty())
		m_history.resize(m_nHistory + 1);

	m_history.push_back(item);
	m_nHistory = m_history.size() - 1;

	OpenFolder(std::move(pFolder));
}

void CItemsView::OpenFolder(std::unique_ptr<CFolder> pFolder)
{
	m_pFolder = std::move(pFolder);
	Populate({}, true);
}

void CItemsView::OnBrowseParent(UINT, int, CWindow)
{
	if (!CanGoParent())
		return;

	CWaitCursor wait;

	CShellItem item(m_pFolder->m_Item);
	item.StripToParent();

	try
	{
		Navigate(item);
	}
	catch (const std::exception&)
	{
	}
}

void CItemsView::OnBrowseBack(UINT, int, CWindow)
{
	if (!CanGoBack())
		return;

	CWaitCursor wait;

	try
	{
		auto pFolder = std::make_unique<CFolder>(m_hWnd, m_history[m_nHistory - 1]);
		m_nHistory -= 1;
		OpenFolder(std::move(pFolder));
	}
	catch (const std::exception&)
	{
	}
}

void CItemsView::OnBrowseForward(UINT, int, CWindow)
{
	if (!CanGoForward())
		return;

	CWaitCursor wait;

	try
	{
		auto pFolder = std::make_unique<CFolder>(m_hWnd, m_history[m_nHistory + 1]);
		m_nHistory += 1;
		OpenFolder(std::move(pFolder));
	}
	catch (const std::exception&)
	{
	}
}

//////////////////////////////////////////////////////////////////////////////
// The item list

// The list is the folder map in the current sort order. Rebuilt whenever the
// map changes, which renumbers every index the selection holds.
void CItemsView::RebuildItems()
{
	m_items.clear();
	m_nToolTipItem = -1;

	if (m_pFolder)
	{
		m_items.reserve(m_pFolder->m_mapThumbs.size());

		for (const auto& entry : m_pFolder->m_mapThumbs)
			m_items.push_back({entry.second.get(), entry.first, 0});
	}

	Sort();
	CountLoaded();
	UpdateThumbnailDecodeSize();
}

// Puts the focus back on strFocus after a reorder, and falls back to the first
// item worth showing - which is what makes a newly opened folder display
// something, and what recovers when the displayed file is deleted under us.
void CItemsView::Reselect(const CString& strFocus, bool bTop)
{
	const int nCount = static_cast<int>(m_items.size());

	m_selection.Reset(nCount);

	for (int i = 0; i < nCount && !strFocus.IsEmpty(); i++)
	{
		if (DisplayName(i, SHGDN_FORPARSING | SHGDN_INCLUDE_NONFILESYS) == strFocus)
		{
			m_selection.SelectOnly(i);
			break;
		}
	}

	if (m_selection.Focus() < 0)
	{
		for (int i = 0; i < nCount; i++)
		{
			if (m_items[i].pThumb->IsLoadable())
			{
				m_selection.SelectOnly(i);
				break;
			}
		}
	}

	LayoutItems(bTop);
	EnsureVisible(m_selection.Focus());
	Invalidate();

	// Tells the frame the folder, the title, the address and the picture, all of
	// which follow from the selection.
	SelectionChanged();

	PrimeThumbChain();
}

void CItemsView::Populate(const CString& strFocus, bool bTop)
{
	// Every queued thumbnail job refers to the previous folder snapshot.
	::InterlockedIncrement(&m_nThumbOrder);

	RebuildItems();
	Reselect(strFocus, bTop);

	// Posted even when the folder has no path. The previous folder's watcher
	// only stops when something is queued behind it, so skipping this left it
	// running against the folder we just left. An empty path returns at once.
	CString strDirName;

	if (!m_pFolder || !m_pFolder->GetPath(strDirName))
		strDirName.Empty();

	m_dirChange = std::make_shared<CJobDirChanged>(this);
	m_dirChange->SetOwner(m_alive);
	auto watch = std::make_shared<CJobWatch>(strDirName, m_threadDirectory.WakeHandle(), m_dirChange);
	watch->SetOwner(m_alive);
	m_threadDirectory.Post(watch);
}

void CItemsView::Sort()
{
	if (!m_pFolder || m_Compare == nullptr)
		return;

	CFolder& folder = *m_pFolder;
	auto compare = m_Compare;

	std::stable_sort(m_items.begin(), m_items.end(),
	                 [&folder, compare](const CItem& a, const CItem& b)
	                 {
		                 return compare(folder, *a.pThumb, *b.pThumb) < 0;
	                 });
}

// How many items have a decoded thumbnail.
void CItemsView::CountLoaded()
{
	m_nLoaded = 0;

	for (const CItem& item : m_items)
		if (item.pThumb->m_Dib.IsOpen())
			m_nLoaded++;
}

void CItemsView::SetSortCommand(int nID)
{
	switch (nID)
	{
	case ID_VIEW_SORT_TYPE:
		m_Compare = CThumb::CompareType;
		break;
	case ID_VIEW_SORT_DATE:
		m_Compare = CThumb::CompareDate;
		break;
	case ID_VIEW_SORT_SIZE:
		m_Compare = CThumb::CompareSize;
		break;
	default:
		nID = ID_VIEW_SORT_NAME;
		m_Compare = CThumb::CompareName;
		break;
	}

	m_nSortCommand = nID;
}

void CItemsView::OnViewSort(UINT, int nID, CWindow)
{
	CWaitCursor wait;

	const CString strFocus = FocusItemName();

	SetSortCommand(nID);
	Sort();
	Reselect(strFocus, true);
}

void CItemsView::OnViewRefresh(UINT, int, CWindow)
{
	if (!m_pFolder)
		return;

	CWaitCursor wait;
	const CString strFocus = FocusItemName();
	try
	{
		// F5 must reload cached thumbnails even if the shell returns the same PIDLs.
		m_pFolder = std::make_unique<CFolder>(m_hWnd, m_pFolder->m_Item);
	}
	catch (const std::exception& error)
	{
		IW::Logging::Warn("Refresh folder: %hs", error.what());
		::MessageBeep(MB_OK);
		return;
	}
	Populate(strFocus, false);
}

void CItemsView::OnDirChanged(const CJobDirChanged& change)
{
	if (&change != m_dirChange.get() || !m_pFolder)
		return;

	CWaitCursor wait;

	const CString strFocus = FocusItemName();

	if (!m_pFolder->Refresh())
	{
		IW::Logging::Warn("Could not read the changed folder");
		return;
	}
	::InterlockedIncrement(&m_nThumbOrder);

	RebuildItems();
	Reselect(strFocus, false);
}

CString CItemsView::DisplayName(int n, DWORD dwFlags) const
{
	if (!m_pFolder || n < 0 || n >= static_cast<int>(m_items.size()))
		return CString();

	return m_pFolder->GetDisplayNameOf(*m_items[n].pThumb, dwFlags);
}

CString CItemsView::FocusItemName() const
{
	const int n = m_selection.Focus();

	if (n < 0 || n >= static_cast<int>(m_items.size()))
		return CString();

	return DisplayName(n, SHGDN_FORPARSING | SHGDN_INCLUDE_NONFILESYS);
}

CPreviewSelection CItemsView::SelectedItems() const
{
	CPreviewSelection result;

	const int nFocus = m_selection.Focus();

	for (const int n : m_selection.Selected())
	{
		CThumb* pThumb = m_items[n].pThumb;

		// Counted whether or not it gets a cell: a selection larger than the
		// pane can draw still has to say what it holds.
		result.nSelected += 1;
		result.nBytes += pThumb->m_sizeFile;

		if (static_cast<int>(result.items.size()) >= CImageView::kMaxCells)
			continue;

		CPreviewItem item;
		item.strName = DisplayName(n, SHGDN_FORPARSING | SHGDN_INCLUDE_NONFILESYS);

		if (item.strName.IsEmpty())
			continue;

		// Folders come through too. The image pane names what it cannot draw
		// rather than going blank, and only bImage is worth a decode.
		item.bImage = pThumb->IsLoadable();
		item.nBytes = pThumb->m_sizeFile;
		item.bFocus = (n == nFocus);

		// Borrowed for the length of the call. The image pane blows it up to
		// the size the decoded picture will occupy and holds it there until the
		// decode lands.
		item.pThumb = pThumb->m_Dib.IsOpen() ? &pThumb->m_Dib : nullptr;

		result.items.push_back(item);
	}

	return result;
}

CString CItemsView::StatusText() const
{
	CString str;

	if (m_selection.Count() > 1)
		str.Format(_T("%d of %d selected"), m_selection.Count(), static_cast<int>(m_items.size()));
	else
		str.Format(_T("%d items, %d thumbnails"), static_cast<int>(m_items.size()), m_nLoaded);

	return str;
}

//////////////////////////////////////////////////////////////////////////////
// Geometry

void CItemsView::SetThumbnailSize(int size, bool tracking)
{
	const int value = std::clamp(size, static_cast<int>(CThumbnailGrid::minEdge),
	                             static_cast<int>(CThumbnailGrid::maxEdge));
	if (value != m_nThumbnailSize)
	{
		m_nThumbnailSize = value;
		if (!m_bDetail)
		{
			const int first = (m_pointScroll.y / m_grid.rowHeight) * m_grid.columns;
			LayoutItems(false);
			ScrollTo(0, m_grid.Cell(first).top);
			Invalidate(FALSE);
		}
	}
	if (!tracking)
	{
		UpdateThumbnailDecodeSize();
		PrimeThumbChain();
	}
}

void CItemsView::UpdateThumbnailDecodeSize()
{
	// Keep the original cache size in large folders; only enlarge decodes when they fit the budget.
	constexpr size_t budget = 32 * 1024 * 1024;
	const size_t budgetPerItem = budget / max(size_t{1}, m_items.size());
	int edge = (m_nThumbnailSize + 31) / 32 * 32;
	while (edge > IMAGE_X && static_cast<size_t>(edge) * edge * 4 > budgetPerItem)
		edge -= 32;
	m_nDecodeSize = max(IMAGE_X, edge);
}

void CItemsView::LayoutItems(bool bTop)
{
	const int nCount = static_cast<int>(m_items.size());

	if (m_bDetail)
	{
		m_nScrollLineY = m_nRowHeight;

		SetScrollSize(0, nCount * m_nRowHeight, bTop ? j_top : j_none);
	}
	else
	{
		CRect r;
		GetClientRect(&r);

		m_grid.Layout(max(0, r.Width() - m_sizeGutterMin.cx), nCount, m_nThumbnailSize, m_nRowHeight);
		m_nScrollLineY = m_grid.rowHeight;
		const int cy = nCount > 0 ? m_grid.Cell(nCount - 1).bottom : 0;
		SetScrollSize(0, cy, bTop ? j_top : j_none);
	}
}

CRect CItemsView::ItemRect(int n) const
{
	if (m_bDetail)
	{
		const CRect r = ViewRect();
		return CRect(0, n * m_nRowHeight, max(r.Width(), m_sizeScroll.cx), (n + 1) * m_nRowHeight);
	}

	return m_grid.Cell(n);
}

int CItemsView::ItemFromPoint(CPoint point) const
{
	const int nCount = static_cast<int>(m_items.size());

	if (point.x < 0 || point.y < 0)
		return -1;

	if (!m_bDetail)
		return m_grid.Hit(point, nCount);

	const int n = point.y / m_nRowHeight;
	return n < nCount ? n : -1;
}

void CItemsView::InvalidateItem(int n)
{
	if (n < 0 || n >= static_cast<int>(m_items.size()))
		return;

	CRect r = ItemRect(n);
	r.OffsetRect(-m_pointScroll);

	InvalidateRect(r, FALSE);
}

void CItemsView::EnsureVisible(int n)
{
	if (n < 0)
		return;

	const CRect rView = ViewRect();
	const CRect r = ItemRect(n);

	if (r.top < m_pointScroll.y)
		ScrollTo(m_pointScroll.x, r.top);
	else if ((r.bottom - m_pointScroll.y) > rView.Height())
		ScrollTo(m_pointScroll.x, r.bottom - rView.Height());
}

//////////////////////////////////////////////////////////////////////////////
// Selection

void CItemsView::Select(int n, bool bControl, bool bShift)
{
	if (n < 0 || n >= static_cast<int>(m_items.size()))
		return;

	m_selection.Click(n, bControl, bShift);

	EnsureVisible(m_selection.Focus());
	Invalidate();

	// Unconditionally: the image pane shows the whole selection, so a ctrl-click
	// that only adds an item to it still changes what is on screen.
	SelectionChanged();
}

void CItemsView::SelectionChanged()
{
	// The frame, not the splitter that happens to be our parent.
	::SendMessage(GetTopLevelParent(), WM_IW_SELECTIONCHANGED, 0, 0);
}

void CItemsView::OnEditSelectAll(UINT, int, CWindow)
{
	m_selection.SelectAll();
	Invalidate();
	SelectionChanged();
}

void CItemsView::OnEditInvertSelection(UINT, int, CWindow)
{
	m_selection.Invert();
	Invalidate();
	SelectionChanged();
}

void CItemsView::OnNextImage(UINT, int, CWindow)
{
	StepPhoto(1);
}

void CItemsView::OnPreviousImage(UINT, int, CWindow)
{
	StepPhoto(-1);
}

bool CItemsView::CanPreview(int index) const
{
	const auto& thumb = *m_items[index].pThumb;
	if (!thumb.IsLoadable()) return false;
	if (thumb.m_Dib.IsOpen()) return true;
	const auto name = DisplayName(index, SHGDN_FORPARSING | SHGDN_INCLUDE_NONFILESYS);
	const auto format = libfiles::info(libfiles::file_type_from_extension(::PathFindExtensionA(name))).image;
	return libfiles::can_decode(format) || libfiles::can_decode_document(format);
}

void CItemsView::StepPhoto(int direction)
{
	const int count = static_cast<int>(m_items.size());
	const int focus = m_selection.Focus();
	const int start = focus >= 0 ? focus + direction : direction > 0 ? 0 : count - 1;
	for (int i = start; i >= 0 && i < count; i += direction)
	{
		if (!CanPreview(i)) continue;
		Select(i, false, false);
		return;
	}
}

bool CItemsView::FocusPhoto()
{
	const int focus = m_selection.Focus();
	if (focus < 0 || !CanPreview(focus)) return false;
	if (m_selection.Count() != 1 || !m_selection.Contains(focus)) Select(focus, false, false);
	return true;
}

void CItemsView::ActivateFocus()
{
	const int focus = m_selection.Focus();
	if (focus >= 0 && !OpenFolderItem(focus) && CanPreview(focus))
		::SendMessage(GetTopLevelParent(), WM_COMMAND, ID_MODE_FULLSCREEN, 0);
}

//////////////////////////////////////////////////////////////////////////////
// Mouse and keyboard

void CItemsView::OnLButtonDown(UINT nFlags, CPoint point)
{
	SetFocus();

	const int n = ItemFromPoint(point + m_pointScroll);

	if (n < 0)
	{
		// A click on the margin or below the last row clears the selection.
		if (!(nFlags & (MK_CONTROL | MK_SHIFT)))
		{
			m_selection.Clear();
			Invalidate();
			SelectionChanged();
		}

		return;
	}

	Select(n, (nFlags & MK_CONTROL) != 0, (nFlags & MK_SHIFT) != 0);
}

void CItemsView::OnLButtonDblClk(UINT, CPoint point)
{
	CWaitCursor wait;
	SetFocus();

	const int n = ItemFromPoint(point + m_pointScroll);

	if (n < 0)
		return;

	Select(n, false, false);
	ActivateFocus();
}

bool CItemsView::OpenFolderItem(int n)
{
	CThumb* pThumb = m_items[n].pThumb;

	CShellItem item;
	item.Cat(m_pFolder->m_Item, *pThumb);

	bool bFolder = pThumb->IsFolder();

	if (pThumb->m_ulAttribs & SFGAO_LINK)
	{
		CShellDesktop desktop;
		desktop.ResolveLink(item);
		bFolder = (ShellItemAttributes(item, SFGAO_FOLDER) & SFGAO_FOLDER) != 0;
	}

	if (!bFolder)
		return false;

	try
	{
		Navigate(item);
	}
	catch (const std::exception& error)
	{
		IW::Logging::Warn("Open focused folder: %hs", error.what());
		::MessageBeep(MB_OK);
	}
	return true;
}

void CItemsView::OnMouseMove(UINT, CPoint point)
{
	if (m_toolTip.IsWindow())
	{
		const int n = ItemFromPoint(point + m_pointScroll);

		if (m_nToolTipItem != n)
		{
			m_toolTip.Activate(FALSE);
			m_nToolTipItem = n;

			if (n != -1)
				m_toolTip.Activate(TRUE);
		}
	}

}

bool CItemsView::OnKeyDown(UINT nChar)
{
	const int nCount = static_cast<int>(m_items.size());

	if (nCount == 0)
		return false;

	const bool bControl = (::GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
	const bool bShift = (::GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;

	const int nFocus = m_selection.Focus();

	if (nFocus == -1)
	{
		switch (nChar)
		{
		case VK_LEFT:
		case VK_UP:
		case VK_RIGHT:
		case VK_DOWN:
		case VK_PRIOR:
		case VK_NEXT:
			Select(0, bControl, bShift);
			return true;
		default:
			break;
		}
	}

	const CRect r = ViewRect();
	const int nRow = m_bDetail ? m_nRowHeight : m_grid.rowHeight;
	const int columns = m_bDetail ? 1 : m_grid.columns;
	const int nPage = max(r.Height() / nRow, 1) * columns;

	switch (nChar)
	{
	case VK_RETURN:
		if (!bControl && !bShift)
			ActivateFocus();
		break;
	case VK_END:
		Select(nCount - 1, bControl, bShift);
		break;
	case VK_HOME:
		Select(0, bControl, bShift);
		break;
	case VK_LEFT:
		Select(nFocus - 1, bControl, bShift);
		break;
	case VK_UP:
		Select(nFocus - columns, bControl, bShift);
		break;
	case VK_RIGHT:
		Select(nFocus + 1, bControl, bShift);
		break;
	case VK_DOWN:
		// Reach a short final row from a column past its end.
		Select(min(nFocus + columns, nCount - 1), bControl, bShift);
		break;
	case VK_PRIOR:
		Select(max(nFocus - nPage, 0), bControl, bShift);
		break;
	case VK_NEXT:
		Select(min(nFocus + nPage, nCount - 1), bControl, bShift);
		break;
	default:
		return false;
	}
	return true;
}

//////////////////////////////////////////////////////////////////////////////
// Copy

// A DROPFILES block built here rather than through IContextMenu's "copy" verb.
// Asking the shell for a context menu instantiates every context-menu handler
// registered for the file type, which is third-party code in this process.
void CItemsView::OnEditCopy(UINT, int, CWindow)
{
	std::vector<wchar_t> names;

	for (const int n : m_selection.Selected())
	{
		const auto path = WideText(DisplayName(n, SHGDN_FORPARSING | SHGDN_INCLUDE_NONFILESYS));

		if (path.empty() || !::PathFileExistsW(path.c_str()))
			continue;

		names.insert(names.end(), path.c_str(), path.c_str() + path.size() + 1);
	}

	if (names.empty())
		return;

	names.push_back(L'\0'); // the second null that ends the list

	const size_t nBytes = sizeof(DROPFILES) + names.size() * sizeof(wchar_t);
	const HGLOBAL hMem = ::GlobalAlloc(GMEM_MOVEABLE, nBytes);

	if (hMem == nullptr)
	{
		IW::Logging::LastError(_T("GlobalAlloc (clipboard)"));
		return;
	}

	auto pDrop = static_cast<DROPFILES*>(::GlobalLock(hMem));
	if (pDrop == nullptr)
	{
		IW::Logging::LastError(_T("GlobalLock (clipboard)"));
		::GlobalFree(hMem);
		return;
	}

	ZeroMemory(pDrop, nBytes);
	pDrop->pFiles = sizeof(DROPFILES);
	pDrop->fWide = TRUE;
	CopyMemory(reinterpret_cast<BYTE*>(pDrop) + sizeof(DROPFILES),
	           names.data(), names.size() * sizeof(wchar_t));
	::GlobalUnlock(hMem);

	if (!::OpenClipboard(m_hWnd))
	{
		IW::Logging::LastError(_T("OpenClipboard"));
		::GlobalFree(hMem);
		return;
	}

	if (!::EmptyClipboard())
	{
		IW::Logging::LastError(_T("EmptyClipboard"));
		::GlobalFree(hMem);
		::CloseClipboard();
		return;
	}

	// The clipboard owns the block on success, and only on success.
	if (::SetClipboardData(CF_HDROP, hMem) == nullptr)
	{
		IW::Logging::LastError(_T("SetClipboardData"));
		::GlobalFree(hMem);
	}

	::CloseClipboard();
}

//////////////////////////////////////////////////////////////////////////////
// The view-mode strip

// Right gutter, top to bottom: the vertical scroll bar, then the view-mode
// strip hard against the bottom.
void CItemsView::LayoutControls(const CRect& rectClient)
{
	const int cx = m_cxBar;

	// Measured from the buttons, not from GetMaxSize: a wrapable vertical bar
	// reports the size it would take unwrapped.
	CRect rectLast(0, 0, cx, cx);

	if (m_toolBar.GetButtonCount() > 0)
		m_toolBar.GetItemRect(m_toolBar.GetButtonCount() - 1, &rectLast);

	const int cyToolBar = rectLast.bottom;

	// Stop short of the horizontal gutter so the strip never lands in the corner
	// square the two bars share.
	const int yEnd = max(rectClient.bottom - m_sizeGutter.cy, 0L);
	const int y = max(yEnd - cyToolBar, 0);

	PlaceControl(m_toolBar, CRect(rectClient.right - cx, y, rectClient.right, y + cyToolBar));

	m_nReservedBottom = cyToolBar;
}

void CItemsView::UpdateBars()
{
	if (!m_toolBar.IsWindow())
		return;

	m_toolBar.CheckButton(ID_ITEMS_THUMBNAILS, !m_bDetail);
	m_toolBar.CheckButton(ID_ITEMS_DETAILS, m_bDetail);
}

void CItemsView::SetViewMode(bool bDetail)
{
	if (m_bDetail == bDetail)
		return;

	m_bDetail = bDetail;
	UpdateBars();
	LayoutItems(true);
	EnsureVisible(m_selection.Focus());
	Invalidate();
}

void CItemsView::OnViewMode(UINT, int nID, CWindow)
{
	SetViewMode(nID == ID_ITEMS_DETAILS);
}

// Opened from the button's command rather than from TBN_DROPDOWN, so the
// button needs no drop-down arrow to be clickable. The popup is the frame's own
// View menu: one copy of the items, and the frame marks it in
// WM_INITMENUPOPUP.
void CItemsView::OnItemsOptions(UINT, int, CWindow)
{
	CRect r;
	m_toolBar.GetRect(ID_ITEMS_OPTIONS, &r);
	m_toolBar.ClientToScreen(&r);

	// The strip sits in the bottom right corner, so the menu grows up and left.
	TrackResourceMenu(IDR_MAINFRAME, kMenuView, TPM_RIGHTALIGN | TPM_BOTTOMALIGN | TPM_LEFTBUTTON,
	                  r.TopLeft(), GetTopLevelParent());
}

LRESULT CItemsView::OnToolTipText(LPNMHDR pnmh)
{
	auto pTTT = reinterpret_cast<LPNMTTDISPINFO>(pnmh);

	// The strip's own tooltip control asks by button command id, not by window
	// handle.
	if (!(pTTT->uFlags & TTF_IDISHWND))
	{
		switch (static_cast<UINT>(pnmh->idFrom))
		{
		case ID_ITEMS_THUMBNAILS: m_strToolText = _T("Thumbnails");
			break;
		case ID_ITEMS_DETAILS: m_strToolText = _T("Details");
			break;
		case ID_ITEMS_OPTIONS: m_strToolText = _T("View");
			break;
		default:
			return 0;
		}

		pTTT->lpszText = const_cast<LPTSTR>(static_cast<LPCTSTR>(m_strToolText));
		pTTT->hinst = nullptr;
		return 0;
	}

	if (m_nToolTipItem < 0 || m_nToolTipItem >= static_cast<int>(m_items.size()))
		return 0;

	CThumb* pThumb = m_items[m_nToolTipItem].pThumb;

	m_strToolText = DisplayName(m_nToolTipItem);

	CShellItem item;
	item.Cat(m_pFolder->m_Item, *pThumb);
	m_strToolText += _T("\n") + ShellItemType(item);

	if (pThumb->IsLoadable())
	{
		if (pThumb->m_sizeFile)
		{
			CString str;
			str.Format(_T("File Size %I64d bytes"), pThumb->m_sizeFile);
			m_strToolText += _T("\n") + str;
		}

		if (pThumb->m_Dib.IsOpen() && !pThumb->m_Dib.m_strInfo.IsEmpty())
			m_strToolText += _T("\n") + pThumb->m_Dib.m_strInfo;
	}

	pTTT->lpszText = const_cast<LPTSTR>(static_cast<LPCTSTR>(m_strToolText));
	pTTT->hinst = nullptr;

	return 0;
}

//////////////////////////////////////////////////////////////////////////////
// Thumbnail loading

void CItemsView::OnThumbJobComplete(CDib* pDib, UINT uKey, int edge)
{
	if (m_nThumbJobsInFlight > 0)
		m_nThumbJobsInFlight -= 1;

	OnThumbLoaded(pDib, uKey, edge);
}

// Starts a run only when one is not already going. The chain carries exactly
// one job at a time and each completion posts the next, so an extra head - a
// folder change arriving mid-run - doubles the work rather than helping.
void CItemsView::PrimeThumbChain()
{
	if (m_nThumbJobsInFlight == 0)
		OnThumbLoaded(nullptr, 0, 0);
}

void CItemsView::OnThumbLoaded(CDib* pDib, UINT uKey, int edge)
{
	const int nCount = static_cast<int>(m_items.size());

	if (nCount <= 0 || !m_pFolder)
		return;

	if (pDib && pDib->IsOpen())
	{
		for (int i = 0; i < nCount; i++)
		{
			if (m_items[i].uKey != uKey)
				continue;

			// The list is rebuilt from the folder map, so this is an item the
			// folder still owns rather than the address the job was posted with.
			CThumb* pThumb = m_items[i].pThumb;

			if (!pThumb->m_Dib.IsOpen())
				m_nLoaded++;
			pThumb->m_Dib.Attach(*pDib);
			pThumb->m_nDecodedEdge = edge;

			InvalidateItem(i);
			break;
		}
	}

	// Find the next item to load, starting from the top of the view.
	const int nFirstOnScreen = m_bDetail
		                           ? (m_pointScroll.y / m_nRowHeight)
		                           : (m_pointScroll.y / m_grid.rowHeight) * m_grid.columns;

	for (int i = 0; i < nCount; i++)
	{
		const int j = (i + nFirstOnScreen) % nCount;

		if (m_items[j].nAttemptedEdge >= m_nDecodeSize)
			continue;

		CThumb* pThumb = m_items[j].pThumb;
		CString strName;

		if (pThumb != nullptr && pThumb->m_nDecodedEdge < m_nDecodeSize && pThumb->IsLoadable() &&
			(!pThumb->m_Dib.IsOpen() || pThumb->m_Dib.OriginalSize() != pThumb->m_Dib.Size()) &&
			!(strName = DisplayName(j, SHGDN_FORPARSING | SHGDN_INCLUDE_NONFILESYS)).IsEmpty())
		{
			auto job = std::make_shared<CJobThumb>(this, strName, m_items[j].uKey, &m_nThumbOrder, m_nDecodeSize);
			job->SetOwner(m_alive);

			// Only mark the slot scanned if the job is actually queued. On a
			// failed post nothing will ever complete to clear it, and the chain
			// has no other producer.
			if (m_threadLoader.Post(job))
			{
				m_nThumbJobsInFlight += 1;
				m_items[j].nAttemptedEdge = m_nDecodeSize;
				break;
			}

			return;
		}

		m_items[j].nAttemptedEdge = m_nDecodeSize;
	}
}

//////////////////////////////////////////////////////////////////////////////
// Drawing

void CItemsView::OnDraw(CDibDC& ddc)
{
	CRect rectClient = ViewRect();
	const auto oldFont = ddc.SelectFont(m_font ? m_font : DefaultGuiFont());

	const int nCount = static_cast<int>(m_items.size());

	if (nCount == 0)
	{
		rectClient.OffsetRect(m_pointScroll);
		ddc.SetTextColor(RGB(255, 255, 255));
		ddc.DrawText(_T("Empty Folder"), -1, rectClient,
		             DT_NOCLIP | DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	}
	else
	{
		const CRect rectClip(ddc.GetClip() + m_pointScroll);

		ddc.SetTextColor(RGB(255, 255, 255));

		const int nRow = m_bDetail ? m_nRowHeight : m_grid.rowHeight;
		const int nPerRow = m_bDetail ? 1 : m_grid.columns;

		int nFirst = (rectClip.top / nRow) * nPerRow;
		int nLast = ((rectClip.bottom - 1) / nRow + 1) * nPerRow - 1;

		nFirst = max(nFirst, 0);
		nLast = min(nLast, nCount - 1);

		for (int i = nFirst; i <= nLast; i++)
		{
			if (m_bDetail)
				DrawDetailRow(ddc, i);
			else
				DrawThumbnail(ddc, i);
		}
	}

	if (oldFont) ddc.SelectFont(oldFont);
}

void CItemsView::DrawThumbnail(CDibDC& ddc, int i)
{
	CThumb* pThumb = m_items[i].pThumb;
	CRect rectThumb = ItemRect(i);
	const bool bFocus = (i == m_selection.Focus());

	if (pThumb->m_Dib.IsOpen())
	{
		CDib& image = pThumb->m_Dib;
		const CRect bounds = ThumbnailImageRect(rectThumb, image.OriginalSize(), m_grid.edge);
		if (bounds.Size() == image.Size())
			ddc.Draw(image, bounds.TopLeft());
		else
			ddc.Draw(image, bounds);
	}
	else
	{
		const CRect icon = ThumbnailImageRect(rectThumb, CSize(32, 32), m_grid.edge);
		// A shell icon cannot shrink with the cell.
		if (icon.Width() == 32)
			DrawItemIcon(ddc, i, icon.TopLeft(), false);
	}

	if (pThumb->m_ulAttribs & SFGAO_GHOSTED)
	{
		CRect r(0, 0, 1, 1);
		ddc.DrawText(_T("Hidden"), -1, r, DT_CALCRECT | DT_SINGLELINE);
		r = ThumbnailImageRect(rectThumb, r.Size(), m_grid.edge);

		ddc.Draw(r, 0x60000000);
		ddc.DrawText(_T("Hidden"), -1, r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
	}

	CRect caption(rectThumb.left + THUMB_PADDING, rectThumb.top + m_grid.CellWidth(),
	              rectThumb.right - THUMB_PADDING, rectThumb.bottom - THUMB_PADDING);
	ddc.Draw(caption, 0x40000000);
	ddc.DrawText(DisplayName(i), -1, caption, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

	if (m_selection.Contains(i))
		ddc.Draw(rectThumb, 0x40008080);

	if (bFocus)
	{
		::SetDCBrushColor(ddc, RGB(0, 255, 0));
		ddc.FrameRect(rectThumb, static_cast<HBRUSH>(::GetStockObject(DC_BRUSH)));
	}
}

void CItemsView::DrawDetailRow(CDibDC& ddc, int i)
{
	CThumb* pThumb = m_items[i].pThumb;
	CRect rectRow = ItemRect(i);

	if (m_selection.Contains(i))
		ddc.Draw(rectRow, 0x40008080);
	else
		ddc.Draw(rectRow, 0x30000000);

	if (i == m_selection.Focus())
	{
		::SetDCBrushColor(ddc, RGB(0, 255, 0));
		ddc.FrameRect(rectRow, static_cast<HBRUSH>(::GetStockObject(DC_BRUSH)));
	}

	DrawItemIcon(ddc, i, CPoint(rectRow.left + 3, rectRow.top + (rectRow.Height() - 16) / 2), true);

	// Name, then type, size and date right of it in fixed columns.
	const int cxName = 200;
	const int cxType = 120;
	const int cxSize = 80;

	CRect r(rectRow);
	r.left += 24;
	r.right = r.left + cxName;
	ddc.DrawText(DisplayName(i), -1, r, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);

	CShellItem item;
	item.Cat(m_pFolder->m_Item, *pThumb);

	r.left = r.right + 8;
	r.right = r.left + cxType;
	ddc.DrawText(ShellItemType(item), -1, r, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);

	r.left = r.right + 8;
	r.right = r.left + cxSize;

	if (!pThumb->IsFolder())
		ddc.DrawText(FormatFileSize(pThumb->m_sizeFile), -1, r,
		             DT_SINGLELINE | DT_VCENTER | DT_RIGHT);

	r.left = r.right + 8;
	r.right = rectRow.right;
	if (r.right > r.left)
		ddc.DrawText(FormatDate(pThumb->m_timeFile), -1, r,
		             DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
}

void CItemsView::DrawItemIcon(CDibDC& ddc, int i, CPoint point, bool bSmall)
{
	CThumb* pThumb = m_items[i].pThumb;

	CShellItem item;
	item.Cat(m_pFolder->m_Item, *pThumb);

	SHFILEINFO sfi = {0};

	const HIMAGELIST hImages = (HIMAGELIST)SHGetFileInfo(
		(LPCTSTR)static_cast<LPCITEMIDLIST>(item), 0, &sfi, sizeof(sfi),
		SHGFI_PIDL | SHGFI_SYSICONINDEX | (bSmall ? SHGFI_SMALLICON : SHGFI_LARGEICON));

	if (hImages == nullptr)
		return;

	UINT uStyle = ILD_TRANSPARENT;

	if (pThumb->m_ulAttribs & SFGAO_LINK)
		uStyle |= INDEXTOOVERLAYMASK(2);

	ImageList_Draw(hImages, sfi.iIcon, ddc.m_hDC, point.x, point.y, uStyle);
}
