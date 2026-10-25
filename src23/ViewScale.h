// ImageWalker by Zac Walker
//
// Purpose: The zoom mode - fit, fill, up, down or a percentage - and the
//          text the zoom box shows.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#pragma once

struct ScaleMode
{
	typedef enum  { Fit, Fill, Up, Down, Normal } Type;
};

class Scale
{
protected:	

	ScaleMode::Type m_eScaleType;
	int m_nScaleSet;

public:

	Scale() : 
	  m_eScaleType(ScaleMode::Normal),
	  m_nScaleSet(100)
	{
	}

	enum { DIV = 0x10000 };

	void SetScale(ScaleMode::Type type)
	{
		m_eScaleType = type;
		m_nScaleSet = 100;
	}

	void SetScale(int s)
	{
		m_nScaleSet = IW::LowerLimit<1>(s);
		m_nScaleSet = IW::UpperLimit<1000>(m_nScaleSet);
		m_eScaleType = ScaleMode::Normal;
	}

	bool IsResizing() const
	{
		return m_eScaleType != ScaleMode::Normal;
	}

	ScaleMode::Type GetScaleType() const 
	{
		return m_eScaleType;
	}

	CSize CalcSize(const CSize &sizeIn, const CSize &sizeClient, const CSize &sizeWindow) const
	{		
		CSize sizeOut;

		int s = CalcScale(sizeIn, sizeClient, sizeWindow);

		sizeOut.cx = IW::LowerLimit<1>(MulDiv(sizeIn.cx, s, DIV));
		sizeOut.cy = IW::LowerLimit<1>(MulDiv(sizeIn.cy, s, DIV));

		return sizeOut;
	}

	int CalcScalePercent(const CSize &sizeIn, const CSize &sizeClient, const CSize &sizeWindow) const
	{
		return MulDiv(100, CalcScale(sizeIn, sizeClient, sizeWindow), DIV);
	}


	int CalcScale(const CSize &sizeIn, const CSize &sizeClient, const CSize &sizeWindow) const
	{
		int s = MulDiv(m_nScaleSet, DIV, 100);
		bool bCalc = false;
		CSize sizeMax = sizeClient;

		if (m_eScaleType == ScaleMode::Down)
		{
			bCalc = sizeClient.cx < sizeIn.cx ||
				sizeClient.cy < sizeIn.cy;
		}
		else  if (m_eScaleType == ScaleMode::Up)
		{
			bCalc = sizeClient.cx > sizeIn.cx &&
				sizeClient.cy > sizeIn.cy;
		}
		else if (m_eScaleType == ScaleMode::Fill)
		{
			sizeMax = sizeWindow;
			bCalc = true;
		}
		else if (m_eScaleType == ScaleMode::Fit)
		{
			bCalc = true;
		}

		if (bCalc)
		{
			int sh = MulDiv(sizeMax.cy, DIV, sizeIn.cy);
			int sw = MulDiv(sizeMax.cx, DIV, sizeIn.cx);

			s =  IW::Min(sh, sw);
		}

		return s;
	}

	CString GetScaleText()
	{
		CString str;

		if (m_eScaleType == ScaleMode::Fit)
		{
			str = App.LoadString(IDS_FIT);
		}
		else if (m_eScaleType == ScaleMode::Fill)
		{
			str = App.LoadString(IDS_FILL);
		}
		else if (m_eScaleType == ScaleMode::Up)
		{
			str = App.LoadString(IDS_UP);
		}
		else if (m_eScaleType == ScaleMode::Down)
		{
			str = App.LoadString(IDS_DOWN);
		}
		else
		{
			str.Format(_T("%d%%"), m_nScaleSet);
		}

		return str;
	}

	bool Parse(LPCTSTR szScale)
	{
		LPCTSTR szColon = nullptr;

		ScaleMode::Type eScaleType = m_eScaleType;
		int nScaleSet = m_nScaleSet;

		if (::StrStrI(szScale, App.LoadString(IDS_FILL)) != nullptr)
		{
			eScaleType = ScaleMode::Fill;
		}
		else if (::StrStrI(szScale, App.LoadString(IDS_FIT)) != nullptr)
		{
			eScaleType = ScaleMode::Fit;
		}
		else if (::StrStrI(szScale, App.LoadString(IDS_UP)) != nullptr)
		{
			eScaleType = ScaleMode::Up;
			nScaleSet = 100;
		}
		else if (::StrStrI(szScale, App.LoadString(IDS_DOWN)) != nullptr)
		{
			eScaleType = ScaleMode::Down;
			nScaleSet = 100;
		}
		else if ((szColon = _tcschr(szScale, _T(':'))) != nullptr)
		{
			const int n = _ttoi(szScale);
			const int d = IW::LowerLimit<1>(_ttoi(szColon + 1));

			nScaleSet = IW::UpperLimit<1000>(IW::LowerLimit<1>(MulDiv(n, 100, d)));
			eScaleType = ScaleMode::Normal;      
		}
		else
		{
			nScaleSet = IW::UpperLimit<1000>(IW::LowerLimit<1>(_ttoi(szScale)));
			eScaleType = ScaleMode::Normal;
		}

		if (eScaleType != m_eScaleType || nScaleSet != m_nScaleSet)
		{
			m_eScaleType = eScaleType;
			m_nScaleSet = nScaleSet;

			return true;
		}

		return false;
	}

	void Toggle(bool canDown)
	{
		if (m_eScaleType == ScaleMode::Normal)
		{
			SetScale(ScaleMode::Fit);
		}
		else if (m_eScaleType == ScaleMode::Fit && canDown)
		{
			SetScale(ScaleMode::Down);
		}
		else
		{
			SetScale(100);
		}		
	}
};
