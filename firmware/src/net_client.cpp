#include "net_client.h"

#include <cstdlib>
#include <cstring>

#ifdef ARDUINO
#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include "config.h"
#include "display.h"
#endif

namespace net_client {
#ifdef ARDUINO
namespace {
template <typename Fn>
bool retry(Fn fn) {
  for (int attempt = 0; attempt < 3; ++attempt) {
    if (fn()) {
      return true;
    }
    delay(500);
  }
  return false;
}

String urlFor(const char* suffix) {
  String url = SERVER_BASE_URL;
  if (!url.endsWith("/") && suffix[0] != '/') {
    url += "/";
  }
  url += suffix;
  return url;
}

uint8_t* allocateBody(size_t len) {
  if (psramFound()) {
    return static_cast<uint8_t*>(ps_malloc(len));
  }
  return static_cast<uint8_t*>(std::malloc(len));
}

void addAuthHeader(HTTPClient& http) {
  if (std::strlen(FJKER_API_TOKEN) > 0) {
    http.addHeader("X-FJ-KER-TOKEN", FJKER_API_TOKEN);
  }
}
}  // 命名空间
#endif

bool ensureWifi() {
#ifdef ARDUINO
  if (WiFi.status() == WL_CONNECTED) {
    return true;
  }
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  const unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 12000) {
    delay(100);
  }
  return WiFi.status() == WL_CONNECTED;
#else
  return false;
#endif
}

bool postJobJpeg(const uint8_t* jpeg, size_t len, std::string& jobId) {
#ifdef ARDUINO
  return retry([&]() {
    constexpr const char* boundary = "----fjker-boundary";
    const String head =
        String("--") + boundary +
        "\r\nContent-Disposition: form-data; name=\"image\"; filename=\"question.jpg\"\r\n"
        "Content-Type: image/jpeg\r\n\r\n";
    const String tail = String("\r\n--") + boundary + "--\r\n";
    const size_t total = head.length() + len + tail.length();
    uint8_t* body = allocateBody(total);
    if (!body) {
      return false;
    }
    std::memcpy(body, head.c_str(), head.length());
    std::memcpy(body + head.length(), jpeg, len);
    std::memcpy(body + head.length() + len, tail.c_str(), tail.length());

    HTTPClient http;
    http.begin(urlFor("/jobs"));
    http.addHeader("Content-Type", String("multipart/form-data; boundary=") + boundary);
    addAuthHeader(http);
    const int code = http.POST(body, total);
    std::free(body);
    if (code < 200 || code >= 300) {
      http.end();
      return false;
    }
    JsonDocument doc;
    const DeserializationError err = deserializeJson(doc, http.getString());
    http.end();
    if (err || !doc["job_id"].is<const char*>()) {
      return false;
    }
    jobId = doc["job_id"].as<const char*>();
    return true;
  });
#else
  (void)jpeg;
  (void)len;
  (void)jobId;
  return false;
#endif
}

bool getJobStatus(const char* jobId, JobStatus& status) {
#ifdef ARDUINO
  return retry([&]() {
    HTTPClient http;
    String path = "/jobs/";
    path += jobId;
    http.begin(urlFor(path.c_str()));
    addAuthHeader(http);
    const int code = http.GET();
    if (code != 200) {
      http.end();
      return false;
    }
    JsonDocument doc;
    const DeserializationError err = deserializeJson(doc, http.getString());
    http.end();
    if (err) {
      return false;
    }
    const String state = doc["status"] | "";
    status.ready = state == "ready";
    status.error = state == "error";
    status.pageCount = doc["pages"] | 0;
    status.errorMessage = (doc["error"] | "");
    return true;
  });
#else
  (void)jobId;
  (void)status;
  return false;
#endif
}

bool getPage(const char* jobId, int pageIndex, uint8_t* dst, size_t len) {
#ifdef ARDUINO
  return retry([&]() {
    HTTPClient http;
    String path = "/jobs/";
    path += jobId;
    path += "/pages/";
    path += pageIndex;
    http.begin(urlFor(path.c_str()));
    addAuthHeader(http);
    const int code = http.GET();
    if (code != 200 || http.getSize() != static_cast<int>(display::PAGE_BYTES) ||
        len != display::PAGE_BYTES) {
      http.end();
      return false;
    }
    WiFiClient* stream = http.getStreamPtr();
    size_t read = 0;
    while (read < len && http.connected()) {
      const int n = stream->readBytes(dst + read, len - read);
      if (n <= 0) {
        break;
      }
      read += static_cast<size_t>(n);
    }
    http.end();
    return read == len;
  });
#else
  (void)jobId;
  (void)pageIndex;
  (void)dst;
  (void)len;
  return false;
#endif
}

bool getPageCount(const char* jobId, int& pageCount) {
  JobStatus status;
  if (!getJobStatus(jobId, status)) {
    return false;
  }
  pageCount = status.pageCount;
  return true;
}

bool cancelJob(const char* jobId) {
#ifdef ARDUINO
  return retry([&]() {
    HTTPClient http;
    String path = "/jobs/";
    path += jobId;
    http.begin(urlFor(path.c_str()));
    addAuthHeader(http);
    const int code = http.sendRequest("DELETE");
    http.end();
    return code >= 200 && code < 300;
  });
#else
  (void)jobId;
  return false;
#endif
}
}  // 命名空间 net_client
