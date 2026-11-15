#include "core.h"
#include <algorithm>
#include <limits>

namespace breakly {
bool Settings::valid() const {
    for(int v:intervalMin) if(v<1 || v>1440) return false;
    for(int v:durationSec) if(v<5 || v>3600) return false;
    return idleSec>=15 && idleSec<=3600 && restSec>=idleSec && restSec<=7200 &&
        theme>=0 && theme<=2 && notificationMode>=0 && notificationMode<=2 &&
        repeatSec>=15 && repeatSec<=3600 && gentleSec>=5 && gentleSec<=300 &&
        monitor>=0 && monitor<=32 && fullscreen>=0 && fullscreen<=2 &&
        workStart>=0 && workStart<1440 && workEnd>=0 && workEnd<1440 &&
        quietStart>=0 && quietStart<1440 && quietEnd>=0 && quietEnd<1440 && days>=1 && days<=127;
}
static bool inRange(int m,int start,int end) {
    if(start==end) return true;
    return start<end ? m>=start && m<end : m>=start || m<end;
}
bool Scheduler::allowed() const {
    if(settings.quiet && inRange(current_.minuteOfDay,settings.quietStart,settings.quietEnd)) return false;
    int weekday=current_.weekday;
    if(settings.workStart>settings.workEnd && current_.minuteOfDay<settings.workEnd) weekday=(weekday+6)%7;
    return !settings.schedule || ((settings.days & (1<<weekday)) &&
        inRange(current_.minuteOfDay,settings.workStart,settings.workEnd));
}
Daily& Scheduler::day() { return days[current_.date]; }
void Scheduler::tick(const Sample& s) {
    if(!initialized_) { current_=s; initialized_=true; }
    Ms dt=std::max<Ms>(0,s.now-current_.now);
    if(s.awake>=0 && current_.awake>=0)dt=std::min(dt,std::max<Ms>(0,s.awake-current_.awake));
    bool previousBlocked=current_.locked || current_.suspended;
    bool currentBlocked=s.locked || s.suspended;
    if(!previousBlocked && !currentBlocked && !active && current_.now>=pauseUntil && allowed()) {
        // Infer the last input boundary; do not credit the idle tail of this interval.
        Ms screen=dt;
        if(!settings.reading && !settings.video) {
            Ms lastInput=s.now-std::max<Ms>(0,s.idle);
            Ms end=std::min(s.now,lastInput+settings.idleSec*second);
            screen=std::clamp(end-current_.now,Ms(0),dt);
        }
        Ms inputEnd=std::min(s.now,s.now-std::max<Ms>(0,s.idle)+15*second);
        Ms input=std::clamp(inputEnd-current_.now,Ms(0),dt);
        auto& d=day(); d.screen+=screen; d.input+=input;
        continuous_+=screen; d.longest=std::max(d.longest,continuous_);
        for(auto& v:accrued) v=std::min(v+screen,Ms(7*24*60)*minute);
        dirty=dirty || screen>0 || input>0;
    }
    if(currentBlocked && !blocked_) { blockedStart_=s.now; wasSleeping_=s.suspended; blocked_=true; }
    if(currentBlocked) wasSleeping_=wasSleeping_ || s.suspended;
    current_=s;
    if(!currentBlocked && blocked_) {
        if((wasSleeping_ && settings.sleepRest) || (!wasSleeping_ && settings.lockRest)) applyRest(s.now-blockedStart_);
        blocked_=false;
    }
    // Inactivity is only a candidate for rest; it never confirms completion.
    if(!settings.reading && !settings.video && s.idle>=settings.restSec*second) continuous_=0;
    updateState();
}
void Scheduler::updateState() {
    if(current_.suspended) state=State::Suspended;
    else if(current_.locked) state=State::Locked;
    else if(active) state=State::BreakActive;
    else if(current_.now<pauseUntil || !allowed()) state=State::Paused;
    else if(pending) state=State::BreakDue;
    else if(due()) state=State::BreakDue;
    else if(std::any_of(snoozeUntil_.begin(),snoozeUntil_.end(),[&](Ms v){return v>current_.now;})) state=State::Snoozed;
    else if(!settings.reading && !settings.video && current_.idle>=settings.idleSec*second) state=State::Idle;
    else state=State::Working;
}
std::optional<Kind> Scheduler::due() const {
    if(active || current_.locked || current_.suspended || current_.now<pauseUntil || !allowed()) return {};
    if(!settings.reading && !settings.video && current_.idle>=settings.idleSec*second) return {};
    for(int i=2;i>=0;--i) if(accrued[i]>=settings.intervalMin[i]*minute && current_.now>=snoozeUntil_[i]) return Kind(i);
    return {};
}
bool Scheduler::propose(Kind k) {
    if(active || (pending && int(k)<=int(*pending))) return false;
    pending=k; ++day().offered; dirty=true; updateState(); return true;
}
void Scheduler::start(Kind k) {
    if(active) return;
    active=k; pending.reset(); breakStart_=current_.now;
    breakEnd_=breakStart_+settings.durationSec[int(k)]*second;
    ++day().started; dirty=true; updateState();
}
void Scheduler::applyRest(Ms duration) {
    for(int i=0;i<3;++i) if(duration>=settings.durationSec[i]*second) {accrued[i]=0; snoozeUntil_[i]=0;}
    if(duration>=settings.restSec*second) continuous_=0;
    if(pending && duration>=settings.durationSec[int(*pending)]*second)pending.reset();
}
bool Scheduler::complete() {
    if(!active || current_.now<breakEnd_ || current_.locked || current_.suspended) return false;
    Ms duration=breakEnd_-breakStart_;
    applyRest(duration);
    ++day().completed; day().confirmedRest+=duration;
    active.reset(); dirty=true; updateState(); return true;
}
void Scheduler::skip() {
    auto k=active ? active : pending;
    if(!k) return;
    accrued[int(*k)]=0; snoozeUntil_[int(*k)]=0;
    active.reset(); pending.reset(); ++day().skipped; dirty=true; updateState();
}
void Scheduler::snooze(Ms duration) {
    if(!pending) return;
    snoozeUntil_[int(*pending)]=current_.now+duration;
    pending.reset(); ++day().snoozed; dirty=true; updateState();
}
void Scheduler::pause(Ms duration) {
    if(pending) snooze(duration);
    pauseUntil=current_.now+duration; updateState();
}
void Scheduler::resume() { pauseUntil=0; updateState(); }
void Scheduler::reconfigure(const Settings& s) { if(s.valid()) settings=s; updateState(); }
Ms Scheduler::remaining(Kind k) const {
    int i=int(k);
    return std::max({Ms(0),settings.intervalMin[i]*minute-accrued[i],snoozeUntil_[i]-current_.now});
}
Ms Scheduler::breakRemaining() const { return active ? std::max<Ms>(0,breakEnd_-current_.now) : 0; }
Ms Scheduler::nextDelay() const {
    Ms result=15*second; // At most one cheap activity sample per 15 s.
    if(active && breakRemaining()>0) result=std::min(result,breakRemaining());
    if(pauseUntil>current_.now) result=std::min(result,pauseUntil-current_.now);
    if(!active && !pending && allowed()) for(int i=0;i<3;++i) if(remaining(Kind(i))>0) result=std::min(result,remaining(Kind(i)));
    return std::max<Ms>(100,result);
}
Daily Scheduler::today() const {
    auto it=days.find(current_.date); return it==days.end() ? Daily{} : it->second;
}
Daily Scheduler::week() const {
    Daily d;
    // Caller supplies YYYYMMDD; use civil-calendar conversion to handle month boundaries.
    auto serial=[](int date) {
        int y=date/10000,m=date/100%100,dayValue=date%100;
        y-=m<=2; int era=y/400; unsigned yo=unsigned(y-era*400);
        unsigned mp=unsigned(m+(m>2?-3:9));
        return era*146097+int(yo*365+yo/4-yo/100+(153*mp+2)/5+unsigned(dayValue)-1);
    };
    int now=serial(current_.date);
    for(auto& [date,v]:days) if(int delta=now-serial(date);delta>=0 && delta<7) {
        d.input+=v.input; d.screen+=v.screen; d.confirmedRest+=v.confirmedRest;
        d.longest=std::max(d.longest,v.longest); d.offered+=v.offered; d.started+=v.started;
        d.completed+=v.completed; d.skipped+=v.skipped; d.snoozed+=v.snoozed;
    }
    return d;
}
}
