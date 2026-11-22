// ImageWalker by Zac Walker
//
// Purpose: ImageState: the image on screen, its edit stack, its dirty flag
//          and the status text describing it.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#pragma once

#include "ViewProgressDlg.h"
#include "ViewFileDialog.h"
#include "iw/zstream.h"
#include "ViewBackBuffer.h"
#include "ImagingDataObject.h"
#include "iw/legacy/Metadata.h"

struct ImageEditMode
{
	typedef enum {
		None,
		Select,
		Rotate,
		Crop,
		Color,
		Resize,
		Sharpen,
		Redeye
	} Mode;
};


class CImageLoad : public IW::Referenced
{
public:

	CImageLoad(const CString &strPath, bool bSignalEvent, long nStartItem, long nLoadItem, long nDirection)
	{
		_path = strPath;
		_bSignalEvent = bSignalEvent;
		_nStartItem = nStartItem;
		_nLoadItem = nLoadItem;
		_nDirection = nDirection;
		_bWasStopped = false;
	}

	~CImageLoad()
	{
	}	

	bool WasImageLoaded() const
	{
		return !_image.IsEmpty();
	}	

	bool Load(CLoadAny &loader, IW::IStatus *pStatus)
	{
		IW::CFile file;

		if (file.OpenForRead(_path))
		{
			IW::Image image;	

			if (loader.Read(IW::Path::FindExtension(_path), &file, image, pStatus) && !image.IsEmpty())
			{						
				_image = image;
				_stamp = file.CaptureEditStamp(pStatus);
				return true;										
			}
		}

		return false;
	}

	CString _path;
	IW::Image _image;
	std::optional<IW::EditFileStamp> _stamp;

	bool _bSignalEvent;
	long _nStartItem;
	long _nLoadItem;
	long _nDirection;
	bool _bWasStopped;	
	uint64_t _loadGeneration = 0;

private:
};

class CUndo : public IW::Referenced
{
protected:
	IW::SimpleBlob _data;
	CString _strAction;
	bool _bDirty;	

public:
	CUndo(IW::Image &image, const CString &strAction = g_szEmptyString, bool bDirty = false) :  _strAction(strAction), _bDirty(bDirty)
	  {
		  IW::Serialize::ArchiveStore archiveStore;
		  image.Serialize(archiveStore);

		  IW::StreamBlob<IW::SimpleBlob>  streamOut(_data);
		  IW::zostream<IW::StreamBlob<IW::SimpleBlob> > zstreamOut(streamOut);
		  archiveStore.Store(zstreamOut);
		  zstreamOut.Close();		
	  }

	  void GetImageState(IW::Image &imageReloaded, bool &bDirty) const
	  {
		  IW::StreamConstBlob streamIn(_data);
		  IW::zistream<IW::StreamConstBlob> zstreamIn(streamIn);

		  IW::Serialize::ArchiveLoad archiveLoad(zstreamIn);
		  imageReloaded.Serialize(archiveLoad);

		  bDirty = _bDirty; 
	  }

	  CString GetAction() const
	  {
		  return _strAction;
	  }

	  void MarkDirty() { _bDirty = true; }

	  virtual ~CUndo() 
	  {
	  }

	  CUndo(const CUndo &other) 
	  {
		  _data = other._data;
		  _strAction = other._strAction;
		  _bDirty = other._bDirty;
	  }

	  void operator=(const CUndo &other)
	  {
		  _data = other._data;
		  _strAction = other._strAction;
		  _bDirty = other._bDirty;
	  }
};



class ImageState
{
private:

	bool _bDirty;		
	std::optional<IW::EditFileStamp> _loadedStamp;
	CString _savedKey;

	ImageEditMode::Mode _editMode;

	Coupling *_pCoupling;	
	Coupling *_pItems;

	CSimpleArray<IW::RefPtr<CUndo> > _arUndo;	
	IW::CFilePath _path;
	IW::FileTime _ft;
	IW::FileSize _size;	
	IW::Histogram _histogram;	
	IW::Image _image;	
	IW::Image _imageRendered;	
	IW::Image _imageThumbnail;
	uint64_t _loadGeneration = 0;

	// True while what is on screen is the items pane's thumbnail standing in for
	// a file the loader thread has not finished decoding, and the size that
	// thumbnail is being drawn at -- the size the real picture will take, so the
	// swap costs a repaint and not a re-layout.
	bool _bPlaceholder = false;
	bool _bPlaceholderLoading = false;
	CSize _sizeDisplay = CSize(0, 0);

