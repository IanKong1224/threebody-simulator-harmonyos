#pragma once
#include "core/physics/ThreeBodyState.h"
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
namespace threebody {
// Portable buffered journal: caller supplies an existing private directory.
// Physics only enqueues states; formatting and disk IO run on the writer.
class TrajectoryJournal {
public:
  ~TrajectoryJournal();
  void configure(std::string directory);
  void begin(const std::string &metadata,
             const ThreeBodyState<double> &initial);
  void append(const ThreeBodyState<double> &state);
  void flush();
  void setReason(const std::string &reason);
  void finish();
  std::string exportFile(bool csv);
  std::string message() const;
  bool hasRecording() const;

private:
  void writeLoop();
  void saveIndex();
  std::recursive_mutex sessionMutex_;
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::thread thread_;
  std::deque<ThreeBodyState<double>> queue_;
  std::string directory_, base_, metadata_, message_, reason_{"运行中"};
  size_t samples_{0}, bytes_{0}, previousBytes_{0};
  bool closing_{false}, busy_{false}, limited_{false}, reasonDirty_{false};
};
} // namespace threebody
