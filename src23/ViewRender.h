// ImageWalker by Zac Walker
//
// Purpose: IW::CRender: the double-buffered drawing surface everything
//          paints into, and IW::Style, the one place colours and fonts come
//          from.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#pragma once

class RenderSurface;

namespace IW
{
	struct Point2D
	{
		float x, y;
	};


	struct Style
	{
		struct Cursor
		{
			static CCursor Normal;
			static CCursor Link;
			static CCursor Select;
			static CCursor Move;
			static CCursor LeftRight;
			static CCursor HandUp;
			static CCursor HandDown;
			static CCursor Insert;
		};

		struct Icon
		{
			static CIcon ImageWalker;
			static CIcon Folder;
			static CIcon Default;
		};

		struct Brush
		{
			static CBrush TaskBackground;
			static CBrush Highlight;
			static CBrush EmphasizedHighlight;
		};

		struct Color
		{
			static DWORD TaskBackground;
			static DWORD TaskText;
			static DWORD TaskFrame;
			static DWORD TaskTextBold;

			static DWORD Window;
			static DWORD WindowText;
			static DWORD HighlightText;
			static DWORD Highlight;
			static DWORD MenuBackground;

			static DWORD Face;
			static DWORD Shadow;
			static DWORD Light;
		};

		struct Font
		{
			enum Type
			{
				Standard,
				Small,
				Heading,				
				Big,
				BigHover,
				Link,
				LinkHover,
				Huge
			};
		};

		struct Text
		{
			enum Style
			{				
				Thumbnail,
				SelectedThumbnail,
				Title,

				Ellipsis,
				EllipsisRight,

				Normal,
				NormalCentre,
				NormalRight,
				SingleLine,
				SingleLineRight,
				CenterInRect,
				Property
			};
		};

		static void SetMood();
		static CFont &GetFont(Font::Type type);

		// DT_CALCRECT needs a DC with the right font and nothing else, so this does
		// not go near a surface. Every call site used to build a whole CRender --
		// a CreateCompatibleDC and a CreateDIBSection -- just to measure a string.
		static CRect MeasureString(LPCTSTR sz, const CRect &rectIn, Font::Type, Text::Style);
	};

	class CRender
	{
	protected:

		RenderSurface *_pSurface;

	public:		

		CRender();
		virtual ~CRender();

		bool Create(HDC hdc);
		bool Create(HDC hdc, const CRect &rectClip);
		void Free();		

		void Flip();

		// Render 		
		void DrawRender(const CRender &renderIn, int opacity, LPRECT pRect = 0);
		void DrawImage(const Page &page, const CRect & rectDstIn, const CRect & rectSrcIn);

		void DrawImage(const Page &page, const CRect & rectDstIn)
		{
			const CRect rectSrcIn(0, 0, page.GetWidth(), page.GetHeight());
			DrawImage(page, rectDstIn, rectSrcIn);
		}

		void DrawImage(const Page &page, const CPoint &point)
		{
			DrawImage(page, point.x, point.y);
		}

		void DrawImage(const Page &page, const long x, const long y)
		{
			const CRect rectSrcIn(0, 0, page.GetWidth(), page.GetHeight());
			const CRect rectDstIn(x, y, x + page.GetWidth(), y + page.GetHeight());

			DrawImage(page, rectDstIn, rectSrcIn);
		}

		bool RenderToSurface(IW::Page &page, const long x, const long y) const;
		bool RenderToSurface(IW::Image &imageDest) const;
		bool RenderToSurface(IW::IImageStream *pImageDest) const;

		void Fill(COLORREF clr, LPCRECT pDestRect = 0);
		void Blend(COLORREF clr, LPCRECT pDestRect = 0);	

		void DrawString(LPCTSTR sz, const CRect &rectIn, Style::Font::Type, Style::Text::Style, COLORREF clr = Style::Color::WindowText);

		CRect MeasureString(LPCTSTR sz, const CRect &rectIn, Style::Font::Type fontType, Style::Text::Style style) const
		{
			return Style::MeasureString(sz, rectIn, fontType, style);
		}

		void DrawFocusRect(LPCRECT lpRect);
		void DrawRect(LPCRECT lpRect, COLORREF clr);
		void DrawRect(LPCRECT lpRect, COLORREF clrBorder, COLORREF clrFill, int nWidth = 0, bool bOpaque = true, bool bDarkToLight = true);		
		void DrawLine(int x1, int y1, int x2, int y2, COLORREF clr, int nWidth = 0);
		void DrawIcon(HICON hIcon, int x, int y, int cx, int cy);
		void DrawImageList(HIMAGELIST hImageList, int nItem, int x, int y, int cx, int cy);

		// Metafile
		void Play(HENHMETAFILE hemf);
		void Play(HMETAFILE hmf, int cx, int cy);

		// Draw straight through to a HDC
		static void DrawToDC(HDC hdc, const Page &page, const CRect & rect);
		static void DrawToDC(HDC hdc, const Page &page, const CPoint &point);

		friend class CDCRender;

	};

	class CDCRender : public CDCHandle 
	{
	public:
		CRender &_render;

		CDCRender(CRender &render);
		~CDCRender();
	};


}