	ImageLoaders &_loaders;

public:	

	volatile bool IsLoading;

	Delegate::List0 EditModeDelegates;

	ImageState(ImageLoaders &loaders, Coupling *coupling, Coupling *pItems) : 
			_pCoupling(coupling),
			_bDirty(false),
			_pItems(pItems),
			_loaders(loaders),
			IsLoading(false),
			_editMode(ImageEditMode::None)
	  {
	  }

	  ~ImageState()
	  {
		  _arUndo.RemoveAll();
		  _image.Free();
		  _imageRendered.Free();
		  _imageThumbnail.Free();
	  }

	  bool IsDirty() const { return _bDirty; }
	  
	  CString GetChangesList() const;
	  bool CanUndo() const { return _arUndo.GetSize() > 0; };
	  bool CanEditImage() const { return IsImageReady(); };	


	  // Something is on screen. A placeholder counts: the zoom strip, the
	  // navigator and the description panel all belong to it.
	  bool IsImageShown() const
	  {
		  return !_image.IsEmpty();
	  }

	  // The file itself is decoded. Everything that writes, saves, filters or
	  // copies the picture has to ask this instead -- a placeholder is a
	  // thumbnail, and saving it would overwrite the file with 160 pixels.
	  bool IsImageReady() const { return !_image.IsEmpty() && !_bPlaceholder; }

	  // What the scale maths measures. While a placeholder is up that is the size
	  // of the picture it stands for, not the size of the thumbnail being drawn.
	  CSize GetDisplaySize() const
	  {
		  if (_sizeDisplay.cx > 0 && _sizeDisplay.cy > 0)
			  return _sizeDisplay;

		  return GetRenderImage().GetBoundingRect().Size();
	  }

	  CString GetImageFileName() const { return _path; };
	  const std::optional<IW::EditFileStamp> &GetLoadedStamp() const { return _loadedStamp; }
	  const IW::Image &GetImage() const { return _image; }; 
	  const IW::Image &GetThumbnailImage() const { return _imageThumbnail; };
	  const IW::Image &GetRenderImage() const { return _imageRendered.IsEmpty() ? _image : _imageRendered; }; 
	  const IW::Histogram &GetHistogram() const {  return _histogram; };

	  bool UpdateFileTime()
	  {
		  IW::FileTime ft = IW::FileTime::FromFile(_path);
		  bool bHasChanged = ft > _ft;
		  _ft = ft;
		  return bHasChanged;
	  }

	  void SetItems(Coupling *pItems)
	  {
		  _pItems = pItems;
	  }

	  // Retires whatever the loader thread is part way through, so a file
	  // operation or a folder change does not land an image nobody asked for.
	  void StopLoading()
	  {
		  ++_loadGeneration;
		  _pCoupling->SetStopLoading();	
	  }

private:
	  void QueueLoad(CImageLoad *pInfo)
	  {
		  pInfo->_loadGeneration = ++_loadGeneration;
		  _pCoupling->ShowImage(pInfo);
	  }

public:
	  // Stands the items pane's thumbnail in for a file the loader thread is
	  // about to decode. Not called with unsaved edits on screen: the prompt for
	  // those belongs to OnLoadComplete, and it has not been asked yet.
	  void ShowPlaceholder(const IW::Image &thumbnail, const CString &strPath, const CSize &sizeReal)
	  {
		  if (thumbnail.IsEmpty() || strPath.IsEmpty() || _bDirty || !IW::GetMainWindow())
			  return;

		  IW::Focus preserveFocus;

		  if (App.Settings.m_bUseEffects)
			  _pCoupling->BeginImageFade(FadeOverlay::defaultSteps);

		  ResetHistory();

		  _bPlaceholder = true;
		  _bPlaceholderLoading = true;
		  _sizeDisplay = (sizeReal.cx > 0 && sizeReal.cy > 0)
			                 ? sizeReal
			                 : thumbnail.GetBoundingRect().Size();

		  IW::Image image = thumbnail;
		  SetImage(image);
		  UpdateFileInfo(strPath);
		  OnNewImage(true);
		  UpdateStatusText();
	  }

	  bool CanDisplayNewImage(const CString &strNewImageName);

	  bool ImportBitmap(HGLOBAL bitmap)
	  {
		  IW::Image image;
		  if (!bitmap || !image.Copy(bitmap) || image.IsEmpty())
			  return false;

		  // A modal save prompt can dispatch a queued viewer completion.
		  StopLoading();
		  if (!_pCoupling->CanChangeImage() || !QuerySave())
			  return false;

		  SetImage(image, g_szEmptyString);
		  UpdateStatusText();
		  return true;
	  }

