#include "net_client.h"

#include <cstring>

#include "diagnostics.h"

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
    diagnostics::event("wifi", "already connected");
    return true;
  }
  diagnostics::event("wifi", "connect start");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  const unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 6000) {
    delay(100);
  }
  const bool ok = WiFi.status() == WL_CONNECTED;
  diagnostics::value("wifi", "status", static_cast<long long>(WiFi.status()));
  diagnostics::result("wifi", "connect", ok);
  if (ok) {
    const String ip = WiFi.localIP().toString();
    diagnostics::text("wifi", "ip", ip.c_str());
    diagnostics::value("wifi", "rssi", WiFi.RSSI());
  }
  return ok;
#else
  return false;
#endif
}

bool postJobRawRgb565(const uint8_t* data, size_t len, int width, int height,
                      std::string& jobId) {
#ifdef ARDUINO
  return retry([&]() {
    diagnostics::size("upload", "raw_bytes", len);
    diagnostics::value("upload", "raw_width", width);
    diagnostics::value("upload", "raw_height", height);
    if (!data || width <= 0 || height <= 0 || len != static_cast<size_t>(width) * height * 2 ||
        len > FJKER_MAX_RAW_IMAGE_BYTES) {
      diagnostics::event("upload", "body too large");
      return false;
    }

    HTTPClient http;
    const String url = urlFor("/jobs");
    if (!http.begin(url)) {
      diagnostics::event("upload", "http begin failed");
      return false;
    }
    http.addHeader("Content-Type", "application/octet-stream");
    http.addHeader("X-FJ-KER-IMAGE-FORMAT", "rgb565");
    http.addHeader("X-FJ-KER-WIDTH", String(width));
    http.addHeader("X-FJ-KER-HEIGHT", String(height));
    http.addHeader("X-FJ-KER-BYTE-ORDER", "be");
    addAuthHeader(http);
    const unsigned long start = millis();
    const int code = http.POST(const_cast<uint8_t*>(data), len);
    diagnostics::value("upload", "post_ms", static_cast<long long>(millis() - start));
    diagnostics::value("upload", "http_code", code);
    if (code < 200 || code >= 300) {
      http.end();
      return false;
    }
    JsonDocument doc;
    const DeserializationError err = deserializeJson(doc, http.getString());
    http.end();
    if (err || !doc["job_id"].is<const char*>()) {
      diagnostics::text("upload", "json_error", err ? err.c_str() : "missing_job_id");
      return false;
    }
    jobId = doc["job_id"].as<const char*>();
    diagnostics::token("job", "id", jobId.c_str());
    return true;
  });
#else
  (void)data;
  (void)len;
  (void)width;
  (void)height;
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
    const String url = urlFor(path.c_str());
    if (!http.begin(url)) {
      diagnostics::event("poll", "http begin failed");
      return false;
    }
    addAuthHeader(http);
    const int code = http.GET();
    diagnostics::value("poll", "http_code", code);
    if (code != 200) {
      http.end();
      return false;
    }
    JsonDocument doc;
    const DeserializationError err = deserializeJson(doc, http.getString());
    http.end();
    if (err) {
      diagnostics::text("poll", "json_error", err.c_str());
      return false;
    }
    const String state = doc["status"] | "";
    status.ready = state == "ready";
    status.error = state == "error";
    status.pageCount = doc["pages"] | 0;
    status.errorMessage = (doc["error"] | "");
    diagnostics::text("poll", "status", state.c_str());
    diagnostics::value("poll", "pages", status.pageCount);
    if (status.error) {
      diagnostics::size("poll", "server_error_bytes", status.errorMessage.size());
    }
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
    const String url = urlFor(path.c_str());
    if (!http.begin(url)) {
      diagnostics::event("page", "http begin failed");
      return false;
    }
    addAuthHeader(http);
    const int code = http.GET();
    diagnostics::value("page", "index", pageIndex);
    diagnostics::value("page", "http_code", code);
    diagnostics::value("page", "content_length", http.getSize());
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
    diagnostics::size("page", "read_bytes", read);
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
    const String url = urlFor(path.c_str());
    if (!http.begin(url)) {
      diagnostics::event("cancel", "http begin failed");
      return false;
    }
    addAuthHeader(http);
    const int code = http.sendRequest("DELETE");
    http.end();
    diagnostics::value("cancel", "http_code", code);
    return code >= 200 && code < 300;
  });
#else
  (void)jobId;
  return false;
#endif
}
}  // 命名空间 net_client
