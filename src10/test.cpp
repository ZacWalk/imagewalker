#include "StdAfx.h"

#include "FixedPoint.h"
#include "ImageView.h"
#include "Selection.h"
#include "Jobs.h"
#include "LoadAny.h"
#include <libfiles/encode.h>
#include "dibthumb.h"
#include "Thumb.h"
#include "AddressBar.h"
#include "window_screenshot.h"

#include "iw/commontests.h"
#include "iw/channelaveragetests.h"
#include "iw/exceptionunwindtests.h"

#include "resource.h"

// ImageWalker 1.0 -- run with /test.

IW_TEST(StartupSplashDecodesFromTheExecutable)
{
	CDib splash;
	IW_CHECK(splash.LoadResource(IDR_SPLASH, {0, 0}));
	if (splash.IsOpen()) IW_CHECK(splash.Size() == CSize(608, 200));
}

IW_TEST(ViewerFooterMakesRoomForStatusAtNarrowWidths)
{
	const INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_BAR_CLASSES};
	IW_CHECK(InitCommonControlsEx(&controls));
	CWindow parent(CreateWindowEx(0, _T("STATIC"), nullptr, WS_POPUP, 0, 0, 960, 400,
		nullptr, nullptr, GetModuleHandle(nullptr), nullptr));
	if (!parent)
	{
		IW_CHECK(parent.IsWindow());
		return;
	}
	CStatusBarCtrl status;
	CImageView view;
	const bool ready = status.Create(parent, CRect(0, 0, 900, 24), nullptr, WS_CHILD, 0, IDC_STATUS_BAR) &&
		view.Create(parent, CRect(0, 0, 180, 200), nullptr, WS_CHILD) && view.CreateFooterControls(status);
	IW_CHECK(ready);
	if (ready)
	{
		const CWindow zoom = status.GetDlgItem(CImageView::kZoomTrackId);
		const CWindow toolbar = status.GetDlgItem(CImageView::kToolBarId);
		const int controlsEnd = view.LayoutFooterControls(CRect(0, 0, 850, 24));
		CRect zoomBounds, toolbarBounds;
		zoom.GetWindowRect(&zoomBounds);
		toolbar.GetWindowRect(&toolbarBounds);
		status.ScreenToClient(&zoomBounds);
		status.ScreenToClient(&toolbarBounds);
		IW_CHECK(zoomBounds.left < 12 && zoomBounds.right > zoomBounds.left &&
			zoomBounds.right <= toolbarBounds.left && toolbarBounds.right <= controlsEnd && controlsEnd < 400);
		IW_CHECK((zoom.GetStyle() & WS_VISIBLE) && (toolbar.GetStyle() & WS_VISIBLE));
		const auto before = view.GetScaleType();
		status.SendMessage(WM_HSCROLL, MAKEWPARAM(TB_ENDTRACK, 0), reinterpret_cast<LPARAM>(zoom.m_hWnd));
		IW_CHECK(view.GetScaleType() == before);
		view.SetFullscreen(true);
		IW_CHECK(view.IsFit());
		view.Command(ID_MODE_ACTUALSIZE);
		IW_CHECK(view.IsActualSize());
		view.SetFullscreen(false);
		IW_CHECK(view.GetScaleType() == before);
		view.LayoutFooterControls(CRect(0, 0, 330, 24));
		IW_CHECK(!(zoom.GetStyle() & WS_VISIBLE) && (toolbar.GetStyle() & WS_VISIBLE));
		const int smallControlsEnd = view.LayoutFooterControls(CRect(0, 0, 110, 24));
		IW_CHECK(!(toolbar.GetStyle() & WS_VISIBLE) && smallControlsEnd == 0);

		CPreviewSelection selection{{{_T("C:\\samples\\orange.png"), nullptr, 2048, false, true}}, 1, 2048};
		view.Show(selection);
		CJobLoad loaded(&view, selection.items.front().strName, nullptr, nullptr);
		loaded.m_bLoaded = loaded.m_dib.Create(120, 80, 24, FALSE);
		IW_CHECK(loaded.m_bLoaded);
		if (loaded.m_bLoaded)
		{
			ZeroMemory(loaded.m_dib.GetBitmap(), loaded.m_dib.m_nStorageWidth * 80);
			view.OnLoaded(loaded);
			IW_CHECK(view.StatusText() == _T("orange.png  120 x 80  24-bit RGB  2 KB"));
			view.Show({});
			IW_CHECK(view.StatusText().IsEmpty());
		}
	}
	parent.DestroyWindow();
}

IW_TEST(FitFillsThePaneAfterPannableZoom)
{
	CWindow parent(CreateWindowEx(0, _T("STATIC"), nullptr, WS_POPUP, 0, 0, 240, 240,
		nullptr, nullptr, GetModuleHandle(nullptr), nullptr));
	CImageView view;
	const bool ready = parent && view.Create(parent, {0, 0, 180, 200}, nullptr, WS_CHILD);
	IW_CHECK(ready);
	if (ready)
	{
		// Fullscreen zoom is temporary; the test must not save a new preference.
		view.SetFullscreen(true);
		view.Command(ID_MODE_ACTUALSIZE);

		CJobLoad loaded(&view, "fit-fixture.bmp", nullptr, nullptr);
		loaded.m_bLoaded = loaded.m_dib.Create(640, 320, 24, FALSE);
		IW_CHECK(loaded.m_bLoaded);
		if (loaded.m_bLoaded)
		{
			ZeroMemory(loaded.m_dib.GetBitmap(), loaded.m_dib.m_nStorageWidth * 320);
			view.OnLoaded(loaded);
			IW_CHECK(view.ViewRect().Width() < 180 && view.ViewRect().Height() < 200);

			view.Command(ID_MODE_FITTOWINDOW);
			IW_CHECK(view.ViewRect().Size() == CSize(180, 200));
			IW_CHECK(view.m_sizeScroll == CSize(180, 90));
		}
	}
	if (parent) parent.DestroyWindow();
}

IW_TEST(FullscreenAndNextPhotoHaveDifferentAccelerators)
{
	const HACCEL table = ::LoadAcceleratorsA(::GetModuleHandleA(nullptr), MAKEINTRESOURCEA(IDR_MAINFRAME));
	if (!table) { IW_CHECK(table != nullptr); return; }
	std::vector<ACCEL> entries(::CopyAcceleratorTable(table, nullptr, 0));
	::CopyAcceleratorTable(table, entries.data(), static_cast<int>(entries.size()));
	const auto command = [&entries](WORD key, BYTE modifiers)
	{
		const auto entry = std::ranges::find_if(entries, [=](const ACCEL& item)
		{
			return item.key == key && (item.fVirt & (FVIRTKEY | FCONTROL | FSHIFT | FALT)) == modifiers;
		});
		return entry == entries.end() ? 0 : entry->cmd;
	};
	IW_CHECK_EQ(command(VK_RETURN, FVIRTKEY), ID_VIEW_ACTIVATE);
	IW_CHECK_EQ(command(VK_SPACE, FVIRTKEY), ID_VIEW_NEXTIMAGE);
	IW_CHECK_EQ(command(VK_SPACE, FVIRTKEY | FCONTROL | FSHIFT), ID_MODE_FULLSCREEN);
	IW_CHECK_EQ(command(VK_F11, FVIRTKEY), ID_MODE_FULLSCREEN);
	IW_CHECK_EQ(command(VK_LEFT, FVIRTKEY | FALT), ID_BROWSE_BACK);
}

