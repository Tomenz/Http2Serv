/* Copyright (C) 2016-2020 Thomas Hauck - All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT

   The author would be happy if changes and
   improvements were reported back to him.

   Author:  Thomas Hauck
   Email:   Thomas@fam-hauck.de
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
            m_tStart = chrono::duration_cast<chrono::milliseconds>(chrono::system_clock::now().time_since_epoch());
            unique_lock<mutex> lock(m_mxCv);
            do
            {
                uint32_t nDifMilliSeconds = static_cast<uint32_t>((chrono::duration_cast<chrono::milliseconds>(chrono::system_clock::now().time_since_epoch()) - m_tStart).count());
                uint32_t tRestMilliSeconds = 1;
                if (nDifMilliSeconds < m_tMilliSeconds)
                    tRestMilliSeconds = m_tMilliSeconds - nDifMilliSeconds;

                if (m_cv.wait_for(lock, chrono::milliseconds(tRestMilliSeconds)) == cv_status::timeout)
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
        m_tStart = chrono::duration_cast<chrono::milliseconds>(chrono::system_clock::now().time_since_epoch());
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
        m_tStart = chrono::duration_cast<chrono::milliseconds>(chrono::system_clock::now().time_since_epoch());
        bool bIsLocked = m_mxCv.try_lock();
        if (bIsLocked == true)  // if it is not looked, the timeout is right now
        {
            m_cv.notify_all();
            m_mxCv.unlock();
        }
    }

private:
    uint32_t m_tMilliSeconds;
    chrono::milliseconds m_tStart;
    function<void(const Timer*, void*)> m_fTimeOut;
    void* m_pUserData;
    atomic<bool> m_bStop;
    atomic<bool> m_bIsStoped;
    thread m_thWaitThread;
    mutex m_mxCv;
    condition_variable m_cv;
};
