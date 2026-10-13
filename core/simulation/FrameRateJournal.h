#pragma once
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <functional>
#include <iomanip>
#include <locale>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <cstdint>
namespace threebody {
struct FrameRateSnapshot {
  uint64_t realFrames{0}, displayFrames{0};
  double simulationTime{0};
  bool running{false};
  std::string acceleration, generation;
  double actualDisplayFps{-1};
};
// A separate wall-clock sampler: even stalled rendering produces a zero-FPS row.
// No formatting or file writes happen on the render/physics threads.
class FrameRateJournal {
  using Clock=std::chrono::steady_clock;
  mutable std::mutex mutex_;
  std::condition_variable wake_;
  std::thread thread_;
  std::string directory_, message_{"开始模拟后自动记录帧率"};
  bool stopping_{false}, hasRows_{false};
  Clock::time_point origin_{};
  uint64_t segment_{0};
  static std::string quote(const std::string& s){std::string out="\"";for(char c:s){if(c=='\"')out+='\"';out+=c;}return out+'\"';}
public:
  ~FrameRateJournal(){stop();}
  void configure(const std::string& directory){
    stop();std::lock_guard<std::mutex> l(mutex_);directory_=directory;
    std::ifstream f(directory_+"/fps-latest.csv");std::string header,row;
    hasRows_=bool(std::getline(f,header)&&std::getline(f,row));
    if(hasRows_)message_="已有上次帧率记录，可导出 CSV";
  }
  void stop(){
    {std::lock_guard<std::mutex> l(mutex_);stopping_=true;}wake_.notify_all();
    if(thread_.joinable())thread_.join();
  }
  void start(bool fresh,std::function<FrameRateSnapshot()> sample){
    stop();
    std::lock_guard<std::mutex> l(mutex_);
    if(directory_.empty())return;
    if(fresh){origin_=Clock::now();segment_=0;hasRows_=false;}
    if(origin_==Clock::time_point{})origin_=Clock::now();
    ++segment_;stopping_=false;message_="每 0.5 秒记录窗口平均帧率；暂停也记录";
    thread_=std::thread([this,fresh,sample]{
      auto before=sample();auto previous=Clock::now();
      std::ofstream out;
      {
        std::lock_guard<std::mutex> l(mutex_);
        out.open(directory_+"/fps-latest.csv",fresh?std::ios::trunc:std::ios::app);
        out.imbue(std::locale::classic());out<<std::setprecision(10);
        if(fresh)out<<"elapsed_seconds,interval_seconds,segment,submitted_fps,render_fps,generated_submitted_fps,submitted_frames,render_frames,simulation_time_internal,running,xengine_status,graphics_status,actual_display_fps\n";
        if(!out){message_="帧率记录写入失败";return;}
      }
      for(;;){
        {
          std::unique_lock<std::mutex> l(mutex_);
          if(wake_.wait_until(l,previous+std::chrono::milliseconds(500),[this]{return stopping_;}))break;
        }
        // Do not hold the journal lock while entering the simulator lock.
        auto value=sample();auto now=Clock::now();
        double dt=std::chrono::duration<double>(now-previous).count();
        uint64_t real=value.realFrames>=before.realFrames?value.realFrames-before.realFrames:0;
        uint64_t display=value.displayFrames>=before.displayFrames?value.displayFrames-before.displayFrames:0;
        {
          std::lock_guard<std::mutex> l(mutex_);
          if(out.tellp()>=32*1024*1024){message_="帧率记录达到 32 MiB 上限；模拟继续";break;}
          out<<std::chrono::duration<double>(now-origin_).count()<<','<<dt<<','<<segment_<<','
             <<display/dt<<','<<real/dt<<','<<(display>=real?display-real:0)/dt<<','<<display<<','<<real<<','
             <<value.simulationTime<<','<<(value.running?1:0)<<','<<quote(value.acceleration)<<','<<quote(value.generation)<<',';
          if(value.actualDisplayFps>=0) out<<value.actualDisplayFps;
          out<<'\n';
          out.flush();
          if(!out){message_="帧率记录写入失败；模拟继续";break;}
          hasRows_=true;
        }
        before=value;previous=now;
      }
    });
  }
  bool hasRecording()const{std::lock_guard<std::mutex> l(mutex_);return hasRows_;}
  std::string message()const{std::lock_guard<std::mutex> l(mutex_);return message_;}
  std::string exportFile(){
    std::lock_guard<std::mutex> l(mutex_);
    if(!hasRows_)throw std::runtime_error("尚无帧率记录，请先运行至少半秒");
    std::ifstream in(directory_+"/fps-latest.csv",std::ios::binary);
    std::string path=directory_+"/fps-export.csv";
    std::ofstream out(path,std::ios::binary|std::ios::trunc);out<<in.rdbuf();out.flush();
    if(!in||!out)throw std::runtime_error("帧率导出失败");
    return path;
  }
};
}
