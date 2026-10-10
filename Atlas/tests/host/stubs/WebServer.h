#pragma once
#include <Arduino.h>
#include <map>
#include <functional>
#include <vector>
constexpr int HTTP_GET=0, HTTP_POST=1;
#define CONTENT_LENGTH_UNKNOWN ((size_t) -1)
using HTTPMethod = int;  // ESP32 core: enum from HTTP_Method.h
enum { UPLOAD_FILE_START, UPLOAD_FILE_WRITE, UPLOAD_FILE_END, UPLOAD_FILE_ABORTED };
struct HTTPUpload { int status=UPLOAD_FILE_START; uint8_t *buf=nullptr; size_t currentSize=0; String filename; };
struct TestHttpClient { size_t write(const uint8_t *, size_t n) { return n; } void stop() {} };
class WebServer;
// The ESP32 core's RequestHandler, reduced to what Atlas overrides.
class RequestHandler {
 public:
  virtual ~RequestHandler() {}
  virtual bool canHandle(HTTPMethod, String) { return false; }
  virtual bool handle(WebServer &, HTTPMethod, String) { return false; }
};
class WebServer {
 public:
  std::map<std::string,String> arguments, headers;
  std::map<std::string,std::function<void()>> routes;
  std::map<std::string,std::function<void()>> uploads;
  std::vector<RequestHandler *> handlers;
  void addHandler(RequestHandler *handler) { handlers.push_back(handler); }
  HTTPUpload uploadState;
  HTTPUpload &upload() { return uploadState; }
  TestHttpClient client() { return {}; }
  void setContentLength(size_t) {}
  template<class F, class U> void on(const char *path,int method,F f,U u) {
    routes[std::to_string(method)+path]=f; uploads[path]=u;
  }
  std::map<std::string,String> responseHeaders;
  String contentType;
  int status = 0;
  String body;
  explicit WebServer(int) {}
  void sendHeader(const char *name,const String &value) { responseHeaders[name]=value; }
  void send(int code,const char *type,const String &value) { status=code; contentType=type; body=value; }
  void send_P(int code,const char *type,const char *value) { send(code,type,value); }
  void send_P(int code,const char *type,const char *value,size_t size) { status=code; contentType=type; body.assign(value,size); }
  // Chunked responses append to the body sent with send().
  void sendContent(const String &value) { body+=value; }
  void sendContent(const char *value,size_t size) { body.append(value,size); }
  String header(const char *key) { return headers[key]; }
  String arg(const char *key) { return arguments[key]; }
  bool hasArg(const char *key) { return arguments.count(key) != 0; }
  void collectHeaders(const char **,size_t) {}
  template<class F> void on(const char *path,int method,F f) { routes[std::to_string(method)+path]=f; }
  template<class F> void onNotFound(F) {}
  void begin() {}
  void handleClient() {}
};