IW_TEST(StartupFolderPrefersRememberedThenPictures)
{
	const auto com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	CShellItem remembered, pictures;
	IW_CHECK(remembered.Open(nullptr, CSIDL_DRIVES));
	if (pictures.Open(nullptr, CSIDL_MYPICTURES) && !remembered.IsNull())
	{
		std::vector<CShellItem> visits;
		const auto open = [&](const CShellItem& item)
		{
			visits.push_back(item);
			return item == pictures;
		};
		IW_CHECK(IW::OpenStartupFolder(nullptr, {}, open) && visits.size() == 1 && visits.front() == pictures);
		visits.clear();
		IW_CHECK(IW::OpenStartupFolder(nullptr, remembered, open) && visits.size() == 2 &&
			visits[0] == remembered && visits[1] == pictures);
		int calls = 0;
		IW_CHECK(IW::OpenStartupFolder(nullptr, remembered, [&](const CShellItem& item)
		{
			++calls;
			return item == remembered;
		}) && calls == 1);
	}
	if (SUCCEEDED(com)) CoUninitialize();
}

IW_TEST(AddressEditCommitsPastedFolderAndRestoresOnEscape)
{
	CAddressBar address;
	CShellItem folder;
	int navigations = 0;
	const auto navigate = [&](const CShellItem& item) { folder = item; ++navigations; return true; };
	const auto com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	const INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_USEREX_CLASSES};
	CWindow parent(CreateWindowEx(0, _T("STATIC"), nullptr, WS_POPUP, 0, 0, 200, 80,
		nullptr, nullptr, GetModuleHandle(nullptr), nullptr));
	IW_CHECK(parent && InitCommonControlsEx(&controls) && address.Create(parent, IDC_ADDRESS_COMBO));
	if (parent && address.IsWindow())
	{
		TCHAR path[MAX_PATH]{};
		const UINT length = GetWindowsDirectory(path, _countof(path));
		IW_CHECK(length > 0 && length < _countof(path));
		if (length > 0 && length < _countof(path))
		{
			address.SetAddress(_T("Previous folder"), 0);
			const CWindow edit(address.Edit());
			edit.SetWindowText(CString(_T("  \"")) + path + _T("\"  "));
			MSG message{edit, WM_KEYDOWN, VK_RETURN};
			IW_CHECK(address.HandleKey(message, folder, navigate));
			CString actual;
			IW_CHECK(folder.GetPath(actual) && actual.CompareNoCase(path) == 0);
			IW_CHECK_EQ(navigations, 1);
			address.SetAddress(path, 0);
			edit.SetWindowText(_T("Incomplete edit"));
			message.wParam = VK_ESCAPE;
			IW_CHECK(address.HandleKey(message, folder, navigate));
			edit.GetWindowText(actual);
			IW_CHECK(actual == path && navigations == 1);

			edit.SetWindowText("System32");
			message.wParam = VK_RETURN;
			IW_CHECK(address.HandleKey(message, folder, navigate));
			IW_CHECK(folder.GetPath(actual) && actual.CompareNoCase(CString(path) + "\\System32") == 0);
			IW_CHECK_EQ(navigations, 2);

			edit.SetWindowText("..");
			IW_CHECK(address.HandleKey(message, folder, navigate));
			IW_CHECK(folder.GetPath(actual) && actual.CompareNoCase(path) == 0);
			IW_CHECK_EQ(navigations, 3);
		}
	}
	if (parent) parent.DestroyWindow();
	if (SUCCEEDED(com)) CoUninitialize();
}

IW_TEST(ThumbnailFittingKeepsPixelsInsideTheirOwnImageArea)
{
	const CRect cell(THUMB_X, THUMB_Y, 2 * THUMB_X, 2 * THUMB_Y + 20);
	const auto wide = ThumbnailImageRect(cell, {4096, 2048});
	const auto tall = ThumbnailImageRect(cell, {100, 400});
	const auto icon = ThumbnailImageRect(cell, {16, 20});
	IW_CHECK(wide.Size() == CSize(IMAGE_X, IMAGE_Y / 2));
	IW_CHECK(tall.Size() == CSize(IMAGE_X / 4, IMAGE_Y));
	IW_CHECK(icon.Size() == CSize(16, 20));
	for (const auto& bounds : {wide, tall, icon})
	{
		IW_CHECK(bounds.left >= cell.left && bounds.right <= cell.right);
		IW_CHECK(bounds.top >= cell.top && bounds.bottom < cell.top + THUMB_Y);
		IW_CHECK(abs(bounds.CenterPoint().x - cell.CenterPoint().x) <= 1);
	}
}

IW_TEST(ThumbnailGridCentersOccupiedColumnsAndKeepsHitsCorrectWhenResized)
{
	CThumbnailGrid grid;
	grid.Layout(300, 11, 80, 20);
	IW_CHECK(grid.columns == 3 && grid.Cell(0).left == 18 && grid.Cell(2).right == 282);
	IW_CHECK(grid.Hit(grid.Cell(4).CenterPoint(), 11) == 4);
	IW_CHECK(grid.Hit(CPoint(17, 15), 11) == -1 && grid.Hit(CPoint(282, 15), 11) == -1);
	IW_CHECK(grid.Hit(grid.Cell(11).CenterPoint(), 11) == -1);

	grid.Layout(375, 2, 128, 20);
	IW_CHECK(abs(grid.Cell(0).left - (375 - grid.Cell(1).right)) <= 1);
	IW_CHECK(grid.Hit(grid.Cell(1).CenterPoint(), 2) == 1);

	grid.Layout(94, 11, 256, 20);
	IW_CHECK(grid.columns == 1 && grid.Cell(0).Width() == 94);
	IW_CHECK(grid.Hit(grid.Cell(8).CenterPoint(), 11) == 8);
}

IW_TEST(PaintBufferIsReusedAndClipsScrolledPixels)
{
	CDibDC paint;
	const bool created = paint.EnsurePaintBuffer({95, 70}) != FALSE;
	IW_CHECK(created);
	if (!created) return;
	::PatBlt(paint, 0, 0, 95, 70, BLACKNESS);
	const HBITMAP buffer = paint.GetDib().GetHBitmap();
	IW_CHECK(paint.EnsurePaintBuffer({103, 78}));
	IW_CHECK(paint.GetDib().GetHBitmap() == buffer);

	const CRect dirty(10, 11, 20, 21);
	paint.SetMapping({7, 9}, dirty);
	CRect logical(0, 0, 50, 50);
	paint.Draw(logical, 0xff0000ff);
	IW_CHECK(paint.GetDib().GetBitmap(10, 11)[0] == 255);
	IW_CHECK(paint.GetDib().GetBitmap(9, 11)[0] == 0);
	IW_CHECK(paint.GetDib().GetBitmap(20, 11)[0] == 0);

	paint.FillRect(&logical, reinterpret_cast<HBRUSH>(::GetStockObject(WHITE_BRUSH)));
	::GdiFlush();
	IW_CHECK(paint.GetDib().GetBitmap(10, 11)[2] == 255);
	IW_CHECK(paint.GetDib().GetBitmap(9, 11)[2] == 0);
	IW_CHECK(paint.GetDib().GetBitmap(20, 11)[2] == 0);
}

