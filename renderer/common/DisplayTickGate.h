#pragma once
#include <cstdint>
namespace threebody {
// Single-consumer, latest-slot gate. Caller owns synchronization.
// A tick received during frame preparation remains usable for its current period.
// Old ticks never form a queue, and one slot cannot authorize two submissions.
class DisplayTickGate {
  uint64_t serial_{0}, consumed_{0}, arrival_{0}, period_{8333333};
public:
  void reset() { consumed_=serial_; arrival_=0; }
  void signal(uint64_t now, uint64_t period) {
    arrival_=now;
    if(period>0) period_=period;
    ++serial_;
  }
  bool consume(uint64_t now) {
    if(serial_==consumed_ || now<arrival_ || now-arrival_>=period_)return false;
    consumed_=serial_;
    return true;
  }
};
}
