//
// Created by cpasjuste on 23/10/2025.
//

#include "clock.h"

#include <Arduino.h>

#include <atomic>

Clock::Clock() { m_startTime = getCurrentTime(); }

Time Clock::getCurrentTime() { return microseconds(micros()); }

Time Clock::getElapsedTime() const {
  // Read the start time before the current time. Another task can restart()
  // the clock in between; reading it the other way round can then give a
  // start time after "now", and the unsigned difference wraps to a huge value
  // that looks like a connection timeout.
  const Time start = m_startTime;
  std::atomic_signal_fence(std::memory_order_seq_cst);
  return getCurrentTime() - start;
}

Time Clock::restart() {
  const Time now = getCurrentTime();
  const Time elapsed = now - m_startTime;
  m_startTime = now;
  return elapsed;
}