	  void OnLoadComplete(CImageLoad *pInfo)
	  {
		  // Cancellation on the worker cannot change a result already posted.
		  if (!pInfo->_bWasStopped && pInfo->_loadGeneration == _loadGeneration)
		  {
			  if (pInfo->WasImageLoaded())
			  {
				  // False means the user declined to leave the current image, or
				  // an autosave failed. Stopping the walk is the point:
				  // continuing would re-prompt on every frame of the show.
				  if (!CanDisplayNewImage(pInfo->_path) || pInfo->_loadGeneration != _loadGeneration)
					  return;

				  SetImage(pInfo->_image, pInfo->_path, pInfo->_stamp);

				  if (pInfo->_bSignalEvent)
				  {
					  _pCoupling->SelectFolderItem(IW::Path::FindFileName(pInfo->_path));				
				  }			
			  }
			  else if (_bPlaceholder && GetImageFileName().CompareNoCase(pInfo->_path) == 0)
			  {
				  // The decode this thumbnail was standing in for is not coming.
				  // The thumbnail is still the best picture of the file there is,
				  // so it stays -- but it must stop saying it is loading.
				  _bPlaceholderLoading = false;
			  }

			  // Outside the WasImageLoaded guard: LoadNext is the only thing that
			  // queues the successor, and its "not an image, seek on" branch runs
			  // only when the load failed.
			  LoadNext(pInfo);
			  UpdateStatusText();
		  }
	  }

	  void LoadImage(int nItem, bool bSignalEvent)
	  {
		  try
		  {
			  IW::RefPtr<CImageLoad> pNextInfo = new CImageLoad(_pItems->GetItemPath(nItem), 
				  bSignalEvent, nItem, nItem, 0);		

			  QueueLoad(pNextInfo);
		  }
		  catch (std::exception &)
		  {
		  }
	  }

	  // Seeks past whatever was not an image. This is the only producer of the
	  // next request, so an early return here ends the walk, not one item.
	  void LoadNext(CImageLoad *pInfo)
	  {
		  const bool bWasLoaded = pInfo->WasImageLoaded();

		  if (pInfo->_bWasStopped || pInfo->_loadGeneration != _loadGeneration || bWasLoaded || pInfo->_nDirection == 0)
			  return;

		  const long nCount = _pItems->GetItemCount();

		  if (nCount == 0)
			  return;

		  int nSeek = (pInfo->_nLoadItem + pInfo->_nDirection);
		  if (nSeek < 0) nSeek = nCount - 1;
		  if (nSeek >= nCount) nSeek = 0;

		  if (pInfo->_nStartItem == nSeek)
			  return;

		  IW::RefPtr<CImageLoad> pNextInfo = new CImageLoad(_pItems->GetItemPath(nSeek), 
			  pInfo->_bSignalEvent, pInfo->_nStartItem, nSeek, pInfo->_nDirection);		

		  QueueLoad(pNextInfo);
	  }

	  void LoadNextImage(int nStep)
	  {
		  StopLoading();
		  CLoadAny loader(_loaders);

		  long nCount = _pItems->GetItemCount();
		  int nSeek = _pItems->GetFocusItem() + nStep;

		  for(int i = 0; i < nCount; i++)
		  {
			  if (nSeek < 0) nSeek = nCount - 1;
			  if (nSeek >= nCount) nSeek = 0;				  

			  CImageLoad info(_pItems->GetItemPath(nSeek), true, nSeek, nSeek, nStep);
			  info._loadGeneration = _loadGeneration;

			  if (info.Load(loader, IW::CNullStatus::Instance))
			  {
				  OnLoadComplete(&info);
				  return;
			  }

			  nSeek += nStep;
		  }
	  }

	  void Reload()
	  {
		  // The folder refresh already asked about unsaved edits. Keep the
		  // current image and history if reading the replacement fails.
		  CLoadAny loader(_loaders);
		  CImageLoad info(_path, false, 0, 0, 0);
			
		  if (info.Load(loader, IW::CNullStatus::Instance))
		  {
			  SetImage(info._image, info._path, info._stamp);
			  UpdateStatusText();
		  }
	  }

	

	  void ResetHistory()
	  {
		  _bDirty = false;
		  _loadedStamp.reset();
		  _savedKey.Empty();
		  _arUndo.RemoveAll();
	  }	

