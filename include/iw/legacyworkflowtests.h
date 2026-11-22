#pragma once

#include <winver.h>
#include "ImagingDataObject.h"
#include "UtilMd5.h"
#include "ViewDropTarget.h"
#include "ViewEdit.h"
#include "ViewModelSearch.h"
#include "AppThreadFolder.h"
#include "iw/formatenum.h"
#include "iw/zstream.h"

// Included by the three 2.x suites after their application headers.
namespace
{
	struct WorkflowCompressedBytes
	{
		std::vector<BYTE> bytes;
		size_t position = 0;

		void Write(LPCVOID data, DWORD count, LPDWORD written)
		{
			const auto* first = static_cast<const BYTE*>(data);
			bytes.insert(bytes.end(), first, first + count);
			if (written) *written = count;
		}

		void Read(LPVOID data, DWORD count, LPDWORD read)
		{
			const size_t length = (std::min)(static_cast<size_t>(count), bytes.size() - position);
			if (length) memcpy(data, bytes.data() + position, length);
			position += length;
			if (read) *read = static_cast<DWORD>(length);
		}
	};

	struct WorkflowTestFiles
	{
		CString folder;
		std::vector<CString> files;

		WorkflowTestFiles()
		{
			folder.Format(_T("%siw-workflow-%lu-%lu"), IW::Paths::ModuleDir().c_str(),
			              GetCurrentProcessId(), GetTickCount());
			if (!CreateDirectory(folder, nullptr)) folder.Empty();
		}

		CString Path(LPCTSTR name)
		{
			const CString path = IW::Path::Combine(folder, name);
			files.push_back(path);
			return path;
		}

		~WorkflowTestFiles()
		{
			for (const auto &path : files) DeleteFile(path);
			if (!folder.IsEmpty()) RemoveDirectory(folder);
		}
	};

	bool WriteWorkflowMarker(const CString &path, char marker)
	{
		IW::CFile file;
		return file.OpenForWrite(path) && file.Write(&marker, sizeof(marker));
	}

	char ReadWorkflowMarker(const CString &path)
	{
		IW::CFile file;
		char marker = 0;
		DWORD read = 0;
		return file.OpenForRead(path) && file.Read(&marker, sizeof(marker), &read) && read == sizeof(marker) ?
			marker : 0;
	}

	struct WorkflowDialogLifetime
	{
		HWND window;
		~WorkflowDialogLifetime() { if (IsWindow(window)) DestroyWindow(window); }
	};

	struct WorkflowCommandTarget
	{
		DWORD owned{};
		std::vector<DWORD> seen;
		bool InvokeCommand(DWORD id)
		{
			seen.push_back(id);
			return id == owned;
		}
	};

	struct WorkflowCommandFrame : CommandFrameBase<WorkflowCommandFrame>
	{
		WorkflowCommandTarget *_pView{};
		WorkflowCommandTarget frameCommands;
		bool InvokeCommand(DWORD id) { return frameCommands.InvokeCommand(id); }
	};

	void ConfigureWorkflowPrint(CPrintFolder &print)
	{
		print.m_rcMargin.SetRectEmpty();
		print.m_bShowHeader = false;
		print.m_bShowFooter = false;
		print.m_bShowPageNumbers = false;
		print.m_bPrintSelected = false;
		print.m_bPrintOnePerPage = false;
		print.m_nPrintRotateBest = 0;
		print.m_annotations.RemoveAll();
		print.m_clrBackGround = RGB(255, 255, 255);
		print._sizeRowsColumns = CSize(2, 2);
	}

	IW::Image WorkflowSolidImage(COLORREF value)
	{
		IW::Image image;
		IW::Page &page = image.CreatePage(32, 32, IW::PixelFormat::PF24);
		auto lock = page.GetSurfaceLock();
		std::vector<COLORREF> line(32, value);
		for (int y = 0; y < 32; ++y) lock->SetLine(line.data(), y, 0, 32);
		return image;
	}

	struct WorkflowImageCoupling : Coupling
	{
		std::vector<CString> paths;
		IW::RefPtr<CImageLoad> request;
		CString selected;
		bool allowChange = true;
		int changeRequests = 0;

		CString GetItemPath(int index) const override { return paths.at(index); }
		HWND GetImageWindow() override { return nullptr; }
		ITEMLIST GetItemList() const override { return {}; }
		CSize GetThumbnailSize() const override { return CSize(160, 160); }
		bool HasSearchSpec() override { return false; }
		bool IsFullScreen() const override { return false; }
		bool IsItemFolder(long) const override { return false; }
		bool SelectFolderItem(const CString &path) override { selected = path; return true; }
		bool ViewFullScreen(bool) override { return false; }
		int GetFocusItem() const override { return 0; }
		int GetItemCount() const override { return static_cast<int>(paths.size()); }
		void AfterCopy(bool) override {}
		void BeginImageFade(int) override {}
		bool CanChangeImage() override { ++changeRequests; return allowChange; }
#if MINOR_VERSION < 3
		bool CanShowGutterItem(DWORD) const override { return false; }
#endif
		void Command(WORD) override {}
		void NewImage(bool) override {}
		void ResetThumbThread() override {}
		void ResetWaitForFolderToChangeThread() override {}
		void SetFocusItem(int, bool) override {}
		void SetStatusText(const CString &) override {}
		void SetStopLoading() override {}
		void ShowImage(CImageLoad *info) override { request = info; }
		void ShowSearchPane() override {}
		void StartSearch(Search::Type) override {}
		void SignalImageLoadComplete(CImageLoad *) override {}
		void SignalSearching(UINT_PTR) override {}
		void SignalSearchingComplete(UINT_PTR) override {}
		void SignalSearchingFolder(const CString &, UINT_PTR) override {}
		void SignalThumbnailing() override {}
		void SignalThumbnailingComplete() override {}
		void SortOrderChanged(int) override {}
		void StartSearchThread(Search::Type, const Search::Spec &) override {}
		void StartSearching() override {}
		void StopLoadingThumbs() override {}
		void TrackPopupMenu(HMENU, UINT, int, int) override {}
		void UpdateStatusText() override {}
		void UpdateToolbarsShown() override {}
	};

