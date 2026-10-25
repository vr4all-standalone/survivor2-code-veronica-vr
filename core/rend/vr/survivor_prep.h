// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <exception>
#include <string>
namespace vr::hands {
// Folder model indices are bounded below this range by the renderer.
constexpr int NativeSurvivorGunBase = 1024;
constexpr bool survivorKind(int kind) { return kind >= NativeSurvivorGunBase && kind < NativeSurvivorGunBase + 256; }
inline int survivorHandChoice(int player, bool automatic, bool enabled, bool ready, bool valid, int id, bool supported, int fallback) {
 return player==0 && automatic && enabled && ready && valid && id>0 && id<256 && supported ? NativeSurvivorGunBase+id : fallback;
}
// Emulation-thread lifecycle; presented() alone is called by the render thread.
// begin() is idempotent for a session, including skipped and failed preparation.
class SurvivorPreparation {
 std::array<bool,8> done{};
 bool attempted=false, needsPresentation=false;
 std::atomic<bool> framePresented{false};
public:
 enum class Phase { Off, Queued, Working, Ready, Skipped, Failed };
 Phase phase = Phase::Off;
 std::string error;
 void begin(bool enabled, const std::array<bool,8>& cached, bool presentationRequired) {
  if(attempted || !enabled)return;
  attempted=true;done=cached;needsPresentation=presentationRequired;framePresented=false;error.clear();
  phase=std::all_of(done.begin(),done.end(),[](bool v){return v;})?Phase::Ready:Phase::Queued;
 }
 void reset() {attempted=false;phase=Phase::Off;done.fill(false);framePresented=false;error.clear();}
 void presented() {framePresented=true;}
 bool preparing() const {return phase==Phase::Queued||phase==Phase::Working;}
 float progress() const {return float(std::count(done.begin(),done.end(),true))/done.size();}
 template<class Work> bool tick(bool skip, Work work) {
  if(!preparing())return false;
  if(skip){phase=Phase::Skipped;return false;}
  if(needsPresentation&&!framePresented.exchange(false))return false;
  if(phase==Phase::Queued){phase=Phase::Working;return false;}
  const size_t index=std::find(done.begin(),done.end(),false)-done.begin();
  try {
   if(!work(index,error)){phase=Phase::Failed;return false;}
  } catch(const std::exception& e){error=e.what();phase=Phase::Failed;return false;}
  done[index]=true;
  if(progress()==1.f){phase=Phase::Ready;return true;}
  return false;
 }
};
}