	  void StepImage(int nStep)
	  {
		  const long nCount = _pItems->GetItemCount();

		  if (nCount)
		  {
			  int nSeek = (_pItems->GetFocusItem() + nStep);
			  if (nSeek < 0) nSeek = nCount - 1;
			  if (nSeek >= nCount) nSeek = 0;

			  IW::RefPtr<CImageLoad> pNextInfo = new CImageLoad(_pItems->GetItemPath(nSeek), true, nSeek, nSeek, nStep);
			  QueueLoad(pNextInfo);
		  }
	  }

	  // Both of these used to land on the last item, so Home and End did the
	  // same thing. They mean here what they mean in the items pane.
	  void Home()
	  {
		  if (_pItems->GetItemCount())
			  _pItems->SetFocusItem(0, true);
	  }

	  void End()
	  {
		  const long nCount = _pItems->GetItemCount();

		  if (nCount)
			  _pItems->SetFocusItem(nCount - 1, true);
	  }

	  void PreviousImage()
	  {
		  StopLoading();
		  StepImage(-1);
	  }	

	  void NextImage()
	  {
		  StopLoading();
		  StepImage(1);
	  }

	  void RenderImageForAnimation()
	  {
		  _imageRendered.Free();

		  const IW::Image &image = GetImage();

		  if (image.NeedRenderForDisplay())
		  {
			  IW::Image imageRendered;

			  if (GetImage().Render(imageRendered))
			  {
				  _imageRendered = imageRendered;
			  }
		  }
	  }

	  void SetImage(IW::Image &image, const CString &strImageFileName)
	  {
		  StopLoading();
		  SetImage(image, strImageFileName, {});
	  }

private:
	  void SetImage(IW::Image &image, const CString &strImageFileName,
	                const std::optional<IW::EditFileStamp> &stamp)
	  {
		  ResetHistory();
		  _loadedStamp = stamp;
		  _savedKey = image.GetLoaderName();

		  // Keep the scroll offset when the real picture replaces its thumbnail.
		  const bool bReplacingPlaceholder = _bPlaceholder &&
			  GetImageFileName().CompareNoCase(strImageFileName) == 0;

		  IW::Focus preserveFocus;

		  if (IW::GetMainWindow() && App.Settings.m_bUseEffects)
			  _pCoupling->BeginImageFade(bReplacingPlaceholder
			                             ? FadeOverlay::quickSteps
			                             : FadeOverlay::defaultSteps);

		  _bPlaceholder = false;
		  _bPlaceholderLoading = false;
		  _sizeDisplay = CSize(0, 0);

		  SetImage(image);
		  UpdateFileInfo(strImageFileName);
		  OnNewImage(!bReplacingPlaceholder);
	  }

public:
	  void UpdateFileInfo(const CString &strImageFileName)
	  {
		  _path = strImageFileName;
		  _ft = IW::FileTime::FromFile(strImageFileName);
		  _size = IW::FileSize::FromFile(strImageFileName);
	  }

	  void SetImage(IW::Image &image)
	  {
		  _image = image;
	  }

	  void OnNewImage(bool bScrollToCenter)
	  {
		  _histogram.Clear();
		  _image.GetHistogram(_histogram);

		  RenderImageForAnimation();
		  CreateThumbnail();
		  
		  _pCoupling->NewImage(bScrollToCenter);		  
	  }

	  void CreateThumbnail()
	  {
		  _imageThumbnail.Free();
		  if (!_image.IsEmpty()) _imageThumbnail = IW::CreatePreview(GetRenderImage(), CSize(160, 160));
	  }
	  
	  void SetImageWithHistory(IW::Image &image, const CString &strAction)
	  {
		  IW::Image imageOld = GetImage();

		  _bPlaceholder = false;
		  _bPlaceholderLoading = false;
		  _sizeDisplay = CSize(0, 0);

		  SetImage(image);

		  if (_arUndo.GetSize() > 10)
		  {
			  _arUndo.RemoveAt(0);
		  }

		  if (!imageOld.IsEmpty())
		  {
			  _arUndo.Add(new IW::RefObj<CUndo>(imageOld, strAction, _bDirty));
		  }

		  OnNewImage(false);
		  _bDirty = true;
	  }