	struct WorkflowBitmapDrop : CDropTargetImpl<WorkflowBitmapDrop>
	{
		using CDropTargetImpl::Drop;
		ImageState &state;
		explicit WorkflowBitmapDrop(ImageState &image) : state(image) {}
		bool LoadImageFromHGlobal(HGLOBAL bitmap) { return state.ImportBitmap(bitmap); }
	};
}

IW_TEST(BitmapDropPreservesTheCurrentDraftWhenRejected)
{
	WorkflowImageCoupling coupling;
	State state(&coupling, false);
	const CString path = _T("draft.bmp");
	auto original = WorkflowSolidImage(IW::RGBA(200, 20, 40));
	state.Image.SetImage(original, path);
	auto draft = WorkflowSolidImage(IW::RGBA(30, 200, 40));
	state.Image.SetImageWithHistory(draft, _T("Edit"));

	const HGLOBAL invalid = ::GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, 1);
	IW_CHECK(invalid != nullptr);
	if (invalid)
	{
		IW_CHECK(!state.Image.ImportBitmap(invalid));
		::GlobalFree(invalid);
	}
	IW_CHECK_EQ(coupling.changeRequests, 0);

	CComPtr<CImageDataObject> data = IW::CreateComObject<CImageDataObject>();
	data->Cache(WorkflowSolidImage(IW::RGBA(20, 40, 200)));
	WorkflowBitmapDrop target(state.Image);
	coupling.allowChange = false;
	DWORD effect = DROPEFFECT_COPY;
	IW_CHECK_EQ(target.Drop(data, 0, POINTL{}, &effect), S_OK);
	IW_CHECK_EQ(effect, DWORD{DROPEFFECT_NONE});
	IW_CHECK_EQ(coupling.changeRequests, 1);
	IW_CHECK(state.Image.IsDirty() && state.Image.CanUndo() && state.Image.GetImageFileName() == path);
	COLORREF pixel{};
	state.Image.GetImage().GetFirstPage().GetSurfaceLock()->GetLine(&pixel, 0, 0, 1);
	IW_CHECK_EQ(IW::GetR(pixel), 30u);

	state.Image.UndoWithoutPrompt();
	coupling.allowChange = true;
	effect = DROPEFFECT_COPY;
	IW_CHECK_EQ(target.Drop(data, 0, POINTL{}, &effect), S_OK);
	IW_CHECK_EQ(effect, DWORD{DROPEFFECT_COPY});
	IW_CHECK(state.Image.IsImageReady() && state.Image.GetImageFileName().IsEmpty());
	state.Image.GetImage().GetFirstPage().GetSurfaceLock()->GetLine(&pixel, 0, 0, 1);
	IW_CHECK_EQ(IW::GetR(pixel), 20u);
}

IW_TEST(SupersededSearchCannotPublishRowsOrNotifications)
{
	WorkflowImageCoupling coupling;
	State state(&coupling, false);
	DecodeThumbsThread worker(&coupling, state);
	const auto folder = state.Folder.GetFolder();
	const auto stale = IW::FolderItem::CreateTestItem();
	const auto oldSearch = worker.StartSearch(Search::Current, Search::Spec(_T("old")));
	IW_CHECK(worker.PublishSearchResult(oldSearch, folder, stale));

	IW_CHECK(worker.Abort());
	folder->DeleteAllThumbs();
	const auto newSearch = worker.StartSearch(Search::Current, Search::Spec(_T("new")));
	IW_CHECK(!worker.IsCurrentSearch(oldSearch));
	IW_CHECK(!worker.PublishSearchResult(oldSearch, folder, stale));
	IW_CHECK_EQ(folder->GetItemCount(), 0);

	const auto fresh = IW::FolderItem::CreateTestItem();
	IW_CHECK(worker.IsCurrentSearch(newSearch));
	IW_CHECK(worker.PublishSearchResult(newSearch, folder, fresh));
	IW_CHECK(folder->GetItemCount() == 1 && folder->GetItem(0).p == fresh.p);

	IW_CHECK(worker.Abort());
	IW_CHECK(!worker.IsCurrentSearch(newSearch));
	IW_CHECK(!worker.PublishSearchResult(newSearch, folder, fresh));
	IW_CHECK_EQ(folder->GetItemCount(), 1);
}

IW_TEST(ViewerIgnoresPostedStaleLoadsAndContinuesPastAFailedCurrentLoad)
{
	WorkflowTestFiles fixture;
	IW_CHECK(!fixture.folder.IsEmpty());
	if (fixture.folder.IsEmpty()) return;

	WorkflowImageCoupling coupling;
	coupling.paths = {fixture.Path(_T("old.bmp")), fixture.Path(_T("not-an-image.txt")),
		fixture.Path(_T("current.bmp"))};
	State state(&coupling, false);

	state.Image.LoadImage(0, true);
	auto posted = coupling.request;
	IW_CHECK(posted != nullptr);
	if (!posted) return;
	posted->_image = WorkflowSolidImage(IW::RGBA(200, 20, 40));
	state.Image.StepImage(1);
	auto failed = coupling.request;
	IW_CHECK(failed != nullptr && failed->_path == coupling.paths[1]);
	if (!failed) return;

	state.Image.OnLoadComplete(posted);
	IW_CHECK(!state.Image.IsImageShown() && coupling.selected.IsEmpty());
	state.Image.OnLoadComplete(failed);
	auto current = coupling.request;
	IW_CHECK(current != nullptr && current->_path == coupling.paths[2]);
	if (!current) return;
	current->_image = WorkflowSolidImage(IW::RGBA(20, 200, 40));
	state.Image.OnLoadComplete(current);
	IW_CHECK(state.Image.IsImageReady() && state.Image.GetImageFileName() == coupling.paths[2]);
	IW_CHECK(coupling.selected == _T("current.bmp"));

	state.Image.LoadImage(0, true);
	auto stopped = coupling.request;
	if (!stopped) return;
	stopped->_image = posted->_image;
	state.Image.StopLoading();
	state.Image.OnLoadComplete(stopped);
	IW_CHECK(state.Image.GetImageFileName() == coupling.paths[2]);
}