IW_TEST(VersionResourceNamesImageWalker10)
{
	DWORD ignored = 0;
	const DWORD size = GetFileVersionInfoSize(IW::Paths::ModulePath().c_str(), &ignored);
	IW_CHECK(size > 0);
	if (size == 0) return;
	std::vector<BYTE> bytes(size);
	IW_CHECK(GetFileVersionInfo(IW::Paths::ModulePath().c_str(), 0, size, bytes.data()));
	LPVOID value = nullptr;
	UINT length = 0;
	const BOOL queried = VerQueryValue(bytes.data(), _T("\\"), &value, &length);
	IW_CHECK(queried && length >= sizeof(VS_FIXEDFILEINFO));
	if (!queried || length < sizeof(VS_FIXEDFILEINFO)) return;
	const auto* fixed = static_cast<const VS_FIXEDFILEINFO*>(value);
	IW_CHECK_EQ(fixed->dwFileVersionMS, static_cast<DWORD>(MAKELONG(0, 1)));
	IW_CHECK_EQ(fixed->dwFileVersionLS, static_cast<DWORD>(0));
	IW_CHECK_EQ(fixed->dwProductVersionMS, fixed->dwFileVersionMS);
	IW_CHECK_EQ(fixed->dwProductVersionLS, fixed->dwFileVersionLS);

	// The names match the title the application shows for itself.
	TCHAR title[64] = {};
	IW_CHECK(::LoadString(nullptr, IDR_MAINFRAME, title, ARRAYSIZE(title)) > 0);
	IW_CHECK(_tcscmp(title, _T("ImageWalker 1.0")) == 0);
	TCHAR appTitle[64] = {};
	IW_CHECK(::LoadString(nullptr, IDS_APP_TITLE, appTitle, ARRAYSIZE(appTitle)) > 0);
	IW_CHECK(_tcscmp(appTitle, title) == 0);
	for (LPCTSTR field : {_T("FileDescription"), _T("ProductName"), _T("FileVersion"), _T("ProductVersion")})
	{
		TCHAR query[96] = {};
		_stprintf_s(query, _T("\\StringFileInfo\\040904b0\\%s"), field);
		const BOOL found = VerQueryValue(bytes.data(), query, &value, &length);
		IW_CHECK(found && length > 0);
		if (!found || length == 0) continue;
		const bool numeric = _tcscmp(field, _T("FileVersion")) == 0 || _tcscmp(field, _T("ProductVersion")) == 0;
		IW_CHECK(_tcscmp(static_cast<LPCTSTR>(value), numeric ? _T("1.0.0.0") : title) == 0);
	}
	for (const auto& field : {std::pair{_T("InternalName"), _T("imagewalker10")},
		std::pair{_T("OriginalFilename"), _T("imagewalker10.exe")}})
	{
		TCHAR query[96] = {};
		_stprintf_s(query, _T("\\StringFileInfo\\040904b0\\%s"), field.first);
		const BOOL found = VerQueryValue(bytes.data(), query, &value, &length);
		IW_CHECK(found && length > 0);
		if (found && length > 0)
			IW_CHECK(_tcscmp(static_cast<LPCTSTR>(value), field.second) == 0);
	}
}

IW_TEST(FixedPointConvertsBothWays)
{
	IW_CHECK_EQ(INT_TO_FIXED(1), 65536);
	IW_CHECK_EQ(FIXED_TO_INT(INT_TO_FIXED(37)), 37);
	IW_CHECK_EQ(DOUBLE_TO_FIXED(0.5), 32768);
	IW_CHECK(FIXED_TO_DOUBLE(DOUBLE_TO_FIXED(0.25)) == 0.25);
}

IW_TEST(NativePackedPixelsMaskEachFiveBitChannel)
{
	for (unsigned value = 0; value <= 0xffff; ++value)
	{
		const BYTE pixel[] = {static_cast<BYTE>(value), static_cast<BYTE>(value >> 8), 0, 0};
		const libfiles::pixel_rows rows{1, 1, libfiles::document_storage::bgr555, 4, false, pixel, {}, {}};
		std::uint32_t color{};
		libfiles::copy_bgra(rows, {&color, 1}, 1, false);
		IW_CHECK_EQ((color >> 16) & 255, ((value >> 10) & 31) << 3);
		IW_CHECK_EQ((color >> 8) & 255, ((value >> 5) & 31) << 3);
		IW_CHECK_EQ(color & 255, (value & 31) << 3);
	}
}

IW_TEST(ThreeByteCompositingPreservesOpaquePixelsAndAlphaEndpoints)
{
	for (unsigned alpha = 0; alpha < 256; ++alpha)
	{
		BYTE destination[] = {37, 111, 205, 0xCD};
		const BYTE original[] = {37, 111, 205};
		const UINT source = (alpha << 24) | 0x00807133u;
		Blend24(destination, source);
		for (unsigned channel = 0; channel < 3; ++channel)
		{
			const UINT value = (source >> (channel * 8)) & 255;
			const auto expected = static_cast<BYTE>(std::lround(
				(value * alpha + original[channel] * (255 - alpha)) / 255.0));
			IW_CHECK_EQ(destination[channel], expected);
		}
		IW_CHECK_EQ(destination[3], 0xCD);
	}
}

IW_TEST(PreviewScalingKeepsTransparentColorsOutOfVisiblePixels)
{
	CDib source;
	if (!source.Create(2, 1, 32, FALSE))
	{
		IW_CHECK(source.IsOpen());
		return;
	}
	const DWORD pixels[] = {0xffff0000, 0x0000ff00};
	std::memcpy(source.GetBitmap(), pixels, sizeof(pixels));
	CDibScale scaled;
	IW_CHECK(scaled.Scale(source, {1, 1}, nullptr));
	if (scaled.m_dib.IsOpen())
	{
		DWORD pixel{};
		std::memcpy(&pixel, scaled.m_dib.GetBitmap(), sizeof(pixel));
		IW_CHECK_EQ(pixel, 0x80ff0000u);
		IW_CHECK(scaled.m_dib.OriginalSize() == CSize(2, 1));
	}
}

IW_TEST(FixedPointRoundsToNearest)
{
	IW_CHECK_EQ(ROUND_FIXED_TO_INT(DOUBLE_TO_FIXED(2.4)), 2);
	IW_CHECK_EQ(ROUND_FIXED_TO_INT(DOUBLE_TO_FIXED(2.5)), 3);
	IW_CHECK_EQ(ROUND_FIXED_TO_INT(DOUBLE_TO_FIXED(2.6)), 3);
}

// These replaced 32-bit inline assembly, so the 64-bit arithmetic is worth
// pinning down.
IW_TEST(FixedPointMultipliesAndDivides)
{
	IW_CHECK_EQ(FixedMul(INT_TO_FIXED(6), INT_TO_FIXED(7)), INT_TO_FIXED(42));
	IW_CHECK_EQ(FixedMul(DOUBLE_TO_FIXED(0.5), INT_TO_FIXED(9)), DOUBLE_TO_FIXED(4.5));

	int product = 0;
	FIXED_MUL(DOUBLE_TO_FIXED(1.5), DOUBLE_TO_FIXED(1.5), product);
	IW_CHECK_EQ(product, DOUBLE_TO_FIXED(2.25));

	int quotient = 0;
	FIXED_DIV(INT_TO_FIXED(9), INT_TO_FIXED(2), quotient);
	IW_CHECK_EQ(quotient, DOUBLE_TO_FIXED(4.5));

	// A product beyond 32 bits before the shift back down is the case the
	// original assembly handled with edx:eax.
	IW_CHECK_EQ(FixedMul(INT_TO_FIXED(4000), INT_TO_FIXED(4000)), INT_TO_FIXED(16000000));
}

