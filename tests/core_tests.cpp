#include "core.h"
#include "storage.h"
#include <iostream>
#include <stdexcept>
#include <filesystem>

using namespace breakly;
int checks=0;
void expect(bool value,const char* message) {++checks;if(!value)throw std::runtime_error(message);}
Sample sample(Ms now,Ms idle=0,bool locked=false,bool sleep=false) {return {now,idle,20261115,6,600,locked,sleep};}
Scheduler working(Ms time) {Scheduler s;s.tick(sample(0));s.tick(sample(time));return s;}
int main() {
    try {
        auto s=working(20*minute);expect(s.due()==Kind::Eyes,"20 min eyes");
        s=working(45*minute);expect(s.due()==Kind::Stretch,"45 min stretch priority");
        s=working(60*minute);expect(s.due()==Kind::Long,"60 min long priority");
        expect(s.propose(Kind::Long)&&!s.propose(Kind::Eyes),"one outstanding notification");
        s.pause(15*minute);expect(!s.due(),"pause suppresses");s.tick(sample(75*minute));expect(s.due().has_value(),"pause expires");
        s=working(20*minute);s.propose(Kind::Eyes);s.snooze();expect(!s.due(),"snoozed");s.tick(sample(25*minute));expect(s.due()==Kind::Eyes,"snooze expires");
        s=working(minute);s.tick(sample(10*minute,9*minute));expect(s.today().screen==3*minute,"idle tail excluded");expect(s.restCandidate(),"idle candidate");expect(s.today().completed==0,"idle not confirmed");
        s=working(10*minute);s.tick(sample(10*minute,0,false,true));s.tick(sample(100*minute,0,false,true));s.tick(sample(100*minute));expect(s.today().screen==10*minute,"sleep excluded");expect(!s.due(),"no stale queue after sleep rest");
        s=working(10*minute);s.tick(sample(10*minute,0,true));s.tick(sample(30*minute,0,true));s.tick(sample(30*minute));expect(s.today().screen==10*minute,"lock excluded");
        s=working(60*minute);s.propose(Kind::Long);s.start(Kind::Long);expect(!s.complete(),"cannot complete early");s.tick(sample(65*minute));expect(s.complete(),"complete after duration");expect(!s.due()&&s.accrued[0]==0&&s.accrued[1]==0&&s.accrued[2]==0,"full rest satisfies all");expect(s.today().completed==1,"completed once");expect(!s.complete(),"no double complete");
        s=working(60*minute);s.start(Kind::Eyes);s.tick(sample(60*minute+20*second));expect(s.complete(),"eyes complete");expect(s.accrued[1]>=45*minute&&s.accrued[2]>=60*minute,"short rest preserves long timers");
        s=working(20*minute);s.propose(Kind::Eyes);s.skip();expect(s.today().completed==0&&s.today().skipped==1,"skip not complete");
        s=working(10*minute);auto v=sample(11*minute);v.minuteOfDay=1;v.date=20261114;s.tick(v);expect(s.accrued[0]==11*minute,"wall clock change independent");
        s=working(minute);auto config=s.settings;config.reading=true;s.reconfigure(config);s.tick(sample(10*minute,9*minute));expect(s.today().screen==10*minute,"reading counts");config.video=true;s.reconfigure(config);s.tick(sample(11*minute,10*minute));expect(s.today().screen==11*minute,"reading plus video no double count");
        config.schedule=true;config.days=1;config.workStart=22*60;config.workEnd=2*60;s.reconfigure(config);v=sample(12*minute);v.weekday=1;v.minuteOfDay=60;s.tick(v);expect(s.allowed(),"overnight schedule previous day");v.minuteOfDay=180;s.tick(v);expect(!s.allowed(),"outside schedule");
        Settings settings,parsed;settings.reading=true;settings.intervalMin[0]=17;expect(parseSettings(serializeSettings(settings),parsed)&&parsed.intervalMin[0]==17&&parsed.reading,"settings roundtrip");
        for(auto bad:{"broken","{\"interval0\":0}","{\"interval0\":20,\"interval0\":21}","{\"sound\":2}","{\"version\":2}","{\"days\":0}","{\"idleSec\":99999999999999999999999}","{\"interval0\":01}"})expect(!parseSettings(bad,parsed),"invalid JSON rejected");
        std::map<int,Daily> days;days[20261031].screen=minute;days[20261101].completed=2;std::map<int,Daily> loaded;
        expect(parseStatistics(serializeStatistics(days),loaded)&&loaded[20261101].completed==2,"statistics roundtrip");
        expect(!parseStatistics("{\"days\":[{\"date\":20261115,\"screen\":-1}]}",loaded),"invalid stats");
        s=Scheduler{};s.days=days;v=sample(0);v.date=20261102;s.tick(v);expect(s.week().screen==minute&&s.week().completed==2,"week across month boundary");
        s=working(20*minute);s.tick(sample(0));expect(s.accrued[0]==20*minute,"backwards monotonic protected");
        s=Scheduler{};v=sample(0);v.awake=0;s.tick(v);v=sample(60*minute);v.awake=minute;s.tick(v);expect(s.today().screen==minute,"missed power event cannot credit sleep");
        s=working(20*minute);s.propose(Kind::Eyes);s.snooze();s.tick(sample(45*minute));expect(s.due()==Kind::Stretch,"higher priority independent of snooze");
        s=working(20*minute);s.propose(Kind::Eyes);s.tick(sample(60*minute));expect(s.propose(Kind::Long)&&s.pending==Kind::Long,"long reminder replaces stale lower priority");
        s=working(20*minute);s.propose(Kind::Eyes);s.tick(sample(20*minute,0,true));s.tick(sample(30*minute));expect(!s.pending&&!s.due(),"enough lock rest cancels stale pending reminder");
        s=working(minute);s.pause(5*minute);s.tick(sample(6*minute));expect(s.today().screen==minute,"pause not credited");
        Settings bad=settings;bad.intervalMin[0]=0;s.reconfigure(bad);expect(s.settings.intervalMin[0]==20,"invalid reconfigure ignored");
        auto root=std::filesystem::temp_directory_path()/ ("breakly-tests-"+std::to_string(std::chrono::system_clock::now().time_since_epoch().count()));
        FileStore store(root);expect(store.saveSettings(settings)&&store.loadSettings(parsed)&&parsed.reading,"atomic file settings");expect(store.saveStatistics(days)&&store.loadStatistics(loaded),"atomic file stats");
        expect(store.write(L"settings.json","corrupt")&&!store.loadSettings(parsed)&&store.damaged,"damaged settings recovered without crash");
        // Test artefacts remain available for inspection, no user files are removed.
        std::cout<<checks<<" checks passed\n";return 0;
    } catch(const std::exception& e) {std::cerr<<"FAILED: "<<e.what()<<"\n";return 1;}
}
