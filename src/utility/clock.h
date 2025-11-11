//
// Created by cpasjuste on 23/10/2025.
//

#ifndef ZEDMD_CLOCK_H
#define ZEDMD_CLOCK_H

#include "clock_time.h"

class Clock {
 public:
  Clock();

  static Time getCurrentTime();

  [[nodiscard]] Time getElapsedTime() const;

  Time restart();

 protected:
  Time m_startTime;
};

#endif  // ZEDMD_CLOCK_H