// A thumbnail job used to be built with no job number at all, which makes
// Exit() permanently false: the decode could not be cancelled and the browser's
// destructor waited for it to finish.
IW_TEST(ThumbnailJobsAreCancellable)
{
	LONG nJobNo = 0;
	CJobThumb job(NULL, _T("test.jpg"), 1234, &nJobNo);

	IW_CHECK(!job.Exit());

	::InterlockedIncrement(&nJobNo);

	IW_CHECK(job.Exit());
}

// The scale job used to hold the address of a CDib the image pane owns, and the
// UI thread reallocates that surface on the next load - while the worker is
// reading it.
IW_TEST(ScaleJobOwnsItsSource)
{
	LONG nJobNo = 0;

	CDib dib;
	IW_CHECK(dib.Create(4, 4, 24, FALSE));
	dib.GetBitmap(0)[0] = 0x5A;

	CJobScale job(NULL, dib, CSize(2, 2), &nJobNo, "Scaling %d%%", FALSE);

	IW_CHECK(dib.Create(64, 64, 24, FALSE));

	IW_CHECK_EQ(job.m_dibSrc.Width(), 4u);
	IW_CHECK_EQ(job.m_dibSrc.Height(), 4u);
	IW_CHECK_EQ(job.m_dibSrc.GetBitmap(0)[0], 0x5A);
}

IW_TEST(DibCopiesHandleEmptySourcesAndSelfAssignment)
{
	CDib empty;
	CDib copy(empty);
	IW_CHECK(!copy.IsOpen());

	IW_CHECK(copy.Create(4, 3, 24, FALSE));
	copy = empty;
	IW_CHECK(!copy.IsOpen());

	IW_CHECK(copy.Create(4, 3, 24, FALSE));
	if (!copy.IsOpen()) return;
	copy.GetBitmap(0)[0] = 0x5A;
	copy.m_sizeOriginal = CSize(400, 300);
	copy.m_strInfo = _T("source image");
	copy.CopyTo(copy);
	IW_CHECK(copy.OriginalSize() == CSize(400, 300));
	IW_CHECK(copy.m_strInfo == _T("source image"));
	IW_CHECK_EQ(copy.GetBitmap(0)[0], 0x5A);

	CDib independent(copy);
	IW_CHECK(independent.IsOpen());
	if (!independent.IsOpen()) return;
	IW_CHECK(independent.OriginalSize() == CSize(400, 300));
	independent.GetBitmap(0)[0] = 0xA5;
	IW_CHECK_EQ(copy.GetBitmap(0)[0], 0x5A);
}

IW_TEST(DibRecreationHonorsBitmapStorageMode)
{
	CDib dib;
	IW_CHECK(dib.Create(4, 3, 24, FALSE));
	if (!dib.IsOpen()) return;
	IW_CHECK(dib.GetHBitmap() == nullptr);
	IW_CHECK(dib.Create(4, 3, 24, TRUE));
	if (!dib.IsOpen()) return;
	IW_CHECK(dib.GetHBitmap() != nullptr);
	IW_CHECK(dib.Create(4, 3, 24, FALSE));
	if (!dib.IsOpen()) return;
	IW_CHECK(dib.GetHBitmap() == nullptr);
}

IW_TEST(DibRejectsUnsupportedPixelDepthsAndInvalidDimensions)
{
	CDib dib;
	for (const unsigned depth : {0u, 2u, 3u, 7u, 9u, 15u, 33u})
	{
		IW_CHECK(!dib.Create(4, 3, depth, FALSE));
		IW_CHECK(!dib.IsOpen());
	}
	IW_CHECK(!dib.Create(0, 3, 24, FALSE));
	IW_CHECK(!dib.Create(4, 0, 24, FALSE));
	IW_CHECK(!dib.Create(0x80000000u, 1, 1, FALSE));
	IW_CHECK(!dib.Create(1, 0x80000000u, 1, FALSE));
	IW_CHECK(!dib.IsOpen());
}

IW_TEST(ThumbnailHashCollisionsKeepDistinctShellItems)
{
	const BYTE firstBytes[] = {4, 0, 'A', 'b', 0, 0};
	const BYTE secondBytes[] = {4, 0, 'B', 'A', 0, 0};
	CShellItem first(reinterpret_cast<LPCITEMIDLIST>(firstBytes));
	CShellItem second(reinterpret_cast<LPCITEMIDLIST>(secondBytes));
	IW_CHECK_EQ(ShellHashKey(first), ShellHashKey(second));

	std::map<UINT, std::unique_ptr<CThumb>> thumbs;
	const UINT firstKey = CFolder::FindItemKey(thumbs, first);
	thumbs.emplace(firstKey, std::make_unique<CThumb>(first));
	const UINT secondKey = CFolder::FindItemKey(thumbs, second);
	IW_CHECK(firstKey != secondKey);
	thumbs.emplace(secondKey, std::make_unique<CThumb>(second));
	IW_CHECK_EQ(thumbs.size(), size_t{2});
	IW_CHECK_EQ(CFolder::FindItemKey(thumbs, first), firstKey);
	IW_CHECK_EQ(CFolder::FindItemKey(thumbs, second), secondKey);
}

// The selection is the only thing that says what the image pane shows, so its
// Explorer semantics are worth pinning down.

IW_TEST(SelectionStartsEmptyAndUnfocused)
{
	CSelection selection;
	selection.Reset(10);

	IW_CHECK_EQ(selection.Size(), 10);
	IW_CHECK_EQ(selection.Count(), 0);
	IW_CHECK_EQ(selection.Focus(), -1);
}

IW_TEST(SelectionPlainClickReplaces)
{
	CSelection selection;
	selection.Reset(10);

	selection.Click(3, false, false);
	selection.Click(7, false, false);

	IW_CHECK_EQ(selection.Count(), 1);
	IW_CHECK(selection.Contains(7));
	IW_CHECK(!selection.Contains(3));
	IW_CHECK_EQ(selection.Focus(), 7);
}

IW_TEST(SelectionControlClickToggles)
{
	CSelection selection;
	selection.Reset(10);

	selection.Click(2, false, false);
	selection.Click(5, true, false);

	IW_CHECK_EQ(selection.Count(), 2);
	IW_CHECK_EQ(selection.Focus(), 5);

	selection.Click(5, true, false);

	IW_CHECK_EQ(selection.Count(), 1);
	IW_CHECK(selection.Contains(2));

	// Focus stays on the item that was just clicked even though it is no longer
	// selected, which is what Explorer does.
	IW_CHECK_EQ(selection.Focus(), 5);
}

// Dragging the far end of a shift range back has to shrink it again; the
// original replaced this with a growing union.
IW_TEST(SelectionShiftRangeCanShrink)
{
	CSelection selection;
	selection.Reset(10);

	selection.Click(2, false, false);
	selection.Click(8, false, true);
	IW_CHECK_EQ(selection.Count(), 7);

	selection.Click(4, false, true);
	IW_CHECK_EQ(selection.Count(), 3);
	IW_CHECK(selection.Contains(2) && selection.Contains(3) && selection.Contains(4));
	IW_CHECK(!selection.Contains(5));
	IW_CHECK_EQ(selection.Focus(), 4);
}

IW_TEST(SelectionSelectAllAndInvert)
{
	CSelection selection;
	selection.Reset(5);

	selection.SelectAll();
	IW_CHECK_EQ(selection.Count(), 5);

	selection.Invert();
	IW_CHECK_EQ(selection.Count(), 0);

	selection.Click(1, false, false);
	selection.Invert();
	IW_CHECK_EQ(selection.Count(), 4);
	IW_CHECK(!selection.Contains(1));
}

