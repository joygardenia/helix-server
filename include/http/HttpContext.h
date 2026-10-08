#pragma once

#include <cstddef>

#include "HttpRequest.h"

class Buffer;

class HttpContext {
public:
  enum ParseResult { kIncomplete, kComplete, kBadRequest, kTooLarge };

  explicit HttpContext(std::size_t maxRequestSize = 64 * 1024);
  ParseResult parseRequest(Buffer* buf);
  bool gotAll() const;
  const HttpRequest& request() const;
  HttpRequest& request();
  void reset();
  int errorCode() const;

private:
  enum State { kExpectRequestLine, kExpectHeaders, kExpectBody, kGotAll };

  State state_;
  HttpRequest request_;
  std::size_t maxRequestSize_;
  std::size_t headerBytes_;
  std::size_t contentLength_;
  bool hasContentLength_;
  int errorCode_;
};
