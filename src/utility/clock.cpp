//
// Created by cpasjuste on 23/10/2025.
//

#include <Arduino.h>
#include "clock.h"

Clock::Clock() {
    m_startTime = getCurrentTime();
}

Time Clock::getCurrentTime() {
    return microseconds(micros());
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