IW_TEST(SelectionIgnoresOutOfRange)
{
	CSelection selection;
	selection.Reset(3);

	selection.Click(-1, false, false);
	selection.Click(3, false, false);

	IW_CHECK_EQ(selection.Count(), 0);
	IW_CHECK_EQ(selection.Focus(), -1);
	IW_CHECK(!selection.Contains(3));
}

namespace
{
	// All fixtures stay in owned memory: tests need no sample downloads, files
	// beside the executable, or cleanup after a failed assertion.
	const BYTE loaderColors[4][3] =
	{
		{ 229, 31, 67 }, { 17, 193, 83 }, { 43, 71, 211 }, { 241, 179, 23 }
	};

	void Append16(std::vector<BYTE>& bytes, unsigned value, bool bigEndian = false)
	{
		bytes.push_back(static_cast<BYTE>(bigEndian ? value >> 8 : value));
		bytes.push_back(static_cast<BYTE>(bigEndian ? value : value >> 8));
	}

	void Append32(std::vector<BYTE>& bytes, unsigned value, bool bigEndian = false)
	{
		Append16(bytes, bigEndian ? value >> 16 : value, bigEndian);
		Append16(bytes, bigEndian ? value : value >> 16, bigEndian);
	}

	std::vector<BYTE> PalettePixels(const std::vector<BYTE>& indices, bool alpha = false)
	{
		std::vector<BYTE> pixels;
		for (BYTE index : indices)
		{
			pixels.insert(pixels.end(), loaderColors[index], loaderColors[index] + 3);
			pixels.push_back(alpha ? static_cast<BYTE>(index * 85) : 255);
		}
		return pixels;
	}

	void CheckDecode(CLoadAny& loader, std::vector<BYTE>& bytes, unsigned width, unsigned height,
		unsigned bpp, const std::vector<BYTE>& expected, int tolerance = 0, BOOL thumbnail = FALSE)
	{
		IW_CHECK(!bytes.empty());
		if (bytes.empty())
			return;

		CDib dib;
		const BOOL loaded = loader.Load(&dib, bytes.data(), static_cast<DWORD>(bytes.size()), nullptr, thumbnail);
		IW_CHECK(loaded);
		IW_CHECK(dib.IsOpen());
		if (!loaded || !dib.IsOpen())
			return;

		IW_CHECK_EQ(dib.Width(), width);
		IW_CHECK_EQ(dib.Height(), height);
		IW_CHECK_EQ(dib.Bpp(), bpp);
		IW_CHECK_EQ(expected.size(), static_cast<size_t>(width) * height * 4);
		if (dib.Width() != width || dib.Height() != height || dib.Bpp() != bpp ||
			expected.size() != static_cast<size_t>(width) * height * 4)
			return;

		for (unsigned y = 0; y < height; ++y)
		{
			const BYTE* row = dib.GetBitmap(y);
			for (unsigned x = 0; x < width; ++x)
			{
				RGBQUAD pixel = {};
				if (bpp == 8)
					pixel = dib.GetColor()[row[x]];
				else
				{
					pixel.rgbBlue = row[x * (bpp / 8)];
					pixel.rgbGreen = row[x * (bpp / 8) + 1];
					pixel.rgbRed = row[x * (bpp / 8) + 2];
					pixel.rgbReserved = bpp == 32 ? row[x * 4 + 3] : 255;
				}
				const BYTE* want = expected.data() + (y * width + x) * 4;
				IW_CHECK(abs(static_cast<int>(pixel.rgbRed) - want[0]) <= tolerance);
				IW_CHECK(abs(static_cast<int>(pixel.rgbGreen) - want[1]) <= tolerance);
				IW_CHECK(abs(static_cast<int>(pixel.rgbBlue) - want[2]) <= tolerance);
				IW_CHECK_EQ(pixel.rgbReserved, want[3]);
			}
		}
	}

	void CheckRejected(CLoadAny& loader, std::vector<BYTE> bytes)
	{
		CDib dib;
		IW_CHECK(!loader.Load(&dib, bytes.data(), static_cast<DWORD>(bytes.size()), nullptr, FALSE));
	}

	std::vector<BYTE> MakeBmp(unsigned width, unsigned height, const std::vector<BYTE>& indices, bool indexed)
	{
		const unsigned bpp = indexed ? 8 : 24;
		const unsigned stride = ((width * (bpp / 8)) + 3) & ~3u;
		const unsigned offset = 54 + (indexed ? 256 * 4 : 0);
		std::vector<BYTE> bytes = { 'B', 'M' };
		Append32(bytes, offset + stride * height);
		Append32(bytes, 0);
		Append32(bytes, offset);
		Append32(bytes, 40);
		Append32(bytes, width);
		Append32(bytes, height);
		Append16(bytes, 1);
		Append16(bytes, bpp);
		Append32(bytes, BI_RGB);
		Append32(bytes, stride * height);
		bytes.resize(54, 0);
		if (indexed)
		{
			bytes.resize(offset, 0);
			for (unsigned i = 0; i < 4; ++i)
			{
				bytes[54 + i * 4] = loaderColors[i][2];
				bytes[54 + i * 4 + 1] = loaderColors[i][1];
				bytes[54 + i * 4 + 2] = loaderColors[i][0];
			}
		}
		// Nonzero padding catches a decoder treating the DIB stride as pixels.
		bytes.resize(offset + stride * height, 0xAD);
		for (unsigned y = 0; y < height; ++y)
			for (unsigned x = 0; x < width; ++x)
			{
				const BYTE index = indices[y * width + x];
				const unsigned pos = offset + (height - 1 - y) * stride + x * (bpp / 8);
				if (indexed)
					bytes[pos] = index;
				else
					for (unsigned channel = 0; channel < 3; ++channel)
						bytes[pos + channel] = loaderColors[index][2 - channel];
			}
		return bytes;
	}

	std::vector<BYTE> MakeGif(const std::vector<BYTE>& indices, bool interlaced)
	{
		std::vector<BYTE> bytes = { 'G', 'I', 'F', '8', '9', 'a', 3, 0, 5, 0, 0x81, 0, 0 };
		for (const auto& color : loaderColors)
			bytes.insert(bytes.end(), color, color + 3);
		const BYTE control[] = { 0x21, 0xF9, 4, 1, 0, 0, 2, 0 };
		bytes.insert(bytes.end(), control, control + sizeof(control));
		bytes.push_back(0x2C);
		Append16(bytes, 0);
		Append16(bytes, 0);
		Append16(bytes, 3);
		Append16(bytes, 5);
		bytes.push_back(interlaced ? 0x40 : 0);
		bytes.push_back(2);

		std::vector<BYTE> raster;
		unsigned bits = 0;
		const auto emit = [&](unsigned code)
		{
			for (unsigned bit = 0; bit < 3; ++bit, ++bits)
			{
				if (bits % 8 == 0)
					raster.push_back(0);
				raster.back() |= static_cast<BYTE>(((code >> bit) & 1) << (bits % 8));
			}
		};
		const unsigned interlaceRows[] = { 0, 4, 2, 1, 3 };
		for (unsigned row = 0; row < 5; ++row)
			for (unsigned x = 0; x < 3; ++x)
			{
				// Clear between literals keeps the independently generated LZW
				// stream at three bits per code, without an encoder dependency.
				emit(4);
				emit(indices[(interlaced ? interlaceRows[row] : row) * 3 + x]);
			}
		emit(5);
		bytes.push_back(static_cast<BYTE>(raster.size()));
		bytes.insert(bytes.end(), raster.begin(), raster.end());
		bytes.push_back(0);
		bytes.push_back(0x3B);
		return bytes;
	}

