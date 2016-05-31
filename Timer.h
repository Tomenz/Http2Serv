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

class Timer
{
public:
    Timer(uint32_t tMilliSeconds, function<void(Timer*)> fTimeOut) : m_tMilliSeconds(tMilliSeconds), /*m_tpStart(chrono::system_clock::now()),*/ m_fTimeOut(fTimeOut)
    {
        atomic_init(&m_bStop, false);

        m_thWaitThread = thread([&]() {

            do
            {
                mutex mut;
                unique_lock<mutex> lock(mut);
                if (m_cv.wait_for(lock, chrono::milliseconds(m_tMilliSeconds)) == cv_status::timeout)
                {
                    if (m_fTimeOut != 0)
                        m_fTimeOut(this);
                    break;
                }
            } while (m_bStop == false);
        });
    }

    virtual ~Timer()
    {
        m_bStop = true;
        m_cv.notify_all();
        if (m_thWaitThread.joinable() == true)
            m_thWaitThread.join();
    }

    void Reset()
    {
        m_cv.notify_all();
    }

    void Stop()
    {
        m_bStop = true;
        m_cv.notify_all();
    }

private:
    uint32_t m_tMilliSeconds;
    function<void(Timer*)> m_fTimeOut;
    atomic<bool> m_bStop;
    thread m_thWaitThread;
    condition_variable m_cv;
};