	  void Undo()
	  {
		  if (_arUndo.GetSize())
		  {
			  int nTop = _arUndo.GetSize() - 1;
			  CUndo *pUndo = _arUndo[nTop];

			  IW::CMessageBoxIndirect mb;
			  if (IDOK == mb.Show(IW::Format(IDS_QUERYUNDO, static_cast<LPCTSTR>(pUndo->GetAction())), MB_ICONQUESTION | MB_OKCANCEL | MB_HELP))
				  UndoWithoutPrompt();
		  }
	  }

	  // Takes back the last SetImageWithHistory without asking, dirty flag and
	  // all. For a save that did not happen: the picture it was going to write
	  // must not stay behind as unsaved work nobody asked for.
	  void UndoWithoutPrompt()
	  {
		  const int nTop = _arUndo.GetSize() - 1;

		  if (nTop < 0)
			  return;

		  _arUndo[nTop]->GetImageState(_image, _bDirty);
		  _arUndo.RemoveAt(nTop);
		  OnNewImage(false);
		  UpdateStatusText();
	  }

	  bool SaveFile(const CString &strFileName, const CString &strType, IW::IStatus *pStatus, bool inPlace)
	  {
		  IW::CFileTemp f;
		  const bool opened = inPlace ? _loadedStamp && f.OpenForReplace(strFileName, *_loadedStamp)
		                              : f.OpenForWrite(strFileName, true);
		  if (!opened)
			  return false;

		  CLoadAny loader(_loaders);
		  if (!loader.Write(strType, &f, GetImage(), App.Settings.Codec, pStatus) || !f.Close(pStatus))
			  return false;

		  _loadedStamp = f.CommittedStamp();
		  _bDirty = false;
		  // Undo now restores pixels different from the version on disk.
		  for (int i = 0; i < _arUndo.GetSize(); ++i)
			  _arUndo[i]->MarkDirty();
		  _savedKey = strType;
		  _image.SetLoaderName(strType);
		  UpdateFileInfo(strFileName);
		  UpdateStatusText();
		  return true;
	  }

	  bool Save()
	  {
		  // The command state greys this, but an accelerator does not ask. Saving an
		  // unchanged picture would re-encode it over itself for nothing.
		  if (!IsImageReady() || !IsDirty())
			  return false;

		  const CString strImageName = GetImageFileName();
		  const IW::Image &image = GetImage();
		  const CString strKey = _savedKey.IsEmpty() ? image.GetLoaderName() : _savedKey;

		  if (strKey.IsEmpty())
		  {
			  IW::CMessageBoxIndirect mb;
			  if (IDOK == mb.Show(IDS_INVALIDSAVEFORMAT, MB_ICONQUESTION | MB_OKCANCEL | MB_HELP))
			  {
				  return SaveAs();
			  }
			  else 
			  {
				  return false;
			  }
		  }

		  if (!_loadedStamp)
		  {
			  IW::CMessageBoxIndirect mb;
			  mb.Show(_T("The original cannot be verified. Use Save As to keep it unchanged."));
			  return false;
		  }
		  return Save(strImageName, strKey, true);
	  }

	  bool Save(const CString &strFilePath, const CString &strKey, bool inPlace)
	  {
		  if (!IW::CFilePath::CheckFileName(strFilePath))
		  {
			  CString str;
			  str.Format(IDS_INVALID_FILE, static_cast<LPCTSTR>(strFilePath));

			  IW::CMessageBoxIndirect mb;
			  mb.Show(str);

			  return false;
		  }

		  const IW::ImageLoaderInfoPtr pFactory = _loaders.Find(strKey);

		  if (pFactory == nullptr)
		  {
			  CString str;
			  str.Format(IDS_INVALID_FILE, static_cast<LPCTSTR>(strFilePath));

			  IW::CMessageBoxIndirect mb;
			  mb.Show(str);

			  return false;
		  }

		  const IW::Image &image = GetImage();
		  const bool bHasMetaData = image.HasMetaData(IW::MetaDataTypes::PROFILE_EXIF) ||
			  image.HasMetaData(IW::MetaDataTypes::PROFILE_IPTC) ||
			  image.HasMetaData(IW::MetaDataTypes::PROFILE_XMP);
		  const bool bSupportMetaData = (pFactory->GetFlags() & IW::ImageLoaderFlags::METADATA)!=0;

		  IW::CMessageBoxIndirect mb;
		  if (bHasMetaData &&
			  !bSupportMetaData &&
			  IDOK != mb.Show(IDS_DOESNOT_SUPPORT_METADATA, MB_ICONQUESTION | MB_OKCANCEL | MB_HELP))
		  {
			  return false;
		  }

		  IW::CFileWriteStatus status;
		  const bool bRet = SaveFile(strFilePath, strKey, &status, inPlace);
		  if (!bRet)
		  {
			  if (!status.error.IsEmpty())
				  mb.Show(status.error);
			  else
				  mb.Show(_T("The photo could not be saved. Check the destination or try Save As with a new filename."));
		  }

		  return bRet;
	  }

