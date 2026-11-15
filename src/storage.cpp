#include "storage.h"
#include <charconv>
#include <fstream>
#include <sstream>
#include <vector>
#include <limits>
#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace breakly {
void LoggingService::error(const std::filesystem::path& root,const char* event) noexcept {
    try {
        std::error_code ec;std::filesystem::create_directories(root,ec);if(ec)return;
        auto file=root/L"errors.log";auto size=std::filesystem::file_size(file,ec);if(!ec && size>65536)return;
        std::ofstream out(file,std::ios::binary|std::ios::app);
        out<<std::chrono::system_clock::now().time_since_epoch().count()<<' '<<event<<'\n';
    }catch(...){} // Reporting an I/O error must never terminate the tray process.
}
namespace {
struct Json {
    enum Type { Number, Boolean, Object, Array, String, Null } type=Null;
    std::int64_t value=0;
    std::map<std::string,Json> object;
    std::vector<Json> array;
};
class Parser {
    const std::string& s; size_t p=0;
    void ws() {while(p<s.size() && (s[p]==' '||s[p]=='\r'||s[p]=='\n'||s[p]=='\t')) ++p;}
    bool consume(char c) {ws(); if(p<s.size() && s[p]==c){++p;return true;}return false;}
    bool str(std::string& out) {
        if(!consume('"')) return false;
        while(p<s.size()) {
            unsigned char c=static_cast<unsigned char>(s[p++]);
            if(c=='"') return true;
            if(c<32) return false;
            if(c=='\\') {
                if(p==s.size()) return false;
                char e=s[p++];
                if(e=='u') {for(int i=0;i<4;++i) {if(p==s.size() || !std::isxdigit(static_cast<unsigned char>(s[p++]))) return false;} out+='?';}
                else if(std::string("\"\\/bfnrt").find(e)==std::string::npos) return false;
                else out+=e;
            } else out+=static_cast<char>(c);
        }return false;
    }
    bool node(Json& j,int depth) {
        if(depth>12) return false;
        ws(); if(p==s.size())return false;
        if(s[p]=='{') {
            ++p; j.type=Json::Object; if(consume('}'))return true;
            do {std::string key;Json v;if(!str(key)||!consume(':')||!node(v,depth+1)||j.object.contains(key))return false;j.object.emplace(key,std::move(v));}while(consume(','));
            return consume('}');
        }
        if(s[p]=='[') {
            ++p;j.type=Json::Array;if(consume(']'))return true;
            do {Json v;if(!node(v,depth+1))return false;j.array.push_back(std::move(v));if(j.array.size()>1000)return false;}while(consume(','));
            return consume(']');
        }
        if(s[p]=='"') {std::string ignored;j.type=Json::String;return str(ignored);}
        for(auto [literal,type,value]:{std::tuple{"true",Json::Boolean,1},std::tuple{"false",Json::Boolean,0},std::tuple{"null",Json::Null,0}}) {
            size_t len=std::char_traits<char>::length(literal);
            if(s.compare(p,len,literal)==0) {p+=len;j.type=type;j.value=value;return true;}
        }
        size_t begin=p;if(s[p]=='-')++p;
        if(p==s.size() || s[p]<'0'||s[p]>'9')return false;
        if(s[p]=='0') ++p; else while(p<s.size() && s[p]>='0'&&s[p]<='9')++p;
        auto r=std::from_chars(s.data()+begin,s.data()+p,j.value);j.type=Json::Number;
        return r.ec==std::errc{} && r.ptr==s.data()+p;
    }
public:
    explicit Parser(const std::string& text):s(text){}
    bool parse(Json& j){if(s.size()>1024*1024)return false;bool ok=node(j,0);ws();return ok && p==s.size();}
};
bool number(const Json& j,const char* key,int& v) {
    auto it=j.object.find(key);if(it==j.object.end())return true;
    if(it->second.type!=Json::Number || it->second.value<std::numeric_limits<int>::min() || it->second.value>std::numeric_limits<int>::max())return false;
    v=static_cast<int>(it->second.value);return true;
}
bool boolean(const Json& j,const char* key,bool& v) {
    auto it=j.object.find(key);if(it==j.object.end())return true;
    if(it->second.type!=Json::Boolean)return false;v=it->second.value!=0;return true;
}
}
std::string serializeSettings(const Settings& s) {
    std::ostringstream o;o<<"{\n  \"version\": 1";
    auto n=[&](const char* k,int v){o<<",\n  \""<<k<<"\": "<<v;};
    auto b=[&](const char* k,bool v){o<<",\n  \""<<k<<"\": "<<(v?"true":"false");};
    for(int i=0;i<3;++i){n(("interval"+std::to_string(i)).c_str(),s.intervalMin[i]);n(("duration"+std::to_string(i)).c_str(),s.durationSec[i]);}
#define N(x) n(#x,s.x)
    N(idleSec);N(restSec);N(theme);N(notificationMode);N(repeatSec);N(gentleSec);N(monitor);N(fullscreen);
    N(workStart);N(workEnd);N(quietStart);N(quietEnd);N(days);
#undef N
#define B(x) b(#x,s.x)
    B(schedule);B(quiet);B(reading);B(video);B(sound);B(minimized);B(onboarded);B(lockRest);B(sleepRest);B(effects);B(nativeNotifications);
#undef B
    o<<"\n}\n";return o.str();
}
bool parseSettings(const std::string& text,Settings& out) {
    Json j;if(!Parser(text).parse(j)||j.type!=Json::Object)return false;
    Settings s; int version=1;if(!number(j,"version",version)||version!=1)return false;
    for(int i=0;i<3;++i) if(!number(j,("interval"+std::to_string(i)).c_str(),s.intervalMin[i])||!number(j,("duration"+std::to_string(i)).c_str(),s.durationSec[i]))return false;
#define N(x) if(!number(j,#x,s.x))return false
    N(idleSec);N(restSec);N(theme);N(notificationMode);N(repeatSec);N(gentleSec);N(monitor);N(fullscreen);N(workStart);N(workEnd);N(quietStart);N(quietEnd);N(days);
#undef N
#define B(x) if(!boolean(j,#x,s.x))return false
    B(schedule);B(quiet);B(reading);B(video);B(sound);B(minimized);B(onboarded);B(lockRest);B(sleepRest);B(effects);B(nativeNotifications);
#undef B
    if(!s.valid())return false;out=s;return true;
}
std::string serializeStatistics(const std::map<int,Daily>& days) {
    std::ostringstream o;o<<"{\"version\":1,\"days\":[";bool first=true;
    for(const auto& [date,d]:days) {if(!first)o<<',';first=false;o<<"{\"date\":"<<date;
#define F(x) o<<",\"" #x "\":"<<d.x
        F(input);F(screen);F(confirmedRest);F(longest);F(offered);F(started);F(completed);F(skipped);F(snoozed);
#undef F
        o<<'}';
    }o<<"]}\n";return o.str();
}
bool parseStatistics(const std::string& text,std::map<int,Daily>& out) {
    Json j;if(!Parser(text).parse(j)||j.type!=Json::Object)return false;
    int version=1;if(!number(j,"version",version)||version!=1)return false;
    auto it=j.object.find("days");if(it==j.object.end()||it->second.type!=Json::Array)return false;
    std::map<int,Daily> result;
    for(auto& row:it->second.array) {
        if(row.type!=Json::Object)return false;
        int date=0;if(!number(row,"date",date)||date<20200101||date>99991231||date%100<1||date%100>31||date/100%100<1||date/100%100>12)return false;
        Daily d;
        auto read=[&](const char* k,std::int64_t& v){auto i=row.object.find(k);if(i==row.object.end()||i->second.type!=Json::Number||i->second.value<0||i->second.value>3155760000000LL)return false;v=i->second.value;return true;};
#define F(x) if(!read(#x,d.x))return false
        F(input);F(screen);F(confirmedRest);F(longest);F(offered);F(started);F(completed);F(skipped);F(snoozed);
#undef F
        if(result.contains(date))return false;result.emplace(date,d);
    }out=std::move(result);return true;
}
std::string FileStore::read(const std::wstring& name) const {
    std::error_code ec;auto file=root_/name;
    auto size=std::filesystem::file_size(file,ec);if(ec||size>1024*1024)return {};
    std::ifstream in(file,std::ios::binary);return {std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>()};
}
void FileStore::preserve(const std::wstring& name) {
    damaged=true;std::error_code ec;
    auto suffix=L".damaged-"+std::to_wstring(std::chrono::system_clock::now().time_since_epoch().count());
    std::filesystem::copy_file(root_/name,root_/(name+suffix),ec);
    error=ec?L"Файл данных повреждён. Не удалось сохранить резервную копию.":L"Файл данных повреждён. Исходная копия сохранена рядом с ним.";
    LoggingService::error(root_,ec?"recovery-backup-failed":"damaged-data-preserved");
}
bool FileStore::loadSettings(Settings& s) {
    std::error_code ec;if(!std::filesystem::exists(root_/L"settings.json",ec))return true;
    if(parseSettings(read(L"settings.json"),s))return true;preserve(L"settings.json");return false;
}
bool FileStore::loadStatistics(std::map<int,Daily>& d) {
    std::error_code ec;if(!std::filesystem::exists(root_/L"statistics.json",ec))return true;
    if(parseStatistics(read(L"statistics.json"),d))return true;preserve(L"statistics.json");return false;
}
bool FileStore::write(const std::wstring& name,const std::string& data) {
    std::error_code ec;std::filesystem::create_directories(root_,ec);
    if(ec){error=L"Не удалось создать папку данных.";return false;}
    auto dest=root_/name,temp=root_/(name+L".tmp");
#ifdef _WIN32
    HANDLE h=CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(h==INVALID_HANDLE_VALUE){error=L"Не удалось открыть временный файл.";return false;}
    DWORD written=0;bool ok=WriteFile(h,data.data(),static_cast<DWORD>(data.size()),&written,nullptr) && written==data.size();
    if(ok)ok=FlushFileBuffers(h)!=FALSE;CloseHandle(h);
    if(ok)ok=MoveFileExW(temp.c_str(),dest.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;
#else
    std::ofstream f(temp,std::ios::binary|std::ios::trunc);f<<data;f.flush();bool ok=bool(f);f.close();
    if(ok){std::filesystem::rename(temp,dest,ec);ok=!ec;}
#endif
    if(!ok){error=L"Не удалось сохранить данные. Проверьте свободное место и доступ к папке.";LoggingService::error(root_,"atomic-write-failed");}
    return ok;
}
bool FileStore::saveSettings(const Settings& s) {return write(L"settings.json",serializeSettings(s));}
bool FileStore::saveStatistics(const std::map<int,Daily>& d) {return write(L"statistics.json",serializeStatistics(d));}
}
