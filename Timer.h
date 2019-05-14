/* Copyright (C) Hauck Software Solutions - All Rights Reserved
* You may use, distribute and modify this code under the terms
* that changes to the code must be reported back the original
* author
*
* Company: Hauck Software Solutions
* Author:  Thomas Hauck
* Email:   Thomas@fam-hauck.de
*
*/

#pragma once

#include <condition_variable>
#include <atomic>
#include <thread>

class Timer
{
public:
    Timer(uint32_t tMilliSeconds, function<void(const Timer*, void*)> fTimeOut, void* pUserData = nullptr) : m_tMilliSeconds(tMilliSeconds), m_fTimeOut(fTimeOut), m_pUserData(pUserData)
    {
        atomic_init(&m_bStop, false);
        atomic_init(&m_bIsStoped, false);

        m_thWaitThread = thread([&]()
        {
            unique_lock<mutex> lock(m_mxCv);
            do
            {
                if (m_cv.wait_for(lock, chrono::milliseconds(m_tMilliSeconds)) == cv_status::timeout)
                {
                    if (m_fTimeOut != 0 && m_bStop == false)
                        m_fTimeOut(this, m_pUserData);
                    break;
                }
            } while (m_bStop == false);
            m_bIsStoped = true;
        });
    }

    virtual ~Timer()
    {
        Stop();
        while (m_bIsStoped == false)
        {
            this_thread::sleep_for(chrono::microseconds(1));
            bool bIsLocked = m_mxCv.try_lock();
            if (bIsLocked == true)  // if it is not looked, the timeout is right now
            {
                m_cv.notify_all();
                m_mxCv.unlock();
            }
        }
        if (m_thWaitThread.joinable() == true)
            m_thWaitThread.join();
    }

    void Reset() noexcept
    {
        bool bIsLocked = m_mxCv.try_lock();
        if (bIsLocked == true)  // if it is not looked, the timeout is right now
        {
            m_cv.notify_all();
            m_mxCv.unlock();
        }
    }

    void Stop() noexcept
    {
        m_bStop = true;
        bool bIsLocked = m_mxCv.try_lock();
        if (bIsLocked == true)  // if it is not looked, the timeout is right now
        {
            m_cv.notify_all();
            m_mxCv.unlock();
        }
    }

    bool IsStopped()
    {
        return m_bIsStoped;
    }

    void SetNewTimeout(uint32_t nNewTime)
    {
        m_tMilliSeconds = nNewTime;
        bool bIsLocked = m_mxCv.try_lock();
        if (bIsLocked == true)  // if it is not looked, the timeout is right now
        {
            m_cv.notify_all();
            m_mxCv.unlock();
        }
    }

private:
    uint32_t m_tMilliSeconds;
    function<void(const Timer*, void*)> m_fTimeOut;
    void* m_pUserData;
    atomic<bool> m_bStop;
    atomic<bool> m_bIsStoped;
    thread m_thWaitThread;
    mutex m_mxCv;
    condition_variable m_cv;
};