IW_TEST(LoadedPhotoKeepsTheSaveGuardAndUndoRemainsUnsavedAfterCommit)
{
	WorkflowTestFiles fixture;
	IW_CHECK(!fixture.folder.IsEmpty());
	if (fixture.folder.IsEmpty()) return;

	const auto path = fixture.Path(_T("photo.bmp"));
	const auto backup = fixture.Path(_T("photo-original.bmp"));
	const auto secondBackup = fixture.Path(_T("photo-original (2).bmp"));
	WorkflowImageCoupling coupling;
	coupling.paths = {path};
	State state(&coupling, false);
	CLoadAny loader(state.Loaders);
	auto initial = WorkflowSolidImage(IW::RGBA(200, 20, 40));
	const bool written = loader.SaveImage(path, initial, App.Settings.Codec, IW::CNullStatus::Instance);
	IW_CHECK(written);
	if (!written) return;

	state.Image.LoadImage(0, false);
	auto request = coupling.request;
	const bool loaded = request && request->Load(loader, IW::CNullStatus::Instance);
	IW_CHECK(loaded);
	if (!loaded) return;
	state.Image.OnLoadComplete(request);
	auto edit = WorkflowSolidImage(IW::RGBA(30, 200, 40));
	state.Image.SetImageWithHistory(edit, _T("Edit"));

	IW::CFileWriteStatus status;
	const CString key = CLoadBmp::_GetKey();
	const bool saved = state.Image.SaveFile(path, key, &status, true);
	IW_CHECK(saved && !state.Image.IsDirty());
	if (!saved) return;
	const auto readRed = [&loader](const CString &file)
	{
		IW::Image image;
		if (!loader.LoadImage(file, image, IW::CNullStatus::Instance) || image.IsEmpty())
			return 0u;
		COLORREF pixel{};
		image.GetFirstPage().GetSurfaceLock()->GetLine(&pixel, 0, 0, 1);
		return static_cast<unsigned>(IW::GetR(pixel));
	};
	IW_CHECK_EQ(readRed(path), 30u);
	IW_CHECK_EQ(readRed(backup), 200u);

	state.Image.UndoWithoutPrompt();
	IW_CHECK(state.Image.IsDirty());
	const bool savedUndo = state.Image.SaveFile(path, key, &status, true);
	if (!savedUndo)
		IW::Test::Detail::Print("\r\n    Save failed: %ls", status.error.GetString());
	IW_CHECK(savedUndo);
	IW_CHECK_EQ(readRed(path), 200u);
	IW_CHECK_EQ(readRed(secondBackup), 30u);

	state.Image.SetImageWithHistory(edit, _T("Edit again"));
	// Preserve file identity, size and mtime, but change the bytes as a
	// metadata editor that preserves the modified timestamp might.
	{
		const iw::publication::File other(::CreateFile(path, GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr));
		DWORD written = 0;
		FILETIME modified{};
		const bool changed = other && GetFileTime(other.get(), nullptr, nullptr, &modified) &&
			WriteFile(other.get(), "x", 1, &written, nullptr) && written == 1 &&
			SetFileTime(other.get(), nullptr, nullptr, &modified);
		IW_CHECK(changed);
		if (!changed) return;
	}
	IW_CHECK(!state.Image.SaveFile(path, key, &status, true));
	IW_CHECK(!status.error.IsEmpty() && state.Image.IsDirty());
	IW_CHECK_EQ(ReadWorkflowMarker(path), 'x');
}

IW_TEST(EditDraftKeepsItsOwnSourceWhenTheSameFileIsRefreshed)
{
	WorkflowTestFiles fixture;
	IW_CHECK(!fixture.folder.IsEmpty());
	if (fixture.folder.IsEmpty()) return;

	const auto path = fixture.Path(_T("photo.bmp"));
	const auto replacement = fixture.Path(_T("replacement.bmp"));
	WorkflowImageCoupling coupling;
	coupling.paths = {path};
	State state(&coupling, false);
	CLoadAny loader(state.Loaders);
	auto initial = WorkflowSolidImage(IW::RGBA(200, 20, 40));
	auto revised = WorkflowSolidImage(IW::RGBA(20, 200, 40));
	const bool written = loader.SaveImage(path, initial, App.Settings.Codec, IW::CNullStatus::Instance) &&
		loader.SaveImage(replacement, revised, App.Settings.Codec, IW::CNullStatus::Instance);
	IW_CHECK(written);
	if (!written) return;

	state.Image.LoadImage(0, false);
	auto request = coupling.request;
	const bool loaded = request && request->Load(loader, IW::CNullStatus::Instance);
	IW_CHECK(loaded);
	if (!loaded) return;
	state.Image.OnLoadComplete(request);
	EditView edit(&coupling, state);
	edit.Reload();
	IW_CHECK(edit.CanSaveOriginal());
	edit._edits._rotate = 1;

	// Copy only into disposable test data. The viewer rereads this path,
	// while the editor's live draft must keep the bytes it started from.
	const bool replaced = ::CopyFile(replacement, path, FALSE) != FALSE;
	IW_CHECK(replaced);
	if (!replaced) return;
	state.Image.Reload();
	edit.Reload();
	IW_CHECK(edit.HasChanges() && !edit.CanSaveOriginal());
	COLORREF pixel{};
	edit._source.GetFirstPage().GetSurfaceLock()->GetLine(&pixel, 0, 0, 1);
	IW_CHECK_EQ(IW::GetR(pixel), 200u);

	edit.CancelEdits();
	IW_CHECK(!edit.HasChanges() && edit.CanSaveOriginal());
	edit._source.GetFirstPage().GetSurfaceLock()->GetLine(&pixel, 0, 0, 1);
	IW_CHECK_EQ(IW::GetR(pixel), 20u);
}