	std::vector<BYTE> MakePcx(const std::vector<BYTE>& indices)
	{
		std::vector<BYTE> bytes(128, 0);
		bytes[0] = 0x0A;
		bytes[1] = 5;
		bytes[2] = 1;
		bytes[3] = 8;
		bytes[8] = 3;
		bytes[10] = 2;
		bytes[65] = 1;
		bytes[66] = 4;
		bytes[68] = 1;
		for (size_t i = 0; i < indices.size();)
		{
			size_t count = 1;
			while (count < 4 - (i % 4) && indices[i + count] == indices[i])
				++count;
			const BYTE value = indices[i] == 3 ? 0xE3 : indices[i];
			if (count > 1 || value >= 0xC0)
				bytes.push_back(static_cast<BYTE>(0xC0 | count));
			bytes.push_back(value);
			i += count;
		}
		bytes.push_back(0x0C);
		const size_t palette = bytes.size();
		bytes.resize(palette + 256 * 3, 0);
		for (unsigned i = 0; i < 4; ++i)
			memcpy(bytes.data() + palette + (i == 3 ? 0xE3 : i) * 3, loaderColors[i], 3);
		return bytes;
	}

	std::vector<BYTE> MakePng(unsigned width, unsigned height, const std::vector<BYTE>& rgba)
	{
		auto bgra = rgba;
		for (size_t i = 0; i < bgra.size(); i += 4) std::swap(bgra[i], bgra[i + 2]);
		auto encoded = libfiles::encode_document({bgra.data(), width * 4, width, height, libfiles::pixel_format::bgra8},
		                                         libfiles::image_format::png);
		IW_CHECK(encoded.success);
		return std::move(encoded.data);
	}

	std::vector<BYTE> MakeTiff(const std::vector<BYTE>& rgba, bool bigEndian)
	{
		// Classic uncompressed RGB TIFF, one strip, with an explicit top-left
		// orientation. Both byte orders use the same top-down pixel payload.
		std::vector<BYTE> bytes(2, bigEndian ? 'M' : 'I');
		Append16(bytes, 42, bigEndian);
		Append32(bytes, 8, bigEndian);
		Append16(bytes, 11, bigEndian);
		const unsigned bitsOffset = 8 + 2 + 11 * 12 + 4;
		const unsigned pixelsOffset = bitsOffset + 6;
		const auto entry = [&](unsigned tag, unsigned type, unsigned count, unsigned value)
		{
			Append16(bytes, tag, bigEndian);
			Append16(bytes, type, bigEndian);
			Append32(bytes, count, bigEndian);
			if (type == 3 && count == 1)
			{
				Append16(bytes, value, bigEndian);
				Append16(bytes, 0, bigEndian);
			}
			else
				Append32(bytes, value, bigEndian);
		};
		entry(256, 4, 1, 3);
		entry(257, 4, 1, 2);
		entry(258, 3, 3, bitsOffset);
		entry(259, 3, 1, 1);
		entry(262, 3, 1, 2);
		entry(273, 4, 1, pixelsOffset);
		entry(274, 3, 1, 1);
		entry(277, 3, 1, 3);
		entry(278, 4, 1, 2);
		entry(279, 4, 1, 18);
		entry(284, 3, 1, 1);
		Append32(bytes, 0, bigEndian);
		for (unsigned i = 0; i < 3; ++i)
			Append16(bytes, 8, bigEndian);
		for (size_t i = 0; i < rgba.size(); i += 4)
			bytes.insert(bytes.end(), rgba.begin() + i, rgba.begin() + i + 3);
		return bytes;
	}

	std::vector<BYTE> JpegFixture(int id)
	{
		const auto module = ::GetModuleHandleA(nullptr);
		const auto found = ::FindResourceA(module, MAKEINTRESOURCEA(id), RT_RCDATA);
		const auto data = found ? ::LoadResource(module, found) : nullptr;
		const auto* bytes = data ? static_cast<const BYTE*>(::LockResource(data)) : nullptr;
		const auto length = found ? ::SizeofResource(module, found) : 0;
		IW_CHECK(bytes && length > 2);
		return bytes && length > 2 ? std::vector<BYTE>(bytes, bytes + length) : std::vector<BYTE>{};
	}
}

IW_TEST(BmpLoaderDecodesBottomUpRowsPaddingAndPalette)
{
	const std::vector<BYTE> indices = { 0, 1, 2, 3, 2, 1 };
	const auto rgba = PalettePixels(indices);
	CLoadAny loader;
	for (bool indexed : { false, true })
	{
		auto bytes = MakeBmp(3, 2, indices, indexed);
		for (BOOL thumbnail : { FALSE, TRUE })
			CheckDecode(loader, bytes, 3, 2, indexed ? 8 : 24, rgba, 0, thumbnail);
		CheckRejected(loader, std::vector<BYTE>(bytes.begin(), bytes.begin() + 53));
		auto truncated = bytes;
		truncated.pop_back();
		CheckRejected(loader, truncated);
		auto badOffset = bytes;
		memset(badOffset.data() + 10, 0xFF, 4);
		CheckRejected(loader, badOffset);
		CheckDecode(loader, bytes, 3, 2, indexed ? 8 : 24, rgba);
	}
}

IW_TEST(BmpLoaderThumbnailKeepsColorsAndAspectRatio)
{
	std::vector<BYTE> indices(160 * 80);
	std::vector<BYTE> thumbnailIndices(80 * 40);
	for (unsigned y = 0; y < 80; ++y)
		for (unsigned x = 0; x < 160; ++x)
			indices[y * 160 + x] = static_cast<BYTE>((x / 80) + 2 * (y / 40));
	for (unsigned y = 0; y < 40; ++y)
		for (unsigned x = 0; x < 80; ++x)
			thumbnailIndices[y * 80 + x] = static_cast<BYTE>((x / 40) + 2 * (y / 20));
	auto bytes = MakeBmp(160, 80, indices, false);
	CLoadAny loader;
	CheckDecode(loader, bytes, 80, 40, 32, PalettePixels(thumbnailIndices), 0, TRUE);
}

IW_TEST(GifLoaderDecodesInterlacedRowsAndTransparency)
{
	const std::vector<BYTE> indices = { 0, 1, 2, 3, 2, 1, 1, 3, 0, 2, 0, 3, 3, 1, 2 };
	auto rgba = PalettePixels(indices);
	for (size_t i = 0; i < indices.size(); ++i)
		if (indices[i] == 2)
			rgba[i * 4 + 3] = 0;
	CLoadAny loader;
	for (bool interlaced : { false, true })
	{
		auto bytes = MakeGif(indices, interlaced);
		for (BOOL thumbnail : { FALSE, TRUE })
			CheckDecode(loader, bytes, 3, 5, 8, rgba, 0, thumbnail);
		auto badVersion = bytes;
		badVersion[3] = '0';
		CheckRejected(loader, badVersion);
		CheckRejected(loader, std::vector<BYTE>(bytes.begin(), bytes.begin() + 12));
		CheckRejected(loader, std::vector<BYTE>(bytes.begin(), bytes.begin() + 20));
		CheckDecode(loader, bytes, 3, 5, 8, rgba);
	}
}

