#ifdef NDEBUG
#undef NDEBUG
#endif
#include "core/simulation/FrameRateJournal.h"
#include <atomic>
#include <cmath>
#include <cassert>
#include <sstream>
#include <vector>
#include <unistd.h>
#include <iostream>
using namespace threebody;
std::vector<std::vector<double>> rows(const std::string& file){
 std::ifstream f(file);std::string line;std::getline(f,line);assert(line.find("submitted_fps,render_fps")!=std::string::npos);assert(line.find("actual_display_fps")!=std::string::npos);
 std::vector<std::vector<double>> result;
 while(std::getline(f,line)){std::istringstream s(line);std::vector<double> v;for(int i=0;i<10;++i){std::string t;std::getline(s,t,',');v.push_back(std::stod(t));}result.push_back(v);}return result;
}
template<class F>void until(F f){auto end=std::chrono::steady_clock::now()+std::chrono::seconds(3);while(!f()){assert(std::chrono::steady_clock::now()<end);std::this_thread::sleep_for(std::chrono::milliseconds(10));}}
int main(){
 char path[]="/tmp/threebody-fps-XXXXXX";assert(mkdtemp(path));
 std::atomic<unsigned> calls{0},count{0};std::atomic<bool> running{true};
 auto sample=[&]{FrameRateSnapshot s;s.realFrames=count;s.displayFrames=2*count;s.running=running;s.acceleration="XEngine, enabled";s.generation="active";++calls;return s;};
 FrameRateJournal j;j.configure(path);assert(!j.hasRecording());j.start(true,sample);until([&]{return calls>=1;});count=30;
 until([&]{return j.hasRecording();});auto r=rows(j.exportFile());
 {std::ifstream f(j.exportFile());std::string h,row;std::getline(f,h);std::getline(f,row);assert(!row.empty()&&row.back()==',');} // unavailable stays blank, not invented FPS
assert(r.size()==1);assert(r[0][6]==60&&r[0][7]==30);assert(r[0][1]>=.49&&r[0][1]<2);assert(std::abs(r[0][3]-r[0][4]*2)<1e-6);
 // A stalled live renderer must emit zero, not repeat the preceding 120 FPS.
 until([&]{return calls>=3;});std::this_thread::sleep_for(std::chrono::milliseconds(20));r=rows(j.exportFile());assert(r.back()[3]==0&&r.back()[9]==1);
 running=false;until([&]{return calls>=4;});std::this_thread::sleep_for(std::chrono::milliseconds(20));r=rows(j.exportFile());assert(r.back()[3]==0&&r.back()[9]==0);
 j.stop();auto n=r.size();auto last=r.back()[0];
 j.start(false,sample);until([&]{return calls>=5;});count=40;until([&]{return calls>=6;});std::this_thread::sleep_for(std::chrono::milliseconds(20));j.stop();r=rows(j.exportFile());assert(r.size()==n+1&&r.back()[2]==2&&r.back()[0]>last&&r.back()[7]==10);
 {FrameRateJournal reopened;reopened.configure(path);assert(reopened.hasRecording());assert(rows(reopened.exportFile()).size()==r.size());}
 j.start(true,sample);until([&]{return calls>=7;});until([&]{return j.hasRecording();});j.stop();r=rows(j.exportFile());assert(r.size()==1&&r[0][2]==1);
 unlink((std::string(path)+"/fps-latest.csv").c_str());unlink((std::string(path)+"/fps-export.csv").c_str());rmdir(path);
 std::cout<<"PASS: half-second counts, stalls, pauses, generation split, continuation, persistence, fresh session\n";
}
