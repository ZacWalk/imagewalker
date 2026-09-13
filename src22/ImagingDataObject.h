// ImageWalker by Zac Walker
//
// Purpose: The IDataObject that carries an image to the clipboard and to
//          drag and drop.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#pragma once


class CImageDataObject   : 
	public CComObjectRootEx<CComSingleThreadModel>,
	public CComCoClass<CImageDataObject>,
	public IDataObject
{
protected:
	// Information remembered for dataobject
	IW::RefPtr<IDataObject>  m_spShellDataObject;
	IW::CShellItem _itemCachedItem; 
	IW::Image _image;

	bool m_bCachedItemImage;
	bool m_bMove;
	bool m_bHasImage;
	bool m_bSetForMove;

public:
	CImageDataObject();
	virtual ~CImageDataObject();


	BEGIN_COM_MAP(CImageDataObject)
		COM_INTERFACE_ENTRY(IDataObject)
	END_COM_MAP()

	void AggregateDataObject(IDataObject *pDataObject);
	void Cache(const IW::CShellItem &item);
	void Cache(const IW::Image &image);
	void SetForMove(bool bMove);


	// IDataObject
	STDMETHOD(GetData)(FORMATETC *pformatetcIn, STGMEDIUM *pmedium);
	STDMETHOD(GetDataHere)(FORMATETC* /* pformatetc */, STGMEDIUM* /* pmedium */);
	STDMETHOD(QueryGetData)(FORMATETC* /* pformatetc */);
	STDMETHOD(GetCanonicalFormatEtc)(FORMATETC* /* pformatectIn */,FORMATETC* /* pformatetcOut */);
	STDMETHOD(SetData)(FORMATETC* /* pformatetc */, STGMEDIUM* /* pmedium */, BOOL /* fRelease */);
	STDMETHOD(EnumFormatEtc)(DWORD /* dwDirection */, IEnumFORMATETC** /* ppenumFormatEtc */);
	STDMETHOD(DAdvise)(FORMATETC *pformatetc, DWORD advf, IAdviseSink *pAdvSink, DWORD *pdwConnection);
	STDMETHOD(DUnadvise)(DWORD dwConnection);
	STDMETHOD(EnumDAdvise)(IEnumSTATDATA **ppenumAdvise);

};
