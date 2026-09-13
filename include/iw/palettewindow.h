#pragma once


template<class T>
class CPaletteImpl
{

	BEGIN_MSG_MAP(CImageWindow)
		MESSAGE_HANDLER(WM_QUERYNEWPALETTE, OnQueryNewPalette)
		MESSAGE_HANDLER(WM_PALETTECHANGED, OnPaletteChanged)
	END_MSG_MAP() 

	LRESULT OnQueryNewPalette(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
	{
		if (App.m_paletteHalftone != NULL) 
		{
			T* pT = static_cast<T*>(this);
			CWindowDC dc(pT->m_hWnd);

			HPALETTE hOldPal = dc.SelectPalette (App.m_paletteHalftone, FALSE);

			const UINT nColors = dc.RealizePalette();
			if (nColors != 0)
				pT->Invalidate();

			if (hOldPal)
				dc.SelectPalette(hOldPal, FALSE);

			return nColors;
		}

		return 0;

	}

	LRESULT OnPaletteChanged(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled)
	{
		T* pT = static_cast<T*>(this);

		if ((App.m_paletteHalftone != NULL) && ((HWND) wParam != pT->m_hWnd)) 
		{			
			CWindowDC dc(pT->m_hWnd);
			HPALETTE hOldPal = dc.SelectPalette(App.m_paletteHalftone, FALSE);

			if (dc.RealizePalette())
				pT->Invalidate();

			if (hOldPal)
				dc.SelectPalette(hOldPal, FALSE);
		}

		return 0;
	}

	class CPaintDC : public CDC
	{
	public:
		// Data members
		HWND m_hWnd;
		PAINTSTRUCT m_ps;
		HPALETTE m_hOldPal;

		// Constructor/destructor
		CPaintDC(HWND hWnd)
		{
			ATLASSERT(::IsWindow(hWnd));
			m_hWnd = hWnd;
			m_hDC = ::BeginPaint(hWnd, &m_ps);
			m_hOldPal = 0;			

			if (App.m_paletteHalftone != NULL) 
			{
				m_hOldPal = SelectPalette (App.m_paletteHalftone, FALSE);
				RealizePalette ();
			}
		}

		~CPaintDC()
		{
			if (m_hOldPal)
			{
				SelectPalette(m_hOldPal, FALSE);
			}

			ATLASSERT(m_hDC != NULL);
			ATLASSERT(::IsWindow(m_hWnd));
			::EndPaint(m_hWnd, &m_ps);
			Detach();
		}
	};
};