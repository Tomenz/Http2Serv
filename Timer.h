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

class Timer
{
public:
    Timer(uint32_t tMilliSeconds, function<void(Timer*)> fTimeOut) : m_tMilliSeconds(tMilliSeconds), m_tpStart(chrono::system_clock::now()), m_fTimeOut(fTimeOut), m_bStop(false)
    {
        m_thWaitThread = thread([&]() {
            uint64_t  nDifMilliSec;
            do
            {
                this_thread::sleep_for(chrono::milliseconds(1));
                if (m_bStop == true)
                    return;
                chrono::system_clock::time_point m_tpNow = chrono::system_clock::now();
                nDifMilliSec = chrono::duration_cast<chrono::milliseconds>(m_tpNow - (chrono::system_clock::time_point)m_tpStart).count();
            } while (nDifMilliSec < m_tMilliSeconds);

            if (m_bStop == false && m_fTimeOut != 0)
                m_fTimeOut(this);
        });
    }

    virtual ~Timer()
    {
        m_bStop = true;
        if (m_thWaitThread.joinable() == true)
            m_thWaitThread.join();
    }

    void Reset()
    {
        atomic_init(&m_tpStart, chrono::system_clock::now());
    }

    void Stop()
    {
        m_bStop = true;
    }

private:
    uint32_t m_tMilliSeconds;
    atomic<chrono::system_clock::time_point> m_tpStart;
    function<void(Timer*)> m_fTimeOut;
    bool m_bStop;
    thread m_thWaitThread;
};
