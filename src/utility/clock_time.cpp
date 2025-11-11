//
// Created by cpasjuste on 23/10/2025.
//

#include "clock_time.h"

Time::Time() : m_microseconds(0) {}

float Time::asSeconds() const {
  return static_cast<float>(m_microseconds) / 1000000.f;
}

uint32_t Time::asMilliseconds() const {
  return static_cast<uint32_t>(m_microseconds / 1000);
}

uint64_t Time::asMicroseconds() const { return m_microseconds; }

Time::Time(const uint64_t microseconds) : m_microseconds(microseconds) {}

Time seconds(const float amount) {
  return Time(static_cast<long>(amount * 1000000));
}

Time milliseconds(const uint32_t amount) {
  return Time(static_cast<uint64_t>(amount) * 1000);
}

Time microseconds(const uint64_t amount) { return Time(amount); }

bool operator==(const Time left, const Time right) {
  return left.asMicroseconds() == right.asMicroseconds();
}

bool operator!=(const Time left, const Time right) {
  return left.asMicroseconds() != right.asMicroseconds();
}

bool operator<(const Time left, const Time right) {
  return left.asMicroseconds() < right.asMicroseconds();
}

bool operator>(const Time left, const Time right) {
  return left.asMicroseconds() > right.asMicroseconds();
}

bool operator<=(const Time left, const Time right) {
  return left.asMicroseconds() <= right.asMicroseconds();
}

bool operator>=(const Time left, const Time right) {
  return left.asMicroseconds() >= right.asMicroseconds();
}

Time operator-(const Time right) {
  return microseconds(-right.asMicroseconds());
}

Time operator+(const Time left, const Time right) {
  return microseconds(left.asMicroseconds() + right.asMicroseconds());
}

Time &operator+=(Time &left, const Time right) { return left = left + right; }

Time operator-(const Time left, const Time right) {
  return microseconds(left.asMicroseconds() - right.asMicroseconds());
}

Time &operator-=(Time &left, const Time right) { return left = left - right; }

Time operator*(const Time left, const float right) {
  return seconds(left.asSeconds() * right);
}

Time operator*(const Time left, const long right) {
  return microseconds(left.asMicroseconds() * right);
}

Time operator*(const float left, const Time right) { return right * left; }

Time operator*(const long left, const Time right) { return right * left; }

Time &operator*=(Time &left, const float right) { return left = left * right; }

Time &operator*=(Time &left, const long right) { return left = left * right; }

Time operator/(const Time left, const float right) {
  return seconds(left.asSeconds() / right);
}

Time operator/(const Time left, const long right) {
  return microseconds(left.asMicroseconds() / right);
}

Time &operator/=(Time &left, const float right) { return left = left / right; }

Time &operator/=(Time &left, const long right) { return left = left / right; }

float operator/(const Time left, const Time right) {
  return left.asSeconds() / right.asSeconds();
}

Time operator%(const Time left, const Time right) {
  return microseconds(left.asMicroseconds() % right.asMicroseconds());
}

Time &operator%=(Time &left, const Time right) { return left = left % right; }