	  bool SaveAs()
	  {
		  if (!IsImageReady())
			  return false;

		  CImageFileDialog dlg(_loaders, FALSE, NULL, GetImageFileName(), OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR);
		  dlg.SetDefaults(GetImage());	

		  if(dlg.DoModal() == IDOK)
		  {
			  if (IW::Path::FileExists(dlg.m_ofn.lpstrFile))
			  {
				  IW::CMessageBoxIndirect mb;
				  mb.Show(_T("Save As does not replace existing files. Choose a new filename."));
				  return false;
			  }
			  return Save(dlg.m_ofn.lpstrFile, dlg.GetLoaderType(), false);
		  }

		  return false;
	  }


	  void UpdateStatusText()
	  {
		  CString str;

		  if (_bPlaceholder)
		  {
			  str.Format(_bPlaceholderLoading
				             ? _T("Loading %s...")
				             : _T("%s - preview only, the file itself could not be opened"),
			             IW::Path::FindFileName(GetImageFileName()));
			  _pCoupling->SetStatusText(str);
			  return;
		  }

		  if (IsImageShown())
		  {
			  const IW::Image &image = GetImage();
			  IW::Page page = image.GetFirstPage();
			  CString originalFileName = GetImageFileName();

			  if (IsDirty())
			  {
				  str.Format(IDS_FMT_EDITING, IW::Path::FindFileName(originalFileName),
					  page.GetWidth(),
					  page.GetHeight(),
					  page.GetPixelFormat().ToBpp());
			  }
			  else
			  {
				  str.Format(IDS_FMT_VIEWING, IW::Path::FindFileName(originalFileName), static_cast<LPCTSTR>(image.GetStatistics()));
			  }

			  if (GetImage().HasMetaData(IW::MetaDataTypes::JPEG_IMAGE))
			  {
				  str += g_szSpace;
				  str += App.LoadString(IDS_NOLOSS);
			  }


			  int nPageCount = image.GetPageCount();

			  if (nPageCount > 1)
			  {
				  CString strPages;
				  strPages.Format(_T("%d Pages"), nPageCount);

				  str += g_szSpace;
				  str += strPages;
			  }

			  _pCoupling->SetStatusText(str);
		  }

	  }

	  bool QuerySave();

	  CString GetTitle() const
	  {
		  CString strTitle = _image.GetTitle();

		  if (strTitle.IsEmpty()) 
		  {
			  CString originalFileName = GetImageFileName();
			  IW::CFilePath path = IW::Path::FindFileName(originalFileName);
			  path.StripToFilename();
			  strTitle = path.ToString();
		  }

		  return strTitle;
	  }

	  CString GetTags() const
	  {
		  return _image.GetTags();		
	  }

	  CString GetDescription() const
	  {
		  CString strDescription = _image.GetDescription();	
		  strDescription.Replace(g_szCRLF, g_szCR);
		  strDescription.Replace(g_szLF, g_szCR);
		  strDescription.Replace(g_szCR, g_szCRLF);
		  return strDescription;
	  }


	  void Copy()
	  {
		  if (IsImageReady())
		  {
			  CComPtr<CImageDataObject> p = IW::CreateComObject<CImageDataObject>();
			  p->Cache(GetImage());
			  OleSetClipboard(p);
		  }
	  }

	  void OnEditCopy() 
	  {
		  // Clean clipboard of contents, and copy the DIB.
		  if (IsImageReady() && OpenClipboard(IW::GetMainWindow()))
		  {
			  EmptyClipboard();
			  HGLOBAL hMem = GetImage().CopyToHandle();
			  SetClipboardData (CF_DIB, hMem);
			  CloseClipboard();
		  }
	  }	

	  void Unload()
	  {
		  IW::Image image;
		  SetImage(image, g_szEmptyString);		
	  }


	  bool IsImageEditMode() const
	  { 
		  return _editMode != ImageEditMode::None;
	  }
	  
	  bool IsImageEditMode(ImageEditMode::Mode mode) const
	  { 
		  return _editMode == mode;
	  }
};
