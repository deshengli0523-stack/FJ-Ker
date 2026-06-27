#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace net_client {
struct JobStatus {
  bool ready = false;
  bool error = false;
  int pageCount = 0;
  std::string errorMessage;
};

bool ensureWifi();
bool postJobRawRgb565(const uint8_t* data, size_t len, int width, int height,
                      std::string& jobId);
bool getJobStatus(const char* jobId, JobStatus& status);
bool getPage(const char* jobId, int pageIndex, uint8_t* dst, size_t len);
bool getPageCount(const char* jobId, int& pageCount);
bool cancelJob(const char* jobId);
}  // 命名空间 net_client
