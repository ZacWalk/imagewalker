// ImageWalker by Zac Walker
//
// Purpose: Search::Spec: what a search matches on - text, size, date, type
//          and tags.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#pragma once

namespace Search
{
	enum { AtLeast = 0, AtMost = 1 };
	


	class Spec
	{
	public:
		
		CString _strText;

		bool _bOnlyShowImages;
		bool _bSize;
		long _nSizeOption;
		long _nSizeKB;
		bool _bModified;
		int _nNumberOfDays;
		bool _bTaken;
		int _nMonth;
		int _nYear;

		mutable bool _bDidMatchSomething;
		mutable bool _isNameMatch;

		Spec(LPCTSTR szText)
		{
			_strText = szText;
			_bOnlyShowImages = false;
			_bSize = false;
			_nSizeOption = 0;
			_nSizeKB = 0;			
			_bModified = 0; 
			_nNumberOfDays = 0; 
			_bTaken = 0; 
			_nMonth = 0;
			_nYear = 0;
			_bDidMatchSomething = true;
			_isNameMatch = true;
		}

		/*Spec(COleDateTime from, COleDateTime to)
		{
			_bSize = false;
			_nSizeOption = 0;
			_nSizeKB = 0;		
			_bOnlyShowImages = true;

			_bDate = true;
			_nDateOption = 4;
			_ftDateFrom = from;
			_ftDateTo = to;
		};*/

		Spec()
		{
			_bSize = false;
			_nSizeOption = 0;
			_nSizeKB = 0;
			_bOnlyShowImages = false;
			_bModified = 0; 
			_nNumberOfDays = 0; 
			_bTaken = 0; 
			_nMonth = 0;
			_nYear = 0;
			_bDidMatchSomething = true;
			_isNameMatch = true;
		};

		
		Spec(LPCTSTR szText, 
			bool bOnlyShowImages,
			bool bSize, 
			long nSizeOption, 
			long nSizeKB, 
			bool bModified, 
			int nNumberOfDays, 
			bool bTaken, 
			int nMonth, 
			int nYear)
		{
			_strText = szText;
			_bOnlyShowImages = bOnlyShowImages;
			_bSize = bSize;
			_nSizeOption = nSizeOption;
			_nSizeKB = nSizeKB;
			_bModified = bModified; 
			_nNumberOfDays = nNumberOfDays; 
			_bTaken = bTaken; 
			_nMonth = nMonth;
			_nYear = nYear;
		}


		Spec(const Spec &other)
		{
			Copy(other);
		};

		void operator=(const Spec &other)
		{
			Copy(other);
		};

		void Copy(const Spec &other)
		{
			_strText = other._strText;
			_bOnlyShowImages = other._bOnlyShowImages;
			_bSize = other._bSize;
			_nSizeOption = other._nSizeOption;
			_nSizeKB = other._nSizeKB;
			_bModified = other._bModified; 
			_nNumberOfDays = other._nNumberOfDays; 
			_bTaken = other._bTaken; 
			_nMonth = other._nMonth;
			_nYear = other._nYear;
		};		

		void LoadImage(IW::FolderItemPtr pThumb, CLoadAny *pLoader) const
		{
			if (pLoader)
			{
				IW::FolderItemLoader job(*pThumb);
				job.LoadImage(pLoader, *this);
				job.RenderAndScale();
				job.SyncThumb(*pThumb);
			}
		}

		bool AbortMatch(const IW::FileTime &ft, const CString &strTitle, const CString &strTags, const CString &strDescription) const
		{
			if (_bTaken)
			{
				if (!IsDateTaken(ft))
					return false;

				_bDidMatchSomething = true;
			}

			if (!_strText.IsEmpty())
			{
				if (!_isNameMatch && !IsTextMatch(strTitle, strTags, strDescription))
					return false;

				_bDidMatchSomething = true;
			}

			return _bDidMatchSomething;
		}

		

		bool DoMatch(IW::FolderItemPtr pThumb, CLoadAny *pLoader = NULL) const
		{
			_bDidMatchSomething = false;
			_isNameMatch = IsNameMatch(pThumb, _strText);

			if (_bSize)
			{
				if  (!IsSizeMatch(pThumb))
					return false;

				_bDidMatchSomething = true;
			}

			if (_bModified)
			{
				if (!IsDateModified(pThumb))
					return false;

				_bDidMatchSomething = true;
			}

			LoadImage(pThumb, pLoader);

			if (_bOnlyShowImages)
			{
				if (!pThumb->IsImage())
					return false;

				_bDidMatchSomething = true;
			}

			if (_bTaken)
			{
				if (!IsDateTaken(pThumb->GetImage().GetCameraSettings().DateTaken))
					return false;

				_bDidMatchSomething = true;
			}

			if (!_strText.IsEmpty())
			{
				if (!_isNameMatch && !IsTextMatch(pThumb))
					return false;

				_bDidMatchSomething = true;
			}

			return _bDidMatchSomething;
		}

	private:

		bool IsNameMatch(const IW::FolderItem *pThumb, const CString &strName) const
		{
			return IW::SimpleMatch(strName, pThumb->GetFileName());
		}

		bool IsSizeMatch(const IW::FolderItem* pThumb) const
		{
			IW::FileSize sizeItem = pThumb->GetFileSize();
			IW::FileSize sizeToMatch = _nSizeKB * 1024;

			if (_nSizeOption == AtLeast)
			{
				return sizeItem > sizeToMatch;
			}
			else
			{
				return sizeItem < sizeToMatch;
			}
		}

		bool IsDateModified(const IW::FolderItem* pThumb) const
		{
			IW::FileTime ftDateFrom = IW::FileTime::Now();
			ftDateFrom.SetDaysPrevious(_nNumberOfDays);

			return ftDateFrom.Compare(pThumb->GetLastWriteTime()) < 0;
		}

		bool IsDateTaken(const IW::FileTime &ft) const
		{
			if (ft.GetYear() == _nYear)
			{
				return _nMonth == 0 ||
					_nMonth == ft.GetMonth();
			}
			
			return false;
		}

		bool IsTextMatch(const IW::FolderItem *pThumb) const
		{
			const IW::Image &image = pThumb->GetImage();
			return IsTextMatch(image.GetTitle(), image.GetTags(), image.GetDescription());
		}		

		bool IsTextMatch(const CString &strTitle, const CString &strTags, const CString &strDescription) const
		{
			IW::CSearchNodeList words;
			words.ParseFromString(_strText);

			if (!words.IsEmpty()) 
			{
				if (words.Match(strTitle) ||
					words.Match(strTags) ||
					words.Match(strDescription))
				{
					return true;
				}
			}

			return false;
		}
	};

	extern Spec Any;
}
