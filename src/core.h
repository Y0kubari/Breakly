#pragma once
#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>

namespace breakly {
using Ms = std::int64_t;
constexpr Ms second = 1000, minute = 60 * second;
enum class Kind : int { Eyes, Stretch, Long };
enum class State { Working, Idle, BreakDue, BreakActive, Snoozed, Paused, Suspended, Locked };
struct Settings {
    std::array<int, 3> intervalMin{20,45,60};
    std::array<int, 3> durationSec{20,90,300};
    int idleSec=120, restSec=300, theme=0, notificationMode=1;
    int repeatSec=300, gentleSec=20, monitor=0, fullscreen=1;
    int workStart=540, workEnd=1080, quietStart=1320, quietEnd=480, days=31;
    bool schedule=false, quiet=false, reading=false, video=false;
    bool sound=false, minimized=false, onboarded=false;
    bool lockRest=true, sleepRest=true, effects=true, nativeNotifications=false;
    bool valid() const;
};
struct Daily {
    Ms input=0, screen=0, confirmedRest=0, longest=0;
    std::int64_t offered=0, started=0, completed=0, skipped=0, snoozed=0;
};
struct Sample {
    Ms now=0, idle=0;
    int date=0, weekday=0, minuteOfDay=0; // Monday = 0; local calendar only.
    bool locked=false, suspended=false;
    Ms awake=-1; // QueryUnbiasedInterruptTime, optional in deterministic tests.
};
struct IClock { virtual ~IClock()=default; virtual Sample sample()=0; };
struct IActivity { virtual ~IActivity()=default; virtual Ms idle()=0; };
struct INotifications { virtual ~INotifications()=default; virtual void show(Kind)=0; };
struct IStore {
    virtual ~IStore()=default;
    virtual bool loadSettings(Settings&)=0;
    virtual bool saveSettings(const Settings&)=0;
    virtual bool loadStatistics(std::map<int,Daily>&)=0;
    virtual bool saveStatistics(const std::map<int,Daily>&)=0;
};
class Scheduler {
public:
    Settings settings;
    std::map<int,Daily> days;
    State state=State::Working;
    std::array<Ms,3> accrued{};
    std::optional<Kind> pending, active;
    Ms pauseUntil=0;
    bool dirty=false;
    void tick(const Sample&);
    std::optional<Kind> due() const;
    bool propose(Kind);
    void start(Kind);
    bool complete();
    void skip();
    void snooze(Ms duration=5*minute);
    void pause(Ms duration);
    void resume();
    void reconfigure(const Settings&);
    Ms remaining(Kind) const;
    Ms breakRemaining() const;
    Ms nextDelay() const;
    bool allowed() const;
    Daily today() const;
    Daily week() const;
    bool restCandidate() const { return current_.idle >= settings.restSec * second; }
private:
    Sample current_{};
    bool initialized_=false;
    std::array<Ms,3> snoozeUntil_{};
    Ms breakStart_=0, breakEnd_=0, blockedStart_=0, continuous_=0;
    bool blocked_=false, wasSleeping_=false;
    void applyRest(Ms);
    Daily& day();
    void updateState();
};
std::string serializeSettings(const Settings&);
bool parseSettings(const std::string&,Settings&);
std::string serializeStatistics(const std::map<int,Daily>&);
bool parseStatistics(const std::string&,std::map<int,Daily>&);
}
