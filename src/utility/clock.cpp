//
// Created by cpasjuste on 23/10/2025.
//

#include <sys/time.h>
#include "clock.h"

Clock::Clock() {
    m_startTime = getCurrentTime();
}

Time Clock::getCurrentTime() {
    timeval time{};
    gettimeofday(&time, nullptr);
    return microseconds(static_cast<long>(1000000) * time.tv_sec + time.tv_usec);
}

Time Clock::getElapsedTime() const {
    return getCurrentTime() - m_startTime;
}

Time Clock::restart() {
    const Time now = getCurrentTime();
    const Time elapsed = now - m_startTime;
    m_startTime = now;
    return elapsed;
}