IW_TEST(GifLiteralAndStackOutputPreserveTheHighestByteIndices)
{
	std::vector<BYTE> bytes = {'G', 'I', 'F', '8', '9', 'a', 2, 0, 1, 0, 0x87, 0, 0};
	for (unsigned i = 0; i < 256; ++i)
		for (unsigned channel = 0; channel < 3; ++channel)
			bytes.push_back(static_cast<BYTE>(i));
	const BYTE descriptor[] = {0x2c, 0, 0, 0, 0, 2, 0, 1, 0, 0, 8};
	bytes.insert(bytes.end(), descriptor, descriptor + sizeof(descriptor));
	std::vector<BYTE> raster;
	unsigned bits = 0;
	for (unsigned code : {256u, 255u, 254u, 257u})
		for (unsigned bit = 0; bit < 9; ++bit, ++bits)
		{
			if (bits % 8 == 0) raster.push_back(0);
			raster.back() |= static_cast<BYTE>(((code >> bit) & 1) << (bits % 8));
		}
	bytes.push_back(static_cast<BYTE>(raster.size()));
	bytes.insert(bytes.end(), raster.begin(), raster.end());
	bytes.push_back(0);
	bytes.push_back(0x3b);
	CLoadAny loader;
	CheckDecode(loader, bytes, 2, 1, 8, {255, 255, 255, 255, 254, 254, 254, 255});
}

IW_TEST(PcxLoaderDecodesRleAndHighPaletteIndices)
{
	const std::vector<BYTE> indices = { 0, 0, 3, 3, 3, 3, 1, 1, 2, 2, 2, 3 };
	const auto rgba = PalettePixels(indices);
	auto bytes = MakePcx(indices);
	CLoadAny loader;
	for (BOOL thumbnail : { FALSE, TRUE })
		CheckDecode(loader, bytes, 4, 3, 8, rgba, 0, thumbnail);
	CheckRejected(loader, std::vector<BYTE>(bytes.begin(), bytes.begin() + 127));
	auto missingPalette = bytes;
	missingPalette.pop_back();
	CheckRejected(loader, missingPalette);
	auto zeroRun = bytes;
	zeroRun[128] = 0xC0;
	CheckRejected(loader, zeroRun);
	auto badEncoding = bytes;
	badEncoding[2] = 0;
	CheckRejected(loader, badEncoding);
	auto tooLarge = bytes;
	tooLarge[8] = tooLarge[10] = 0xFF;
	tooLarge[9] = tooLarge[11] = 0x7F;
	CheckRejected(loader, tooLarge);
	CheckDecode(loader, bytes, 4, 3, 8, rgba);
}

IW_TEST(PngLoaderPreservesRgbaRatherThanCompositingIt)
{
	const auto rgba = PalettePixels({ 0, 1, 2, 3, 2, 1 }, true);
	auto bytes = MakePng(3, 2, rgba);
	IW_CHECK(bytes.size() >= 33);
	if (bytes.size() < 33)
		return;
	CLoadAny loader;
	for (BOOL thumbnail : { FALSE, TRUE })
		CheckDecode(loader, bytes, 3, 2, 32, rgba, 0, thumbnail);
	CheckRejected(loader, std::vector<BYTE>(bytes.begin(), bytes.begin() + 8));
	CheckRejected(loader, std::vector<BYTE>(bytes.begin(), bytes.begin() + 32));
	auto badCrc = bytes;
	badCrc[29] ^= 1;
	CheckRejected(loader, badCrc);
	CheckDecode(loader, bytes, 3, 2, 32, rgba);
}

IW_TEST(TiffLoaderDecodesBothByteOrdersAndTopLeftRgb)
{
	const auto rgba = PalettePixels({ 0, 1, 2, 3, 2, 1 });
	CLoadAny loader;
	for (bool bigEndian : { false, true })
	{
		auto bytes = MakeTiff(rgba, bigEndian);
		for (BOOL thumbnail : { FALSE, TRUE })
			CheckDecode(loader, bytes, 3, 2, 32, rgba, 0, thumbnail);
		CheckRejected(loader, std::vector<BYTE>(bytes.begin(), bytes.begin() + 7));
		CheckRejected(loader, std::vector<BYTE>(bytes.begin(), bytes.begin() + 8));
		auto badStrip = bytes;
		memset(badStrip.data() + 10 + 5 * 12 + 8, 0xFF, 4);
		CheckRejected(loader, badStrip);
		CheckDecode(loader, bytes, 3, 2, 32, rgba);
	}
}

IW_TEST(JpegLoaderDecodesColorAndGrayscaleAndRecoversAfterBadHeader)
{
	CLoadAny loader;
	for (bool grayscale : { false, true })
	{
		std::vector<BYTE> rgba;
		// Constant, MCU-aligned blocks avoid edge ringing. A small tolerance
		// accounts for JPEG's lossy color conversion and the fast IDCT.
		for (unsigned y = 0; y < 16; ++y)
			for (unsigned x = 0; x < 16; ++x)
			{
				const unsigned index = (x / 8) + 2 * (y / 8);
				if (grayscale)
				{
					const BYTE value = static_cast<BYTE>(32 + index * 64);
					rgba.insert(rgba.end(), 3, value);
				}
				else
				{
					rgba.insert(rgba.end(), loaderColors[index], loaderColors[index] + 3);
				}
				rgba.push_back(255);
			}
		auto bytes = JpegFixture(grayscale ? ID_TEST_JPEG_GRAY : ID_TEST_JPEG_RGB);
		for (BOOL thumbnail : { FALSE, TRUE })
			CheckDecode(loader, bytes, 16, 16, grayscale ? 8 : 24, rgba, 3, thumbnail);
		CheckRejected(loader, { 0xFF, 0xD8, 0xFF, 0xD9 });
		CheckRejected(loader, { 0xFF, 0xD8, 0xFF, 0xE0, 0, 1 });
		CheckDecode(loader, bytes, 16, 16, grayscale ? 8 : 24, rgba, 3);
	}
}

IW_TEST(ImageLoaderRejectsEmptyAndUnknownSignatures)
{
	CLoadAny loader;
	CDib dib;
	IW_CHECK(!loader.Load(&dib, static_cast<LPBYTE>(nullptr), 0, nullptr, FALSE));
	CheckRejected(loader, { 'B' });
	CheckRejected(loader, std::vector<BYTE>(16, 0xAA));
}

IW_TEST(JpegLoaderShowsPixelsAsStoredWithoutExifRotation)
{
	auto bytes = JpegFixture(ID_TEST_JPEG_ORIENTATION);
	if (bytes.empty()) return;
	// Exif orientation 6, which a decoder that turned photos upright would show as 16x32.
	const BYTE exif[] = { 0xFF, 0xE1, 0, 34, 'E', 'x', 'i', 'f', 0, 0,
		'I', 'I', 42, 0, 8, 0, 0, 0,
		1, 0, 0x12, 0x01, 3, 0, 1, 0, 0, 0, 6, 0, 0, 0,
		0, 0, 0, 0 };
	bytes.insert(bytes.begin() + 2, std::begin(exif), std::end(exif));
	CLoadAny loader;
	for (BOOL thumbnail : { FALSE, TRUE })
	{
		CDib dib;
		IW_CHECK(loader.Load(&dib, bytes.data(), static_cast<DWORD>(bytes.size()), nullptr, thumbnail));
		IW_CHECK_EQ(dib.Width(), 32u);
		IW_CHECK_EQ(dib.Height(), 16u);
	}
}

