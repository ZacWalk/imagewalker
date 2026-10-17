#pragma once

// Progress and cancellation for the background loader jobs. The class is not in
// the repository at any revision; this is reconstructed from its constructor
// calls in BrowserThread.h and the single call site in LoadJpg.cpp:
//    if (pStatus && pStatus->Status(done, total)) AfxThrowUserException();
// so a TRUE result means "abandon this job".
class CStatus
{
public:
   CStatus(LONG *pJobNo, LPCSTR szName)
      : m_pJobNo(pJobNo),
        m_nJobNo(pJobNo != NULL ? ::InterlockedCompareExchange(pJobNo, 0, 0) : 0),
        m_strName(szName != NULL ? szName : ""),
        m_nDone(0),
        m_nTotal(0)
   {
   }

   virtual ~CStatus() {}

   // A newer job has been queued, so this one is stale.
   // Same test as Status() but without reporting progress.
   BOOL Exit() const
   {
      return IsStale();
   }

   virtual BOOL Status(int nDone, int nTotal)
   {
      m_nDone = nDone;
      m_nTotal = nTotal;

      return IsStale();
   }

   volatile LONG *m_pJobNo;
   LONG    m_nJobNo;
   CString m_strName;
   int     m_nDone;
   int     m_nTotal;

private:
   // The counter is written by the UI thread and read here on a worker.
   BOOL IsStale() const
   {
      return (m_pJobNo != NULL &&
              ::InterlockedCompareExchange(m_pJobNo, 0, 0) != m_nJobNo)
         ? TRUE : FALSE;
   }
};