IW_TEST(RawDigestRequiresFinalizationAndReturnsOwnedBytes)
{
	MD5 digest;
	std::unique_ptr<unsigned char[]> unfinished(digest.raw_digest());
	IW_CHECK(unfinished == nullptr);
	digest.finalize();
	IW_CHECK(digest.hex_digest() == _T("d41d8cd98f00b204e9800998ecf8427e"));
	std::unique_ptr<unsigned char[]> bytes(digest.raw_digest());
	IW_CHECK(bytes != nullptr);
	if (!bytes) return;
	const unsigned char expected[] = {0xd4, 0x1d, 0x8c, 0xd9, 0x8f, 0x00, 0xb2, 0x04,
		0xe9, 0x80, 0x09, 0x98, 0xec, 0xf8, 0x42, 0x7e};
	IW_CHECK(memcmp(bytes.get(), expected, sizeof(expected)) == 0);
	bytes[0] = 0;
	std::unique_ptr<unsigned char[]> independent(digest.raw_digest());
	IW_CHECK(independent != nullptr);
	if (independent) IW_CHECK_EQ(independent[0], expected[0]);
}

IW_TEST(ClipboardFormatEnumerationSkipsAndClonesAtTheCorrectPosition)
{
	CComPtr<CImageDataObject> object = IW::CreateComObject<CImageDataObject>();
	object->Cache(WorkflowSolidImage(RGB(255, 0, 0)));
	CComPtr<IEnumFORMATETC> formats;
	IW_CHECK_EQ(object->EnumFormatEtc(DATADIR_GET, &formats), S_OK);
	IW_CHECK(formats != nullptr);
	if (!formats) return;
	FORMATETC value{};
	IW_CHECK_EQ(formats->Skip(0), S_OK);
	IW_CHECK_EQ(formats->Skip(1), S_OK);
	CComPtr<IEnumFORMATETC> cloned;
	IW_CHECK_EQ(formats->Clone(&cloned), S_OK);
	IW_CHECK(cloned != nullptr);
	if (!cloned) return;
	IW_CHECK_EQ(formats->Next(1, &value, nullptr), S_OK);
	IW_CHECK_EQ(value.cfFormat, CF_METAFILEPICT);
	::CoTaskMemFree(value.ptd);
	IW_CHECK_EQ(cloned->Next(1, &value, nullptr), S_OK);
	IW_CHECK_EQ(value.cfFormat, CF_METAFILEPICT);
	::CoTaskMemFree(value.ptd);
	IW_CHECK_EQ(formats->Next(1, &value, nullptr), S_FALSE);
	IW_CHECK_EQ(formats->Reset(), S_OK);
	IW_CHECK_EQ(formats->Skip(2), S_OK);
	IW_CHECK_EQ(formats->Next(1, &value, nullptr), S_FALSE);
	IW_CHECK_EQ(formats->Reset(), S_OK);
	IW_CHECK_EQ(formats->Skip(0xffffffffu), S_FALSE);
	IW_CHECK_EQ(formats->Next(1, &value, nullptr), S_FALSE);
}