IW_TEST(FileDecodeBoundsPreviewAndDoesNotChangeTheSource)
{
	TCHAR temp[MAX_PATH]{};
	const auto length = ::GetTempPathA(_countof(temp), temp);
	GUID unique{};
	if (!length || length >= _countof(temp) || FAILED(::CoCreateGuid(&unique)))
	{
		IW_CHECK(false);
		return;
	}
	wchar_t id[40]{};
	::StringFromGUID2(unique, id, _countof(id));
	const CString path = CString(temp) + NarrowText(id) + ".jpg";
	const auto bytes = JpegFixture(ID_TEST_JPEG_CMYK_LARGE);
	if (bytes.empty()) return;

	struct Fixture
	{
		HANDLE file;
		~Fixture() { if (file != INVALID_HANDLE_VALUE) ::CloseHandle(file); }
	} fixture{::CreateFileA(path, GENERIC_READ | GENERIC_WRITE | DELETE, FILE_SHARE_READ | FILE_SHARE_DELETE,
	                        nullptr, CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr)};
	IW_CHECK(fixture.file != INVALID_HANDLE_VALUE);
	if (fixture.file == INVALID_HANDLE_VALUE) return;
	DWORD written{};
	IW_CHECK(::WriteFile(fixture.file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) &&
	         written == bytes.size());
	IW_CHECK(::FlushFileBuffers(fixture.file));

	CDib dib;
	CLoadAny loader;
	IW_CHECK(loader.Load(&dib, path, nullptr, TRUE, 64));
	if (dib.IsOpen())
	{
		IW_CHECK(dib.Size() == CSize(64, 64));
		IW_CHECK(dib.OriginalSize() == CSize(96, 96));
	}

	std::vector<BYTE> after(bytes.size());
	DWORD read{};
	IW_CHECK(::SetFilePointerEx(fixture.file, {}, nullptr, FILE_BEGIN) &&
	         ::ReadFile(fixture.file, after.data(), static_cast<DWORD>(after.size()), &read, nullptr) &&
	         read == bytes.size() && after == bytes);
}

IW_TEST(ScreenshotKeepsAnOccupiedOutput)
{
	wchar_t temp[MAX_PATH]{};
	const auto length = ::GetTempPathW(_countof(temp), temp);
	if (!length || length >= _countof(temp))
	{
		IW_CHECK(false);
		return;
	}
	const auto saved = iw::publication::temporary_sibling(std::filesystem::path(temp) / L"capture.png", L"iw10-test-");
	IW_CHECK(!saved.empty());
	if (saved.empty()) return;
	const bool created = iw::screenshot::write_new(saved, {1, 2, 3});
	IW_CHECK(created);
	if (!created) return;
	IW_CHECK(!iw::screenshot::write_new(saved, {4, 5}));
	const iw::publication::File file(::CreateFileW(saved.c_str(), GENERIC_READ | DELETE, 0,
	                                              nullptr, OPEN_EXISTING, FILE_FLAG_DELETE_ON_CLOSE, nullptr));
	BYTE bytes[4]{};
	DWORD read{};
	IW_CHECK(file && ::ReadFile(file.get(), bytes, sizeof(bytes), &read, nullptr) &&
	         read == 3 && bytes[0] == 1 && bytes[1] == 2 && bytes[2] == 3);
}

IW_TEST(NameSortOrdersShellItemsAlphabetically)
{
	// CompareIDs returns "before" as 0xFFFF in the HRESULT's code field; read as
	// an unsigned code it is "after", and sorting by name changed nothing.
	TCHAR szTemp[MAX_PATH] = {};
	IW_CHECK(GetTempPath(MAX_PATH, szTemp) != 0);
	const HRESULT hrCom = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	CString strFolder;
	strFolder.Format(_T("%siw10-sort-%lu"), szTemp, GetCurrentProcessId());
	IW_CHECK(CreateDirectory(strFolder, nullptr) != FALSE);
	for (LPCTSTR szName : { _T("b.txt"), _T("c.txt"), _T("a.txt") })
	{
		const HANDLE file = CreateFile(strFolder + _T("\\") + szName, GENERIC_WRITE, 0, nullptr, CREATE_NEW,
		                               FILE_ATTRIBUTE_NORMAL, nullptr);
		IW_CHECK(file != INVALID_HANDLE_VALUE);
		if (file != INVALID_HANDLE_VALUE)
			CloseHandle(file);
	}
	{
		CShellDesktop desktop;
		CShellItem item;
		const HRESULT hr = ShellParseDisplayName(desktop, nullptr, strFolder, item);
		IW_CHECK(SUCCEEDED(hr));
		if (SUCCEEDED(hr))
		{
			CFolder folder(nullptr, item);
			std::vector<CThumb*> thumbs;
			for (auto& entry : folder.m_mapThumbs)
				thumbs.push_back(entry.second.get());
			IW_CHECK_EQ(thumbs.size(), static_cast<size_t>(3));
			std::stable_sort(thumbs.begin(), thumbs.end(), [&folder](CThumb* a, CThumb* b)
			{
				return CThumb::CompareName(folder, *a, *b) < 0;
			});
			CString names;
			for (CThumb* thumb : thumbs)
				names += folder.GetDisplayNameOf(*thumb, SHGDN_INFOLDER | SHGDN_FORPARSING) + _T(";");
			IW_CHECK(names == _T("a.txt;b.txt;c.txt;"));
			if (thumbs.size() == 3)
			{
				IW_CHECK(CThumb::CompareName(folder, *thumbs[0], *thumbs[1]) < 0);
				IW_CHECK(CThumb::CompareName(folder, *thumbs[1], *thumbs[0]) > 0);
				IW_CHECK_EQ(CThumb::CompareName(folder, *thumbs[2], *thumbs[2]), 0);
			}
		}
	}
	for (LPCTSTR szName : { _T("a.txt"), _T("b.txt"), _T("c.txt") })
		DeleteFile(strFolder + _T("\\") + szName);
	IW_CHECK(RemoveDirectory(strFolder) != FALSE);
	if (SUCCEEDED(hrCom))
		CoUninitialize();
}

IW_TEST(JpegLoaderConvertsAdobeCmykToRgb)
{
	// libjpeg marks a CMYK file with Adobe's APP14 marker, so its samples are read
	// inverted: each is the light an ink lets through. Full K keeps C, M and Y exact.
	const BYTE samples[4][4] = { {255, 0, 0, 255}, {0, 255, 0, 255}, {0, 0, 255, 255}, {255, 255, 255, 128} };
	CLoadAny loader;
	// 16 pixels square decodes directly. A 96-pixel thumbnail is wider than IMAGE_X but too
	// small for libjpeg's own DCT scaling, so it goes through the scaling iterator; its
	// quadrants meet on an 8-pixel block edge that maps exactly onto a thumbnail pixel edge.
	for (unsigned size : { 16u, 96u })
	{
		auto bytes = JpegFixture(size == 16 ? ID_TEST_JPEG_CMYK : ID_TEST_JPEG_CMYK_LARGE);
		for (BOOL thumbnail : { FALSE, TRUE })
		{
			const bool scaled = thumbnail && size > IMAGE_X;
			const unsigned out = scaled ? IMAGE_X : size;
			std::vector<BYTE> rgba;
			for (unsigned y = 0; y < out; ++y)
				for (unsigned x = 0; x < out; ++x)
				{
					const BYTE* s = samples[(x / (out / 2)) + 2 * (y / (out / 2))];
					for (int channel = 0; channel < 3; ++channel)
						rgba.push_back(static_cast<BYTE>(s[channel] * s[3] / 255));
					rgba.push_back(255);
				}
			CheckDecode(loader, bytes, out, out, scaled ? 32 : 24, rgba, 3, thumbnail);
		}
	}
}
