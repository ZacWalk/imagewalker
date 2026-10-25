// dib.h : header file
//
// CDib class
//

#ifndef __DIB__
#define __DIB__

// m_nOriginalType is a leftover of the format the loaders used to record here.
// Nothing in this build sets it.
#define DIB_UNKNOWN 0

inline int CalcStorageWidth(UINT nWidth, UINT nBpp)
{
   // Both come from a file header, so the product does not fit in 32 bits.
   const unsigned __int64 nBits = static_cast<unsigned __int64>(nWidth) * nBpp;
   const unsigned __int64 nBytes = (((nBits + 7) / 8) + 3) & ~3ui64;

   return (nBytes > 0x7FFFFFFFui64) ? 0 : static_cast<int>(nBytes);
}

// The largest size with the aspect of 'size' that fits inside 'box'. It enlarges
// as well as shrinks, so one axis always lands exactly on the box: a fit worked
// out through a whole-number percentage instead leaves up to 1/scale of the box
// empty, which at 12% is a twelfth of the pane.
inline CSize FitToBox(CSize size, CSize box)
{
   if (size.cx <= 0 || size.cy <= 0 || box.cx <= 0 || box.cy <= 0)
      return size;

   // Cross-multiplied rather than divided, in 64 bits: the product of two image
   // dimensions does not fit in an int.
   return (static_cast<__int64>(size.cx) * box.cy <= static_cast<__int64>(size.cy) * box.cx)
             ? CSize(max(MulDiv(size.cx, box.cy, size.cy), 1), box.cy)
             : CSize(box.cx, max(MulDiv(size.cy, box.cx, size.cx), 1));
}

class CDib
{
public:

   void Close() { Free(); };

   CSize HalfSize();

   CDib();
   CDib(const CDib &dib);

   ~CDib();

   void Draw(HDC hDC, const POINT &point, const RECT &rect);

   CSize Size();

   int SetDIBits(HDC hdc, HBITMAP hbm, UINT firstline = 0, UINT lastline = -1, UINT flags = DIB_RGB_COLORS);
   
protected:
   void Free();
   
   BITMAPINFO* m_pBMI;         // Pointer to BITMAPINFO struct
   BYTE* m_pBits;              // Pointer to the bits
   HBITMAP m_hBitmap;
public:
   int m_nStorageWidth;
   int m_nOriginalType;
   UINT m_uFlags;

   // Format description built by the loaders for the tooltip.
   CString m_strInfo;

   // The pixel size of the file this came from. A thumbnail is a reduced copy,
   // so it is not its own source; zero means "the same as this DIB".
   CSize m_sizeOriginal;
   
public:
   const CDib& operator=(const CDib &dib);
   void CopyTo(CDib &dib) const;
   BOOL LoadResource(UINT nID, CSize size);
   BOOL Set(CDib *pDib);

   // Takes the other DIB's pixels and leaves it closed. The jobs hand over a
   // decode they are about to destroy, and those run to tens of megabytes.
   void Attach(CDib &dib);

   CSize OriginalSize() const
   {
      return (m_sizeOriginal.cx > 0 && m_sizeOriginal.cy > 0)
                ? m_sizeOriginal
                : CSize(Width(), Height());
   }
   
   HBITMAP GetHBitmap() { ASSERT_VALID(this); ASSERT(m_pBMI); return m_hBitmap; };
   
   BOOL Create(unsigned width, unsigned height, unsigned bpp, BOOL bHBitmap);         // Create a new DIB
   
   inline BITMAPINFO* GetInfo() const
   {
      ASSERT_VALID(this);
      ASSERT(m_pBMI);
      return m_pBMI; // Pointer to bitmap info
   }
   
   inline BITMAPINFOHEADER* GetHeader() const
   {
      ASSERT_VALID(this);
      ASSERT(m_pBMI);
      return &(m_pBMI->bmiHeader); // Pointer to bitmap header
   }                        
   
   inline BYTE* GetBitmap() const
   {
      ASSERT_VALID(this);
      ASSERT(m_pBits);

      return m_pBits;
   }
   
   inline BYTE* GetBitmap(unsigned line) const
   {
      //ASSERT_VALID(this);
      ASSERT(m_pBits);
      ASSERT(line < Height());
      
      // Pointer to the bits
      
      return m_pBits + m_nStorageWidth * ((Height() - 1) - line); 
   }
   
