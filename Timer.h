/* Copyright (C) 2016-2020 Thomas Hauck - All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT

   The author would be happy if changes and
   improvements were reported back to him.

   Author:  Thomas Hauck
   Email:   Thomas@fam-hauck.de
*/

#ifndef TIMER_H
#define TIMER_H

#include <condition_variable>
#include <atomic>
#include <thread>
#include <functional>

template<class T>
class Timer
{
public:
    Timer(uint32_t tMilliSeconds, std::function<void(const Timer*, T*)> fTimeOut, T* pUserData = nullptr) : m_tMilliSeconds(tMilliSeconds), m_fTimeOut(fTimeOut), m_pUserData(pUserData)
    {
        atomic_init(&m_bStop, false);
        atomic_init(&m_bIsStoped, false);

        m_thWaitThread = std::thread([&]()
        {
            m_tStart = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch());
            std::unique_lock<std::mutex> lock(m_mxCv);
            do
            {
                std::chrono::milliseconds tNow = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch());
                uint32_t nDifMilliSeconds = static_cast<uint32_t>((tNow - m_tStart).count());
                if (tNow < m_tStart)
                {
                    tNow = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch());
                    nDifMilliSeconds = static_cast<uint32_t>((tNow - m_tStart).count());
                }
                uint32_t tRestMilliSeconds = 1;
                if (nDifMilliSeconds < m_tMilliSeconds)
                    tRestMilliSeconds = m_tMilliSeconds - nDifMilliSeconds;

                if (m_cv.wait_for(lock, std::chrono::milliseconds(tRestMilliSeconds)) == std::cv_status::timeout)
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
            std::this_thread::sleep_for(std::chrono::microseconds(1));
            const bool bIsLocked = m_mxCv.try_lock();
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
        m_tStart = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch());
        const bool bIsLocked = m_mxCv.try_lock();
        if (bIsLocked == true)  // if it is not looked, the timeout is right now
        {
            m_cv.notify_all();
            m_mxCv.unlock();
        }
    }

    void Stop() noexcept
    {
        m_bStop = true;
        const bool bIsLocked = m_mxCv.try_lock();
        if (bIsLocked == true)  // if it is not looked, the timeout is right now
        {
            m_cv.notify_all();
            m_mxCv.unlock();
        }
    }

    bool IsStopped() noexcept
    {
        return m_bIsStoped;
    }

    void SetNewTimeout(uint32_t nNewTime)
    {
        m_tMilliSeconds = nNewTime;
        m_tStart = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch());
        const bool bIsLocked = m_mxCv.try_lock();
        if (bIsLocked == true)  // if it is not looked, the timeout is right now
        {
            m_cv.notify_all();
            m_mxCv.unlock();
        }
    }

private:
    uint32_t m_tMilliSeconds;
    std::chrono::milliseconds m_tStart;
    std::function<void(const Timer*, T*)> m_fTimeOut;
    T* m_pUserData;
    std::atomic<bool> m_bStop;
    std::atomic<bool> m_bIsStoped;
    std::thread m_thWaitThread;
    std::mutex m_mxCv;
    std::condition_variable m_cv;
};

#endif  // TIMER_H
