#include "TrajectoryJournal.h"
#include "StellarUnits.h"
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>
namespace threebody {
namespace {
constexpr size_t chunkSize = 4096, capacity = 4096, quota = 256 * 1024 * 1024;
std::string row(const ThreeBodyState<double> &s) {
  std::ostringstream o;
  o.imbue(std::locale::classic());
  o << std::setprecision(17) << '[' << s.simulationTime;
  for (const auto &b : s.body)
    o << ',' << b.mass << ',' << b.position.x << ',' << b.position.y << ','
      << b.position.z << ',' << b.velocity.x << ',' << b.velocity.y << ','
      << b.velocity.z;
  if(s.planetEnabled){const auto& b=s.planet;o << ',' << b.mass << ',' << b.position.x << ',' << b.position.y << ',' << b.position.z << ',' << b.velocity.x << ',' << b.velocity.y << ',' << b.velocity.z;}
  return o.str() + "]\n";
}
} // namespace
TrajectoryJournal::~TrajectoryJournal() { finish(); }
void TrajectoryJournal::configure(std::string directory) {
  std::lock_guard<std::recursive_mutex> session(sessionMutex_);
  finish();
  std::lock_guard<std::mutex> l(mutex_);
  directory_ = std::move(directory);
  base_.clear();
  samples_ = bytes_ = previousBytes_ = 0;
  message_.clear();
  limited_ = false;
  if (directory_.empty())
    return;
  std::ifstream index(directory_ + "/latest.index");
  std::string name;
  if (std::getline(index, name) && name.find('/') == std::string::npos &&
      name.find("run-") == 0) {
    base_ = directory_ + '/' + name;
    index >> samples_ >> bytes_ >> previousBytes_ >> limited_;
    if (!index) {
      base_.clear();
      samples_ = bytes_ = previousBytes_ = 0;
      return;
    }
    std::ifstream meta(base_ + ".meta");
    std::getline(meta, metadata_);
    std::ifstream event(base_ + ".event");
    if (!std::getline(event, reason_))
      reason_ = "上次运行已结束；可能未正常关闭";
    message_ = limited_ ? "记录已停止，请导出已保存部分" : "已恢复上次轨迹记录";
  }
}
void TrajectoryJournal::begin(const std::string &metadata,
                              const ThreeBodyState<double> &initial) {
  std::lock_guard<std::recursive_mutex> session(sessionMutex_);
  finish();
  {
    std::lock_guard<std::mutex> l(mutex_);
    if (directory_.empty())
      return;
    if (previousBytes_ + bytes_ >= quota) {
      limited_ = true;
      message_ = "记录空间达到 256 MiB 上限，请导出上一段记录";
      return;
    }
    previousBytes_ += bytes_;
    bytes_ = samples_ = 0;
    limited_ = false;
    closing_ = false;
    busy_ = false;
    queue_.clear();
    metadata_ = metadata;
    reason_ = "运行中";
    reasonDirty_ = true;
    auto id = std::chrono::duration_cast<std::chrono::microseconds>(
                  std::chrono::system_clock::now().time_since_epoch())
                  .count();
    base_ = directory_ + "/run-" + std::to_string(id);
    if (previousBytes_ >= quota) {
      limited_ = true;
      message_ = "记录空间达到 256 MiB 上限，请先导出并清理应用记录";
      return;
    }
    std::ofstream meta(base_ + ".meta");
    meta << metadata_ << '\n';
    meta.flush();
    if (!meta) {
      limited_ = true;
      message_ = "无法写入轨迹记录，模拟仍可继续";
      return;
    }
    message_ = "自动记录中 · 与渲染帧率无关";
    queue_.push_back(initial);
  }
  thread_ = std::thread(&TrajectoryJournal::writeLoop, this);
}
void TrajectoryJournal::append(const ThreeBodyState<double> &s) {
  std::lock_guard<std::mutex> l(mutex_);
  if (!thread_.joinable() || closing_ || limited_)
    return;
  if (queue_.size() >= capacity) {
    limited_ = true;
    message_ = "记录写入跟不上，已停止记录；请导出已保存部分";
    return;
  }
  queue_.push_back(s);
  cv_.notify_all();
}
void TrajectoryJournal::setReason(const std::string &reason) {
  std::lock_guard<std::mutex> l(mutex_);
  if (thread_.joinable()) {
    reason_ = reason;
    reasonDirty_ = true;
    cv_.notify_all();
  }
}
void TrajectoryJournal::flush() {
  std::unique_lock<std::mutex> l(mutex_);
  cv_.notify_all();
  cv_.wait(l, [&] {
    return queue_.empty() && !busy_ && (!reasonDirty_ || !thread_.joinable());
  });
}
void TrajectoryJournal::finish() {
  std::lock_guard<std::recursive_mutex> session(sessionMutex_);
  {
    std::lock_guard<std::mutex> l(mutex_);
    closing_ = true;
    cv_.notify_all();
  }
  if (thread_.joinable())
    thread_.join();
}
void TrajectoryJournal::saveIndex() {
  std::string contents;
  {
    std::lock_guard<std::mutex> l(mutex_);
    const auto slash = base_.find_last_of('/');
    std::ostringstream value;
    value << base_.substr(slash + 1) << '\n'
          << samples_ << ' ' << bytes_ << ' ' << previousBytes_ << ' '
          << limited_ << '\n';
    contents = value.str();
  }
  std::ofstream o(directory_ + "/latest.tmp");
  o << contents;
  o.close();
  if (!o || std::rename((directory_ + "/latest.tmp").c_str(),
                        (directory_ + "/latest.index").c_str()))
    throw std::runtime_error("无法保存轨迹索引");
}
void TrajectoryJournal::writeLoop() {
  try {
    for (;;) {
      std::deque<ThreeBodyState<double>> batch;
      {
        std::unique_lock<std::mutex> l(mutex_);
        cv_.wait_for(l, std::chrono::milliseconds(100), [&] {
          return closing_ || reasonDirty_ || queue_.size() >= 64;
        });
        if (queue_.empty() && !reasonDirty_ && closing_)
          break;
        if (queue_.empty() && !reasonDirty_)
          continue;
        batch.swap(queue_);
        busy_ = true;
      }
      std::ofstream out;
      size_t openChunk = size_t(-1);
      for (const auto &state : batch) {
        const auto line = row(state);
        if (previousBytes_ + bytes_ + line.size() > quota) {
          std::lock_guard<std::mutex> l(mutex_);
          limited_ = true;
          message_ = "记录空间达到 256 MiB 上限，模拟仍可继续";
          break;
        }
        if (openChunk != samples_ / chunkSize) {
          out.close();
          out.clear();
          openChunk = samples_ / chunkSize;
          out.open(base_ + "." + std::to_string(openChunk) + ".jsonl",
                   std::ios::app);
        }
        out << line;
        if (!out)
          throw std::runtime_error("轨迹写入失败，模拟仍可继续");
        {
          std::lock_guard<std::mutex> l(mutex_);
          ++samples_;
          bytes_ += line.size();
        }
      }
      if (out.is_open()) {
        out.close();
        if (!out)
          throw std::runtime_error("轨迹写入失败，模拟仍可继续");
      }
      saveIndex();
      {
        std::string reason;
        {
          std::lock_guard<std::mutex> l(mutex_);
          reason = reason_;
          reasonDirty_ = false;
        }
        std::ofstream event(base_ + ".event");
        event << reason << '\n';
        event.close();
        if (!event)
          throw std::runtime_error("记录状态写入失败");
      }
      {
        std::lock_guard<std::mutex> l(mutex_);
        if (limited_)
          queue_.clear();
        busy_ = false;
        cv_.notify_all();
      }
    }
    if (!base_.empty())
      saveIndex();
  } catch (const std::exception &e) {
    std::lock_guard<std::mutex> l(mutex_);
    limited_ = true;
    message_ = e.what();
    queue_.clear();
    busy_ = false;
    reasonDirty_ = false;
    cv_.notify_all();
  }
}
std::string TrajectoryJournal::message() const {
  std::lock_guard<std::mutex> l(mutex_);
  return message_;
}
bool TrajectoryJournal::hasRecording() const {
  std::lock_guard<std::mutex> l(mutex_);
  return samples_ > 0;
}
std::string TrajectoryJournal::exportFile(bool csv) {
  std::lock_guard<std::recursive_mutex> session(sessionMutex_);
  flush();
  std::string base, meta, reason;
  size_t count;
  bool limited;
  {
    std::lock_guard<std::mutex> l(mutex_);
    base = base_;
    meta = metadata_;
    count = samples_;
    limited = limited_;
    reason = reason_;
  }
  if (!count)
    throw std::runtime_error("没有已保存的轨迹");
  std::string destination = directory_ + (csv ? "/export.csv" : "/export.json");
  std::ofstream out(destination);
  out.imbue(std::locale::classic());
  out << std::setprecision(17);
  const bool planet=meta.find("\"planetEnabled\":true")!=std::string::npos;
  if (csv) {
    out << "time_internal,time_days";
    for (int i = 1; i <= 3; ++i)
      for (const auto *name : {"mass_solar", "x_AU", "y_AU", "z_AU", "vx_km_s",
                               "vy_km_s", "vz_km_s"})
        out << ",star" << i << '_' << name;
    if(planet)for(const auto* field : {"mass_solar","x_AU","y_AU","z_AU","vx_km_s","vy_km_s","vz_km_s"})out << ",planet_" << field;
    out << '\n';
  } else {
    if (meta.empty() || meta.back() != '}')
      throw std::runtime_error("记录元数据损坏");
    meta.pop_back();
    std::string escaped;
    for (char c : reason) {
      if (c == '"' || c == '\\')
        escaped += '\\';
      if (c == '\n') {
        escaped += "\\n";
        continue;
      }
      if (static_cast<unsigned char>(c) < 32)
        continue;
      escaped += c;
    }
    out << meta << ",\"stopReason\":\"" << escaped
        << "\",\"truncated\":" << (limited ? "true" : "false")
        << ",\"samples\":[";
  }
  std::ifstream in;
  std::string line;
  for (size_t i = 0; i < count; ++i) {
    if (i % chunkSize == 0) {
      in.close();
      in.clear();
      in.open(base + "." + std::to_string(i / chunkSize) + ".jsonl");
    }
    if (!std::getline(in, line) || line.size() < 3 || line.front() != '[' ||
        line.back() != ']')
      throw std::runtime_error("轨迹片段不完整");
    if (csv) {
      for (char &c : line)
        if (c == '[' || c == ']' || c == ',')
          c = ' ';
      std::istringstream values(line);
      values.imbue(std::locale::classic());
      double t, v;
      values >> t;
      out << t << ',' << t * stellar::timeDays();
      for (int k = 0; k < (planet?28:21); ++k) {
        if (!(values >> v))
          throw std::runtime_error("轨迹数据损坏");
        if (k % 7 >= 4)
          v *= stellar::velocityKmS();
        out << ',' << v;
      }
      out << '\n';
    } else
      out << (i ? "," : "") << line;
  }
  if (!csv)
    out << "]}\n";
  out.close();
  if (!out)
    throw std::runtime_error("导出文件写入失败");
  return destination;
}
} // namespace threebody