   inline BYTE *GetBitmap(const unsigned &x, const unsigned &y) const
   {
      //ASSERT_VALID(this);
      ASSERT(x < Width());
      return GetBitmap(y) + (x * (Bpp() / 8));
   }
   
   inline RGBQUAD* GetColor() const
   {
      ASSERT_VALID(this);
      ASSERT(m_pBMI);
      return (LPRGBQUAD)(((BYTE*)(m_pBMI)) 
         + sizeof(BITMAPINFOHEADER));        // Pointer to color table
   }
   
   inline BOOL IsOpen() const
   {
      ASSERT_VALID(this);
      return (m_pBMI != NULL) && (Width()) && (Height());
   }
   
   inline unsigned Width() const { return GetHeader()->biWidth; };
   inline unsigned Height() const { return GetHeader()->biHeight; };
   inline unsigned Bpp() const { return GetHeader()->biBitCount; };
};




class CStatus;

// ArtMate cached a pre-scaled copy of an image while the preview was resized.
class CDibScale
{
public:
   CDibScale();
   ~CDibScale();

   BOOL Scale(CDib &dibSrc, CSize sizeDst, CStatus *pStatus);

   CDib m_dib;
};

CDibScale *CreateScale(CDib &dib, CSize sizeDst, CStatus *pStatus);

class CDibDC : public CDC  
{
public:
   CDibDC();
   virtual ~CDibDC();

   BOOL Create(int x, int y, int bpp);
   
   void Draw(LPRECT pRect, unsigned rgb);
   void Draw(CDib &dib, CPoint point);
   void Draw(CDib &dib, CRect r);
   void Draw(CDib &dib, CRect r, CDibScale *pScale);
   
   void Tile(CDib &dib, CPoint pointOffset);
   
   void SetMapping(CPoint pointScroll, CRect rect);
   
   CFontHandle *SelectObject(HFONT hFont);

   // The MFC-shaped members WTL does not carry: CDC has no SelectObject, and
   // its GetViewportOrg fills an out parameter instead of returning the point.
   HGDIOBJ SelectObject(HGDIOBJ hObject) { return ::SelectObject(m_hDC, hObject); }

   using CDC::GetViewportOrg;

   CPoint GetViewportOrg() const
   {
      POINT pt = { 0, 0 };
      CDC::GetViewportOrg(&pt);
      return CPoint(pt);
   }

   
   CPoint m_pointMapping;
   
   CRect  m_rectMapping;
   
   // Unmanaged: the DC never owns a font it did not create.
   CFontHandle m_fontOld;
   
   CDib  &GetDib() { return m_dib; };
   CRect &GetClip() { return m_rectClip; };
   void  SetClip(const CRect &r) 
   { 
      m_rectClip = r;

      POINT ptOrg = { 0, 0 };
      GetViewportOrg(&ptOrg);

      CRect rc(r);
      rc.OffsetRect(ptOrg.x, ptOrg.y);

      CRgn rgn;
      VERIFY(rgn.CreateRectRgnIndirect(&rc));
      SelectClipRgn(rgn);
   };
   
protected:
   HGDIOBJ		m_h;
   CDib		   m_dib;
   CRect		   m_rectClip;
private:
   
};

typedef struct
{
   UINT r, g, b, a;
   UINT s;   // pixel count; the thumbnail scaler divides by it
} 
RGBSUM, *LPRGBSUM;


class __declspec(novtable) CDibIterator1
{
protected:
   CDibIterator1() {};

public:
   
   inline COLORREF GetPixel(LPBYTE pLineSrc, UINT xx)
   {
      // In a Windows DIB the most significant bit of a byte is the leftmost pixel
      static int masks[] = { 0x80, 0x40, 0x20, 0x10, 
         0x08, 0x04, 0x02, 0x01};
      
      return *((COLORREF*)(m_pRgb + ((pLineSrc[xx >> 3] & masks[xx & 7]) ? 1 : 0)));
   }
   
   
   inline void GetLine(COLORREF *pLineDst, LPBYTE pLineSrc, UINT nStart, UINT nSize)
   {
      static int masks[] = { 0x80, 0x40, 0x20, 0x10, 
         0x08, 0x04, 0x02, 0x01};
      
      for(UINT xx = nStart; xx < nSize; xx++)
      {
         *pLineDst = *((COLORREF*)(m_pRgb + ((pLineSrc[xx >> 3] & masks[xx & 7]) ? 1 : 0)));
         pLineDst++;
      }
   }
   
