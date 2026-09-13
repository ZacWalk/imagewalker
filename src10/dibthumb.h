#pragma once

#define IMAGE_X 80
#define IMAGE_Y 80


template<class Loader>
class CDibThumb
{
public:

   inline void Scale(CDib &dibDst, Loader &loader)
   {
      UINT nWidthSrc = loader.Width();
      UINT nHeightSrc = loader.Height();

      UINT nWidthDst = (nWidthSrc  * IMAGE_Y) / nHeightSrc;
		UINT nHeightDst = (nHeightSrc  * IMAGE_X) / nWidthSrc;

      if (nWidthDst == 0)
         nWidthDst = 1;

      if (nHeightDst == 0)
         nHeightDst = 1;

		if (nWidthDst > IMAGE_X)
		{
			nWidthDst = IMAGE_X;
		}
		else
		{
			nHeightDst = IMAGE_Y;
		}


		if (dibDst.Create(nWidthDst, nHeightDst, 32, FALSE))
		{
         // What the image pane sizes the stand-in by, so a thumbnail blown up
         // while the picture loads occupies the rectangle the picture will.
         dibDst.m_sizeOriginal = CSize(static_cast<int>(nWidthSrc), static_cast<int>(nHeightSrc));

         RGBSUM pSum[IMAGE_X];
         ASSERT(nWidthDst <= IMAGE_X);

         UINT yDst = 0;
         LPRGBSUM pSumLast = pSum + nWidthDst;
         UINT ySum = 0;

		   while (yDst < nHeightDst) 
		   {
            ZeroMemory(pSum, sizeof(RGBSUM) * nWidthDst);

            ySum += nHeightSrc;

			   while (ySum >= nHeightDst) 
			   {
               LPBYTE pByte = loader.ScanLine();

               // A truncated or malformed source runs out of rows early.
               if (pByte == NULL)
                  return;

               loader.GetLine(nWidthSrc, nWidthDst, 
                           pSum, pSumLast, 
                           pByte);
			         
               ySum -= nHeightDst;
			   }

            LPBYTE pLine = dibDst.GetBitmap(yDst++);

            // Each RGBSUM is four channel accumulators followed by the pixel
            // count; divide, truncate to a byte and pack. The original divided
            // unconditionally and faulted on an empty column.
            DWORD *pOut = reinterpret_cast<DWORD *>(pLine);

            for (LPRGBSUM p = pSum; p < pSumLast; p++, pOut++)
            {
               const DWORD *a = reinterpret_cast<const DWORD *>(p);
               const DWORD n = a[4] ? a[4] : 1;

               *pOut = (((a[3] / n) & 0xFF) << 24) |
                       (((a[2] / n) & 0xFF) << 16) |
                       (((a[1] / n) & 0xFF) <<  8) |
                       (((a[0] / n) & 0xFF));
            }


		   }
      }
   }
};