IW_TEST(ClipboardFormatEnumerationOwnsItsTargetDeviceCopies)
{
	struct DeviceObject : CImageDataObject
	{
		STDMETHOD(EnumFormatEtc)(DWORD direction, IEnumFORMATETC** formats) override
		{
			if (direction != DATADIR_GET) return E_NOTIMPL;
			DVTARGETDEVICE device{};
			device.tdSize = sizeof(device);
			FORMATETC value{CF_TEXT, &device, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
			return IW::CreateFormatEnumerator(1, &value, formats);
		}
	};
	CComPtr<DeviceObject> provider = IW::CreateComObject<DeviceObject>();
	CComPtr<CImageDataObject> object = IW::CreateComObject<CImageDataObject>();
	object->AggregateDataObject(provider);
	CComPtr<IEnumFORMATETC> formats;
	IW_CHECK_EQ(object->EnumFormatEtc(DATADIR_GET, &formats), S_OK);
	if (!formats) return;
	object.Release();
	provider.Release();
	FORMATETC value{};
	IW_CHECK_EQ(formats->Next(1, &value, nullptr), S_OK);
	CComHeapPtr<DVTARGETDEVICE> device;
	device.Attach(value.ptd);
	IW_CHECK(device != nullptr);
	if (device) IW_CHECK_EQ(device->tdSize, sizeof(DVTARGETDEVICE));
}

IW_TEST(CompressedStreamsReportConsumedBytesAndZeroLengthTransfers)
{
	std::vector<BYTE> input(4096);
	for (size_t i = 0; i < input.size(); ++i)
		input[i] = static_cast<BYTE>(i);
	WorkflowCompressedBytes bytes;
	{
		IW::zostream<WorkflowCompressedBytes> output(bytes);
		DWORD written = 123;
		output.Write(nullptr, 0, &written);
		IW_CHECK_EQ(written, 0u);
		output.Write(input.data(), static_cast<DWORD>(input.size()), &written);
		IW_CHECK_EQ(written, static_cast<DWORD>(input.size()));
		output.Close();
	}
	IW::zistream<WorkflowCompressedBytes> stream(bytes);
	DWORD read = 123;
	stream.Read(nullptr, 0, &read);
	IW_CHECK_EQ(read, 0u);
	std::vector<BYTE> decoded(input.size() + 16, 0xCD);
	stream.Read(decoded.data(), static_cast<DWORD>(decoded.size()), &read);
	IW_CHECK_EQ(read, static_cast<DWORD>(input.size()));
	IW_CHECK(memcmp(decoded.data(), input.data(), input.size()) == 0);
	IW_CHECK_EQ(decoded.back(), 0xCD);
	stream.Read(nullptr, 0, &read);
	IW_CHECK_EQ(read, 0u);
}

IW_TEST(SearchNodeNegationAndReparsingKeepTheRequestedOperators)
{
	IW::CSearchNode node(IW::seOR, _T("cat"), true);
	IW_CHECK_EQ(node._se, static_cast<DWORD>(IW::seOR | IW::seNOT));
	IW_CHECK(node.ParseFromString(_T("+"), _T("dog")));
	IW_CHECK_EQ(node._se, static_cast<DWORD>(IW::seAND));
	IW_CHECK(node.ParseFromString(_T("!"), _T("cat")));
	IW_CHECK_EQ(node._se, static_cast<DWORD>(IW::seNOT));

	IW::CSearchNodeList words;
	IW_CHECK(words.ParseFromString(_T("cat ! dog")));
	IW_CHECK(words.Match(_T("cat")));
	IW_CHECK(!words.Match(_T("cat dog")));
}

IW_TEST(SearchNodeSelfAssignmentPreservesTheQuery)
{
	IW::CSearchNodeList words;
	IW_CHECK(words.ParseFromString(_T("cat -dog")));
	IW_CHECK(words.Match(_T("cat")));
	IW_CHECK(!words.Match(_T("cat dog")));
	words.Copy(words);
	IW_CHECK(!words.IsEmpty());
	IW_CHECK(words.Match(_T("cat")));
	IW_CHECK(!words.Match(_T("cat dog")));
	IW::CSearchNodeList copied(words);
	IW_CHECK(copied.Match(_T("cat")));
	IW_CHECK(!copied.Match(_T("cat dog")));
}

IW_TEST(VersionIdentityMatchesTheCompiledResourcesAndAboutDialog)
{
	CString expectedName;
	expectedName.Format(_T("ImageWalker %d.%d"), MAJOR_VERSION, MINOR_VERSION);
	IW_CHECK(expectedName == _T(APP_VERSION_NAME));

	DWORD ignored = 0;
	const DWORD size = GetFileVersionInfoSize(IW::Paths::ModulePath().c_str(), &ignored);
	IW_CHECK(size > 0);
	if (size == 0) return;
	std::vector<BYTE> bytes(size);
	const BOOL loaded = GetFileVersionInfo(IW::Paths::ModulePath().c_str(), 0, size, bytes.data());
	IW_CHECK(loaded);
	if (!loaded) return;
	LPVOID value = nullptr;
	UINT length = 0;
	const BOOL queried = VerQueryValue(bytes.data(), _T("\\"), &value, &length);
	IW_CHECK(queried && length >= sizeof(VS_FIXEDFILEINFO));
	if (!queried || length < sizeof(VS_FIXEDFILEINFO)) return;
	const auto* fixed = static_cast<const VS_FIXEDFILEINFO*>(value);
	IW_CHECK_EQ(fixed->dwFileVersionMS, MAKELONG(MINOR_VERSION, MAJOR_VERSION));
	IW_CHECK_EQ(fixed->dwFileVersionLS, MAKELONG(0, BUILD_NUMBER));
	IW_CHECK_EQ(fixed->dwProductVersionMS, fixed->dwFileVersionMS);
	IW_CHECK_EQ(fixed->dwProductVersionLS, fixed->dwFileVersionLS);

	const BOOL translated = VerQueryValue(bytes.data(), _T("\\VarFileInfo\\Translation"), &value, &length);
	IW_CHECK(translated && length >= 2 * sizeof(WORD));
	if (!translated || length < 2 * sizeof(WORD)) return;
	const auto* translation = static_cast<const WORD*>(value);
	for (LPCTSTR field : {_T("FileDescription"), _T("ProductName"), _T("FileVersion"), _T("ProductVersion")})
	{
		CString query;
		query.Format(_T("\\StringFileInfo\\%04x%04x\\%s"),
			static_cast<unsigned>(translation[0]), static_cast<unsigned>(translation[1]), field);
		const BOOL found = VerQueryValue(bytes.data(), query, &value, &length);
		IW_CHECK(found && length > 0);
		if (found && length > 0)
		{
			const bool numeric = _tcscmp(field, _T("FileVersion")) == 0 || _tcscmp(field, _T("ProductVersion")) == 0;
			IW_CHECK(CString(static_cast<LPCTSTR>(value)) == (numeric ? _T(RC_VERSION) : _T(APP_VERSION_NAME)));
		}
	}

	CAboutDlg dialog;
	const HWND window = dialog.Create(nullptr);
	IW_CHECK(window != nullptr);
	if (!window) return;
	WorkflowDialogLifetime lifetime{window};
	dialog.ShowWindow(SW_HIDE);
	CString title;
	dialog.GetDlgItemText(IDC_TITLE, title);
	IW_CHECK(title.Find(expectedName + _T(" - released ")) == 0);
}

IW_TEST(CommandRoutingGivesTheActiveViewFirstRefusal)
{
	WorkflowCommandTarget view;
	view.owned = ID_FILE_PRINT;
	WorkflowCommandFrame frame;
	frame._pView = &view;
	frame.frameCommands.owned = ID_VIEW_PRINT;
	BOOL handled = FALSE;
	frame.OnCommand(WM_COMMAND, MAKEWPARAM(ID_FILE_PRINT, 1), 0, handled);
	IW_CHECK(handled);
	IW_CHECK(view.seen == std::vector<DWORD>{ID_FILE_PRINT});
	IW_CHECK(frame.frameCommands.seen.empty());

	frame.OnCommand(WM_COMMAND, MAKEWPARAM(ID_VIEW_PRINT, 0), 0, handled);
	IW_CHECK(handled);
	IW_CHECK_EQ(frame.frameCommands.seen.back(), static_cast<DWORD>(ID_VIEW_PRINT));
	frame.OnCommand(WM_COMMAND, MAKEWPARAM(ID_BROWSE_RENAME, 0), 0, handled);
	IW_CHECK(!handled);
	IW_CHECK_EQ(view.seen.back(), static_cast<DWORD>(ID_BROWSE_RENAME));
	IW_CHECK_EQ(frame.frameCommands.seen.back(), static_cast<DWORD>(ID_BROWSE_RENAME));

	frame._pView = nullptr;
	frame.OnCommand(WM_COMMAND, MAKEWPARAM(ID_VIEW_PRINT, 1), 0, handled);
	IW_CHECK(handled);
	IW_CHECK_EQ(view.seen.size(), size_t{3});
}

IW_TEST(CompiledAcceleratorsMatchSharedNavigationAndFileCommands)
{
	const HACCEL table = LoadAccelerators(_Module.GetResourceInstance(), MAKEINTRESOURCE(IDR_MAINFRAME));
	IW_CHECK(table != nullptr);
	if (!table) return;
	const int count = CopyAcceleratorTable(table, nullptr, 0);
	IW_CHECK(count > 0);
	if (count <= 0) return;
	std::vector<ACCEL> entries(static_cast<size_t>(count));
	IW_CHECK_EQ(CopyAcceleratorTable(table, entries.data(), count), count);
	const auto command = [&entries](WORD key, BYTE modifiers)
	{
		WORD found = 0;
		for (const auto &entry : entries)
		{
			if (entry.key == key && (entry.fVirt & (FVIRTKEY | FCONTROL | FSHIFT | FALT)) == modifiers)
			{
				IW_CHECK_EQ(found, 0);
				found = entry.cmd;
			}
		}
		return found;
	};
	IW_CHECK_EQ(command(VK_F2, FVIRTKEY), ID_BROWSE_RENAME);
	IW_CHECK_EQ(command(VK_DELETE, FVIRTKEY), ID_EDIT_DELETE);
	IW_CHECK_EQ(command(VK_F7, FVIRTKEY), 0);
	IW_CHECK_EQ(command('X', FVIRTKEY | FCONTROL | FSHIFT), ID_FILE_MOVETO_POPUP);
	IW_CHECK_EQ(command('C', FVIRTKEY | FCONTROL | FSHIFT), ID_FILE_COPYTO_POPUP);
	IW_CHECK_EQ(command(VK_RETURN, FVIRTKEY | FCONTROL), ID_FILE_OPEN_WITH);
	IW_CHECK_EQ(command(VK_RETURN, FVIRTKEY | FCONTROL | FSHIFT), ID_FILE_PROPERTIES);
	IW_CHECK_EQ(command(VK_SPACE, FVIRTKEY), ID_IMAGE_NEXT);
	IW_CHECK_EQ(command(VK_SPACE, FVIRTKEY | FCONTROL | FSHIFT), ID_VIEW_IMAGEFULLSCREEN);
	IW_CHECK_EQ(command(VK_F11, FVIRTKEY), ID_VIEW_IMAGEFULLSCREEN);
	IW_CHECK_EQ(command(VK_BACK, FVIRTKEY), ID_BROWSE_BACK);
	IW_CHECK_EQ(command(_T('P'), FVIRTKEY | FCONTROL), ID_FILE_PRINT);
	IW_CHECK_EQ(command(_T('P'), FVIRTKEY | FCONTROL | FSHIFT), ID_VIEW_PRINT);
	IW_CHECK_EQ(command(VK_F10, FVIRTKEY), 0);
}

IW_TEST(RenameDialogInitializesAndRebuildsTheVisibleReviewOnControlChanges)
{
	IW_CHECK(AtlInitCommonControls(ICC_LISTVIEW_CLASSES | ICC_UPDOWN_CLASS));
	WorkflowTestFiles fixture;
	IW_CHECK(!fixture.folder.IsEmpty());
	if (fixture.folder.IsEmpty()) return;

	RENAMEPLAN plan;
	for (LPCTSTR name : {_T("alpha"), _T("beta")})
	{
		RenameItem item;
		item.strFolder = fixture.folder;
		item.strBase = name;
		item.strExt = _T(".jpg");
		item.strName = item.strBase + item.strExt;
		item.strPath = IW::Path::Combine(item.strFolder, item.strName);
		plan.push_back(item);
	}
	CRenameSelectedDlg dialog(plan, _T("Photo ###"), 37);
	const HWND window = dialog.Create(nullptr);
	IW_CHECK(window != nullptr);
	if (!window) return;
	WorkflowDialogLifetime lifetime{window};
	dialog.ShowWindow(SW_HIDE);
	IW_CHECK_EQ(dialog._nStart, 37);
	IW_CHECK_EQ(dialog._list.GetItemCount(), 2);
	IW_CHECK(plan[0].strNewName == _T("Photo 037.jpg"));
	IW_CHECK(plan[1].strNewName == _T("Photo 038.jpg"));
	IW_CHECK(dialog.GetDlgItem(IDOK).IsWindowEnabled());
	TCHAR text[256]{};
	dialog._list.GetItemText(0, 1, text, countof(text));
	IW_CHECK(CString(text) == plan[0].strNewName);

	// SetWindowText sends the real edit's EN_CHANGE through the dialog map.
	dialog.SetDlgItemText(IDC_TEMPLATE, _T("same"));
	IW_CHECK(!dialog.GetDlgItem(IDOK).IsWindowEnabled());
	for (int row = 0; row < 2; ++row)
	{
		dialog._list.GetItemText(row, 1, text, countof(text));
		IW_CHECK(CString(text).IsEmpty());
		dialog._list.GetItemText(row, 2, text, countof(text));
		IW_CHECK(!CString(text).IsEmpty());
	}
	dialog.SetDlgItemText(IDC_TEMPLATE, _T("?"));
	IW_CHECK(!dialog.GetDlgItem(IDOK).IsWindowEnabled());
	dialog.SetDlgItemText(IDC_TEMPLATE, _T("ALPHA"));
	IW_CHECK(!dialog.GetDlgItem(IDOK).IsWindowEnabled());
	dialog.SetDlgItemText(IDC_TEMPLATE, _T("New ##"));
	dialog.SetDlgItemInt(IDC_START_AT, 8, FALSE);
	IW_CHECK(dialog.GetDlgItem(IDOK).IsWindowEnabled());
	IW_CHECK(plan[0].strNewName == _T("New 08.jpg"));
	IW_CHECK(plan[1].strNewName == _T("New 09.jpg"));
	dialog.SetDlgItemText(IDC_TEMPLATE, _T(""));
	IW_CHECK(!dialog.GetDlgItem(IDOK).IsWindowEnabled());
	dialog.GetDlgItemText(IDC_RENAME_STATUS, text, countof(text));
	IW_CHECK(!CString(text).IsEmpty());
}

IW_TEST(ExplicitShellCopyAndMoveDoNotReplaceAnOccupiedName)
{
	WorkflowTestFiles fixture;
	IW_CHECK(!fixture.folder.IsEmpty());
	if (fixture.folder.IsEmpty()) return;

	const auto destination = IW::Path::Combine(fixture.folder, _T("destination"));
	if (!::CreateDirectory(destination, nullptr))
	{
		IW_CHECK(false);
		return;
	}
	struct OwnedDirectory
	{
		std::filesystem::path path;
		~OwnedDirectory() { std::error_code error; std::filesystem::remove_all(path, error); }
	} owner{destination.GetString()};

	for (const bool move : {false, true})
	{
		const LPCTSTR name = move ? _T("move.png") : _T("copy.png");
		const CString source = fixture.Path(name);
		const CString occupied = IW::Path::Combine(destination, name);
		IW_CHECK(WriteWorkflowMarker(source, move ? 'M' : 'C'));
		IW_CHECK(WriteWorkflowMarker(occupied, 'O'));

		IW::FileOperation operation(source + _T("\n\n"));
		IW_CHECK(operation.Copy(destination, move));
		IW_CHECK_EQ(ReadWorkflowMarker(occupied), 'O');
		IW_CHECK_EQ(ReadWorkflowMarker(source), move ? 0 : 'C');

		int copies = 0;
		for (const auto &entry : std::filesystem::directory_iterator(owner.path))
		{
			if (ReadWorkflowMarker(entry.path().c_str()) == (move ? 'M' : 'C'))
				copies += 1;
		}
		IW_CHECK_EQ(copies, 1);
	}
}

IW_TEST(RecycleRefusesInvalidSelectionAndNeverDeletesLockedOriginal)
{
	WorkflowTestFiles fixture;
	IW_CHECK(!fixture.folder.IsEmpty());
	if (fixture.folder.IsEmpty()) return;

	const CString original = fixture.Path(_T("original.txt"));
	IW_CHECK(WriteWorkflowMarker(original, 'O'));
	const IW::FileOperation invalid(original + _T("\nrelative.txt\n\n"));
	IW_CHECK(!invalid.Recycle());
	IW_CHECK_EQ(ReadWorkflowMarker(original), 'O');

	const HANDLE lock = ::CreateFile(original, GENERIC_READ, FILE_SHARE_READ, nullptr,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	IW_CHECK(lock != INVALID_HANDLE_VALUE);
	if (lock == INVALID_HANDLE_VALUE) return;
	const IW::FileOperation locked(original + _T("\n\n"));
	IW_CHECK(!locked.Recycle());
	::CloseHandle(lock);
	IW_CHECK_EQ(ReadWorkflowMarker(original), 'O');
}

IW_TEST(PrintPaginationAndOnePerPageUseTheSameImageSelection)
{
	State state(static_cast<Coupling *>(nullptr), false);
	IW_CHECK_EQ(state.Folder.GetFolder()->GetItemCount(), 0);
	IW::FolderPtr folder = new IW::RefObj<IW::Folder>;
	state.Folder.SetFolder(folder);
	for (int i = 0; i < 5; ++i)
	{
		auto item = IW::FolderItem::CreateTestItem();
		item->ModifyFlags(THUMB_SELECTED, i < 2 ? THUMB_SELECTED : 0);
		folder->InsertThumb(item);
	}
	auto nonImage = IW::FolderItem::CreateTestItem();
	nonImage->ModifyFlags(THUMB_IMAGE, THUMB_SELECTED);
	folder->InsertThumb(nonImage);

	CPrintFolder print(state);
	ConfigureWorkflowPrint(print);
	CDC dc;
	IW_CHECK(dc.CreateCompatibleDC(nullptr) != nullptr);
	if (dc.IsNull()) return;
	const CRect page(0, 0, 640, 480);
	IW_CHECK(print.CalcLayout(dc.m_hDC, page));
	IW_CHECK_EQ(print.GetPageCount(), 2);
	IW_CHECK_EQ(print._sizeSection, CSize(320, 240));
	IW_CHECK(!print.IsValidPage(0));
	IW_CHECK(print.IsValidPage(1));
	IW_CHECK(print.IsValidPage(2));
	IW_CHECK(!print.IsValidPage(3));
	print.m_bPrintSelected = true;
	IW_CHECK_EQ(print.GetPageCount(), 1);
	print.m_bPrintOnePerPage = true;
	IW_CHECK(print.CalcLayout(dc.m_hDC, page));
	IW_CHECK_EQ(print.GridSize(), CSize(1, 1));
	IW_CHECK_EQ(print._sizeSection, page.Size());
	IW_CHECK_EQ(print.GetPageCount(), 2);
	print.m_bPrintSelected = false;
	IW_CHECK_EQ(print.GetPageCount(), 5);
	print.m_bShowHeader = true;
	print.m_bShowFooter = true;
	IW_CHECK(print.CalcLayout(dc.m_hDC, page));
	IW_CHECK_EQ(print._sizeSection.cy, page.Height() - print.m_nHeaderHeight - print.m_nFooterHeight);
	IW_CHECK(!print.CalcLayout(dc.m_hDC, CRect(0, 0, 5, 5)));
}

IW_TEST(PrintPageRendersOnlyItsOwnGridAndRestoresTheDeviceContext)
{
	State state(static_cast<Coupling *>(nullptr), false);
	IW::FolderPtr folder = new IW::RefObj<IW::Folder>;
	state.Folder.SetFolder(folder);
	const COLORREF colors[] = {RGB(200, 20, 20), RGB(20, 200, 20), RGB(20, 20, 200),
		RGB(200, 200, 20), RGB(200, 20, 200)};
	for (COLORREF value : colors)
	{
		auto item = IW::FolderItem::CreateTestItem();
		// Surface locks use DIB byte order; GetPixel returns Win32 COLORREF.
		item->SetImage(WorkflowSolidImage(IW::SwapRB(value) | 0xff000000));
		folder->InsertThumb(item);
	}
	CPrintFolder print(state);
	ConfigureWorkflowPrint(print);
	CDC dc;
	IW_CHECK(dc.CreateCompatibleDC(nullptr) != nullptr);
	if (dc.IsNull()) return;
	CBitmap bitmap;
	IW_CHECK(bitmap.CreateBitmap(640, 480, 1, 32, nullptr) != nullptr);
	if (!bitmap.m_hBitmap) return;
	const HBITMAP old = dc.SelectBitmap(bitmap);
	IW_CHECK(print.CalcLayout(dc.m_hDC, CRect(0, 0, 640, 480)));
	dc.FillSolidRect(0, 0, 640, 480, RGB(255, 255, 255));
	dc.SetBkMode(OPAQUE);
	dc.SetTextColor(RGB(1, 2, 3));
	IW_CHECK(print.PrintPage(CDCHandle(dc.m_hDC), 0));
	IW_CHECK_EQ(dc.GetPixel(160, 120), colors[0]);
	IW_CHECK_EQ(dc.GetPixel(480, 120), colors[1]);
	IW_CHECK_EQ(dc.GetPixel(160, 360), colors[2]);
	IW_CHECK_EQ(dc.GetPixel(480, 360), colors[3]);
	IW_CHECK_EQ(dc.GetBkMode(), OPAQUE);
	IW_CHECK_EQ(dc.GetTextColor(), RGB(1, 2, 3));
	dc.FillSolidRect(0, 0, 640, 480, RGB(255, 255, 255));
	IW_CHECK(print.PrintPage(CDCHandle(dc.m_hDC), 1));
	IW_CHECK_EQ(dc.GetPixel(160, 120), colors[4]);
	IW_CHECK_EQ(dc.GetPixel(480, 120), RGB(255, 255, 255));
	dc.SelectBitmap(old);
}

IW_TEST(ContactSheetEncodesPagesWithoutReplacingAnExistingSheet)
{
	WorkflowTestFiles fixture;
	IW_CHECK(!fixture.folder.IsEmpty());
	if (fixture.folder.IsEmpty()) return;
	State state(static_cast<Coupling *>(nullptr), false);
	CPrintFolder print(state);
	ConfigureWorkflowPrint(print);
	print.m_bPrintOnePerPage = true;
	print.m_bPrintSelected = true;
	IW_CHECK(print.CalcLayout(CDCHandle()));
	CToolContactSheet tool(print, state);
	IW_CHECK(tool._bSelected);
	IW_CHECK_EQ(tool._options.GridSize(), CSize(1, 1));
	IW_CHECK(!tool.AllowSource() && !tool.AllowOverwrite());
	tool._pathFolderOut = fixture.folder;
	tool._strOutputFile = _T("Sheet");
	tool._sizeOutputImage = CSize(320, 240);
	tool.m_pLoaderFactory = state.Loaders.Find(CLoadPng::_GetKey());
	tool.m_pLoader = new IW::RefObj<CLoadPng>;
	IW_CHECK(tool.m_pLoaderFactory != nullptr);
	if (!tool.m_pLoaderFactory) return;

	const CString existing = fixture.Path(_T("Sheet 1.PNG"));
	const CString output = fixture.Path(_T("Sheet 2.PNG"));
	const CString input = fixture.Path(_T("source.PNG"));
	const auto image = WorkflowSolidImage(IW::RGBA(200, 20, 40));
	const auto write = [&image](const CString &path)
	{
		IW::CFile file;
		CLoadPng encoder;
		return file.OpenForWrite(path) &&
			encoder.Write(g_szEmptyString, &file, image, IW::CodecSettings{}, IW::CNullStatus::Instance);
	};
	IW_CHECK(write(existing));
	IW_CHECK(write(input));
	auto item = IW::FolderItem::CreateTestItem();
	IW_CHECK(item->Init(input));
	IW_CHECK(tool.StartItem(item, IW::CNullStatus::Instance));
	IW_CHECK(IW::Path::FileExists(output));
	CLoadAny loader(state.Loaders);
	IW::Image saved, original;
	IW_CHECK(loader.LoadImage(output, saved, IW::CNullStatus::Instance));
	IW_CHECK(loader.LoadImage(existing, original, IW::CNullStatus::Instance));
	IW_CHECK(!saved.IsEmpty() && saved.GetBoundingRect().Size() == CSize(320, 240));
	IW_CHECK(!original.IsEmpty() && original.GetBoundingRect().Size() == CSize(32, 32));
	if (!saved.IsEmpty())
	{
		COLORREF pixel{};
		saved.GetFirstPage().GetSurfaceLock()->GetLine(&pixel, 120, 160, 1);
		IW_CHECK_EQ(IW::GetR(pixel), 200u);
		IW_CHECK_EQ(IW::GetG(pixel), 20u);
		IW_CHECK_EQ(IW::GetB(pixel), 40u);
	}
}