   inline void GetLine(UINT &nWidthSrc, UINT &nWidthDst, 
      LPRGBSUM pSum, LPRGBSUM pSumEnd, 
      LPBYTE &pBytes)
   {
      UINT xx = 0;
      UINT x = 0;
      
      static int masks[] = { 0x80, 0x40, 0x20, 0x10, 
         0x08, 0x04, 0x02, 0x01};
      
      while(pSum < pSumEnd) 
      {
         x += nWidthSrc;
         
         while (x >= nWidthDst) 
         {
            LPBYTE p = (LPBYTE)(m_pRgb + ((pBytes[xx >> 3] & masks[xx & 7]) ? 1 : 0));
            
            pSum->r += p[0];
            pSum->g += p[1];
            pSum->b += p[2];
            pSum->a += 255;
            pSum->s += 1;
            
            x -= nWidthDst;
            xx += 1;
         }
         
         pSum++;
      }
   };
   
   COLORREF *m_pRgb;
};

class __declspec(novtable) CDibIterator8
{
protected:
   CDibIterator8() {};

public:
   
   inline void CopyPixel(LPBYTE pLineSrc, int nDst, int nSrc)
   {
      pLineSrc[nDst] = pLineSrc[nSrc];
   }
   
   inline COLORREF GetPixel(LPBYTE pLineSrc, UINT x)
   {
      return *((COLORREF*)(m_pRgb + pLineSrc[x]));
   }
   
   inline void GetLine(COLORREF *pLineDst, LPBYTE pLineSrc, UINT nStart, UINT nSize)
   {
      for(UINT n = nStart; n < nSize; n++)
      {
         *pLineDst = *((COLORREF*)(m_pRgb + pLineSrc[n]));
         pLineDst++;
      }
   }
   
   inline void GetLine(UINT &nWidthSrc, UINT &nWidthDst, 
      LPRGBSUM pSum, LPRGBSUM pSumEnd, 
      LPBYTE &pBytes)
   {
      UINT x = 0;
      
      while(pSum < pSumEnd) 
      {
         x += nWidthSrc;
         
         while (x >= nWidthDst) 
         {
            LPBYTE p = (LPBYTE)(m_pRgb + *pBytes++);
            
            pSum->r += p[0];
            pSum->g += p[1];
            pSum->b += p[2];
            pSum->a += p[3];
            pSum->s += 1;
            
            x -= nWidthDst;
         }
         
         pSum++;
      }
   };
   
   COLORREF *m_pRgb;
};

class __declspec(novtable) CDibIterator4
{
protected:
   CDibIterator4() {};

public:
   
   inline COLORREF GetPixel(LPBYTE pLineSrc, UINT xx)
   {
      return *((COLORREF*)(m_pRgb + ((xx & 1) ? pLineSrc[xx >> 1] & 0x0f : 
      pLineSrc[xx >> 1] >> 4)));
   }
   
   inline void GetLine(COLORREF *pLineDst, LPBYTE pLineSrc, UINT nStart, UINT nSize)
   {
      for(UINT xx = nStart; xx < nSize; xx++)
      {
         *pLineDst = *((COLORREF*)(m_pRgb + ((xx & 1) ? pLineSrc[xx >> 1] & 0x0f : 
         pLineSrc[xx >> 1] >> 4)));
         pLineDst++;
      }
   }
   
   inline void GetLine(UINT &nWidthSrc, UINT &nWidthDst, 
      LPRGBSUM pSum, LPRGBSUM pSumEnd, 
      LPBYTE &pBytes)
   {
      
      UINT x = 0;
      UINT xx = 0;
      
      while(pSum < pSumEnd)
      {
         x += nWidthSrc;
         
         while (x >= nWidthDst) 
         {
            LPBYTE p = (LPBYTE)(m_pRgb + ((xx & 1) ? pBytes[xx >> 1] & 0x0f : 
         pBytes[xx >> 1] >> 4));
         
         pSum->r += p[0];
         pSum->g += p[1];
         pSum->b += p[2];
         pSum->a += p[3];
         pSum->s += 1;
         
         x -= nWidthDst;
         xx += 1;
         }
         
         pSum++;
      }
   };
   
