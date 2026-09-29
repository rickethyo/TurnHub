#pragma once
#include <Arduino.h>
#include <map>
#include <functional>
constexpr int HTTP_GET=0, HTTP_POST=1;
using HTTPMethod = int;  // ESP32 core: enum from HTTP_Method.h
enum { UPLOAD_FILE_START, UPLOAD_FILE_WRITE, UPLOAD_FILE_END, UPLOAD_FILE_ABORTED };
struct HTTPUpload { int status=UPLOAD_FILE_START; uint8_t *buf=nullptr; size_t currentSize=0; String filename; };
struct TestHttpClient { size_t write(const uint8_t *, size_t n) { return n; } void stop() {} };
class WebServer {
 public:
  std::map<std::string,String> arguments, headers;
  std::map<std::string,std::function<void()>> routes;
  std::map<std::string,std::function<void()>> uploads;
  HTTPUpload uploadState;
  HTTPUpload &upload() { return uploadState; }
  TestHttpClient client() { return {}; }
  void setContentLength(size_t) {}
  template<class F, class U> void on(const char *path,int method,F f,U u) {
    routes[std::to_string(method)+path]=f; uploads[path]=u;
  }
  int status = 0;
  String body;
  explicit WebServer(int) {}
  void sendHeader(const char*,const String &) {}
  void send(int code,const char*,const String &value) { status=code; body=value; }
  void send_P(int code,const char *type,const char *value) { send(code,type,value); }
  String header(const char *key) { return headers[key]; }
  String arg(const char *key) { return arguments[key]; }
  bool hasArg(const char *key) { return arguments.count(key) != 0; }
  void collectHeaders(const char **,size_t) {}
  template<class F> void on(const char *path,int method,F f) { routes[std::to_string(method)+path]=f; }
  template<class F> void onNotFound(F) {}
  void begin() {}
  void handleClient() {}
};