   COLORREF *m_pRgb;
};


class __declspec(novtable) CDibIterator8np
{
protected:
   CDibIterator8np() {};

public:
   
   inline void CopyPixel(LPBYTE pLineSrc, int nDst, int nSrc)
   {
      pLineSrc[nDst] = pLineSrc[nSrc];
   }
   
   
   inline void GetLine(UINT &nWidthSrc, UINT &nWidthDst, 
      LPRGBSUM pSum, LPRGBSUM pSumEnd, 
      LPBYTE &pBytes)
   {
      UINT x = 0;
      
      while(pSum < pSumEnd) 
      {
         x += nWidthSrc;
         
         while (x >= nWidthDst) 
         {
            pSum->r += *pBytes;
            pSum->g += *pBytes;
            pSum->b += *pBytes;
            pSum->a += 0xff;
            pSum->s += 1;
            
            x -= nWidthDst;
            pBytes++;
         }
         
         pSum++;
      }
   };
   
   COLORREF *m_pRgb;
};

class __declspec(novtable) CDibIterator24
{
protected:
   CDibIterator24() {};

public:
   inline COLORREF GetPixel(LPBYTE pLineSrc, UINT x)
   {
      LPBYTE p = pLineSrc + x * 3;
      return p[0] | (p[1] << 8) | ((COLORREF)p[2] << 16) | 0xff000000;
   }
   
   inline void CopyPixel(LPBYTE pLineSrc, int nDst, int nSrc)
   {
      nDst *= 3;
      nSrc *= 3;
      
      pLineSrc[nDst++] = pLineSrc[nSrc++];
      pLineSrc[nDst++] = pLineSrc[nSrc++];
      pLineSrc[nDst] = pLineSrc[nSrc];
   }
   
   inline void GetLine(COLORREF *pLineDst, LPBYTE pLineSrc, UINT nStart, UINT nSize)
   {
      if (nStart)
         pLineSrc += 3 * nStart;
      
      for(UINT n = nStart; n < nSize; n++)
      {
         *(((LPBYTE)pLineDst)) = *(((LPBYTE)pLineSrc));
         *(((LPBYTE)pLineDst+1)) = *(((LPBYTE)pLineSrc+1));
         *(((LPBYTE)pLineDst+2)) = *(((LPBYTE)pLineSrc+2));
         *(((LPBYTE)pLineDst+3)) = 0xff;
         pLineDst++;
         pLineSrc += 3;
      }
   }
   
   inline void SetPixel(LPBYTE &pLine, COLORREF c)
   {
      pLine[0] = (BYTE)c;
      pLine[1] = (BYTE)(c >> 8);
      pLine[2] = (BYTE)(c >> 16);
      pLine += 3;
   }
   
   inline void GetLine(UINT nWidthSrc, UINT nWidthDst, 
      LPRGBSUM pSum, LPRGBSUM pSumEnd, 
      LPBYTE pBytes)
   {
      UINT x = 0;
      
      while(pSum < pSumEnd) 
      {
         x += nWidthSrc;
         
         while (x >= nWidthDst) 
         {
            UINT c = *((UINT*)pBytes);
            
            pSum->r += c & 0xff;
            pSum->g += (c >> 8) & 0xff;
            pSum->b += (c >> 16) & 0xff;
            pSum->a += 0x0ff;
            pSum->s += 1;
            
            pBytes+=3;
            x -= nWidthDst;
         }
         
         pSum++;
      }
      
      /*_asm
      {
      xor ecx, ecx
      xor eax, eax
      mov edi, [pSum]
      mov esi, [pBytes]
      
        loop_sum:
        
          
            add ecx, nWidthSrc
            
              loop_x:
              
                mov edx, dword ptr [esi]
                mov al, dl
                add dword ptr [edi], eax
                
                  shr edx, 8
                  mov al, dl
                  add dword ptr [edi+4], eax
                  
                    shr edx, 8
                    mov al, dl
                    add dword ptr [edi+8], eax
                    
                      add dword ptr [edi+12], 0100h
                      
                        add esi, 3
                        
                          sub ecx, nWidthDst
                          cmp ecx, nWidthDst
                          jg  loop_x
                          
                            add edi, 16
                            
                              cmp edi, pSumEnd
                              jl  loop_sum
   }*/
      
      
   };
   
   /*inline void SetPixel(LPBYTE &pBytes, RGBSUM &c)
   {
   if (c.s)
   {
   pBytes[0] = c.r / c.s;
   pBytes[1] = c.g / c.s;
   pBytes[2] = c.b / c.s;
   };
   
     pBytes += 3;
   };*/
   
   COLORREF *m_pRgb;
};

class __declspec(novtable) CDibIterator32
{
protected:
   CDibIterator32() {};

public:
   inline COLORREF GetPixel(LPBYTE pLineSrc, UINT x)
   {
      return *(((COLORREF*)pLineSrc) + x);
   }
   
   inline void CopyPixel(LPBYTE pLineSrc, int nDst, int nSrc)
   {
      nDst *= 4;
      nSrc *= 4;
      
      pLineSrc[nDst++] = pLineSrc[nSrc++];
      pLineSrc[nDst++] = pLineSrc[nSrc++];
      pLineSrc[nDst] = pLineSrc[nSrc];
   }
   
   inline void GetLine(COLORREF *pLineDst, LPBYTE pLineSrc, UINT nStart, UINT nSize)
   {
      if (nStart)
         pLineSrc += sizeof(COLORREF) * nStart;
      
      CopyMemory(pLineDst, pLineSrc, (nSize - nStart) * sizeof(COLORREF));
   }
   
   inline void GetLine(UINT &nWidthSrc, UINT &nWidthDst, 
      LPRGBSUM pSum, LPRGBSUM pSumEnd, 
      LPBYTE &pBytes)
   {
      
      UINT x = 0;
      
      while(pSum < pSumEnd) 
      {
         x += nWidthSrc;
         
         while (x >= nWidthDst) 
         {
            pSum->r += pBytes[0];
            pSum->g += pBytes[1];
            pSum->b += pBytes[2];
            pSum->a += pBytes[3];
            pSum->s += 1;
            
            pBytes+=4;
            x -= nWidthDst;
         }
         
         pSum++;
      }
   };
   
   COLORREF *m_pRgb;
};

template<class Iterator>
class __declspec(novtable) CIteratorDibFile : public Iterator
{

public:   // 0.00's loaders construct these directly

   CIteratorDibFile(LPBYTE pByte, RGBQUAD *pRgb, CSize size, int nLineOffset)
   {
      m_pByte = pByte;
      m_pRgb = (COLORREF*)pRgb;
      m_size = size; 
      m_nLineOffset = nLineOffset;
   }

   CIteratorDibFile()
   {
      m_pByte = NULL;
      m_pRgb = NULL;
      m_size = 0; 
      m_nLineOffset = 0;
   }
   
   ~CIteratorDibFile()
   {
   }

public:

   void Open(LPBYTE pByte, RGBQUAD *pRgb, CSize size, int nLineOffset)
   {
      m_pByte = pByte;
      m_pRgb = (COLORREF*)pRgb;
      m_size = size; 
      m_nLineOffset = nLineOffset;
   }
   
   LPBYTE m_pByte;
   CSize m_size;
   int m_nLineOffset;
   
   inline UINT Height() { return m_size.cy; };
   inline UINT Width() { return m_size.cx; };
   
   inline LPBYTE ScanLine()
   {
      LPBYTE p = m_pByte;
      m_pByte += m_nLineOffset;
      return p;
   }
   
   inline LPBYTE ScanLine(int y)
   {
      return m_pByte + (m_nLineOffset * y);
   }
   
   
};


template<class Iterator>
class __declspec(novtable) CIteratorDib : public CIteratorDibFile<Iterator>
{
public:   // 0.00's loaders construct these directly
   CIteratorDib(CDib *pDib)
      : CIteratorDibFile<Iterator>(pDib->GetBitmap(0), 
      pDib->GetColor(), 
      pDib->Size(), 
      -pDib->m_nStorageWidth)
   {
   };

   CIteratorDib()
   {
   };

public:
   void Open(CDib *pDib)
   {
      m_pByte = pDib->GetBitmap(0);
      m_pRgb = (COLORREF*)pDib->GetColor();
      m_size = pDib->Size(); 
      m_nLineOffset = -pDib->m_nStorageWidth;
   };
};

#endif // __DIB__

