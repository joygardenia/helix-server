#include "http/HttpContext.h"

#include "Buffer.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <string>

namespace {

std::string trim(const std::string& value) {
  std::size_t begin = 0;
  std::size_t end = value.size();
  while (begin < end && std::isspace(static_cast<unsigned char>(value[begin])))
    ++begin;
  while (end > begin &&
         std::isspace(static_cast<unsigned char>(value[end - 1])))
    --end;
  return value.substr(begin, end - begin);
}

std::string lowerCase(const std::string& value) {
  std::string result(value);
  std::transform(result.begin(), result.end(), result.begin(),
                 [](char ch) {
                   return static_cast<char>(
                       std::tolower(static_cast<unsigned char>(ch)));
                 });
  return result;
}

bool parseSize(const std::string& value, std::size_t* result) {
  if (value.empty())
    return false;
  std::size_t number = 0;
  for (std::size_t i = 0; i < value.size(); ++i) {
    if (value[i] < '0' || value[i] > '9')
      return false;
    const std::size_t digit = static_cast<std::size_t>(value[i] - '0');
    if (number > (std::numeric_limits<std::size_t>::max() - digit) / 10)
      return false;
    number = number * 10 + digit;
  }
  *result = number;
  return true;
}

const char* findCrlf(const Buffer* buf) {
  const char crlf[] = "\r\n";
  return std::search(buf->peek(), buf->beginWrite(), crlf, crlf + 2);
}

HttpRequest::Method parseMethod(const std::string& method) {
  if (method == "GET")
    return HttpRequest::kGet;
  if (method == "POST")
    return HttpRequest::kPost;
  if (method == "HEAD")
    return HttpRequest::kHead;
  if (method == "PUT")
    return HttpRequest::kPut;
  if (method == "DELETE")
    return HttpRequest::kDelete;
  return HttpRequest::kInvalid;
}

bool validVersion(const std::string& version) {
  if (version.size() < 8 || version.compare(0, 7, "HTTP/1.") != 0)
    return false;
  for (std::size_t i = 7; i < version.size(); ++i) {
    if (version[i] < '0' || version[i] > '9')
      return false;
  }
  return true;
}

bool exceedsLimit(std::size_t used, std::size_t additional,
                  std::size_t limit) {
  return used > limit || additional > limit - used;
}

}

HttpContext::HttpContext(std::size_t maxRequestSize)
    : state_(kExpectRequestLine), maxRequestSize_(maxRequestSize),
      headerBytes_(0), contentLength_(0), hasContentLength_(false),
      errorCode_(0) {}

bool HttpContext::gotAll() const { return state_ == kGotAll; }

const HttpRequest& HttpContext::request() const { return request_; }

HttpRequest& HttpContext::request() { return request_; }

void HttpContext::reset() {
  state_ = kExpectRequestLine;
  request_.reset();
  headerBytes_ = 0;
  contentLength_ = 0;
  hasContentLength_ = false;
  errorCode_ = 0;
}

int HttpContext::errorCode() const { return errorCode_; }

HttpContext::ParseResult HttpContext::parseRequest(Buffer* buf) {
  if (errorCode_ != 0)
    return errorCode_ == 413 ? kTooLarge : kBadRequest;

  while (state_ != kGotAll) {
    const char* line = findCrlf(buf);
    if (state_ == kExpectBody) {
      if (buf->readableBytes() < contentLength_)
        return kIncomplete;
      request_.setBody(std::string(buf->peek(), contentLength_));
      buf->retrieve(contentLength_);
      state_ = kGotAll;
      break;
    }

    if (line == buf->beginWrite()) {
      if (exceedsLimit(headerBytes_, buf->readableBytes(), maxRequestSize_)) {
        errorCode_ = 413;
        return kTooLarge;
      }
      return kIncomplete;
    }

    const std::size_t lineLength =
        static_cast<std::size_t>(line - buf->peek());
    if (exceedsLimit(headerBytes_, lineLength + 2, maxRequestSize_)) {
      errorCode_ = 413;
      return kTooLarge;
    }

    const std::string lineText(buf->peek(), lineLength);
    if (state_ == kExpectRequestLine) {
      const std::size_t firstSpace = lineText.find(' ');
      const std::size_t secondSpace =
          firstSpace == std::string::npos
              ? std::string::npos
              : lineText.find(' ', firstSpace + 1);
      if (firstSpace == std::string::npos || secondSpace == std::string::npos ||
          lineText.find(' ', secondSpace + 1) != std::string::npos ||
          firstSpace == 0 || secondSpace == firstSpace + 1 ||
          secondSpace + 1 == lineText.size()) {
        errorCode_ = 400;
        return kBadRequest;
      }
      const std::string method = lineText.substr(0, firstSpace);
      const std::string path =
          lineText.substr(firstSpace + 1, secondSpace - firstSpace - 1);
      const std::string version = lineText.substr(secondSpace + 1);
      const HttpRequest::Method parsedMethod = parseMethod(method);
      if (parsedMethod == HttpRequest::kInvalid || !validVersion(version)) {
        errorCode_ = 400;
        return kBadRequest;
      }
      request_.setMethod(parsedMethod);
      request_.setPath(path);
      request_.setVersion(version);
      headerBytes_ += lineLength + 2;
      buf->retrieve(lineLength + 2);
      state_ = kExpectHeaders;
      continue;
    }

    if (lineLength == 0) {
      headerBytes_ += 2;
      buf->retrieve(2);
      if (!hasContentLength_) {
        if (request_.method() != HttpRequest::kGet &&
            request_.method() != HttpRequest::kHead) {
          errorCode_ = 411;
          return kBadRequest;
        }
        state_ = kGotAll;
        break;
      }
      if (exceedsLimit(headerBytes_, contentLength_, maxRequestSize_)) {
        errorCode_ = 413;
        return kTooLarge;
      }
      if (contentLength_ == 0) {
        state_ = kGotAll;
      } else {
        state_ = kExpectBody;
      }
      continue;
    }

    const std::size_t colon = lineText.find(':');
    if (colon == std::string::npos || colon == 0) {
      errorCode_ = 400;
      return kBadRequest;
    }
    const std::string field = trim(lineText.substr(0, colon));
    const std::string value = trim(lineText.substr(colon + 1));
    if (field.empty()) {
      errorCode_ = 400;
      return kBadRequest;
    }
    const std::string lowerField = lowerCase(field);
    if (lowerField == "content-length") {
      if (!parseSize(value, &contentLength_)) {
        errorCode_ = 411;
        return kBadRequest;
      }
      hasContentLength_ = true;
    } else if (lowerField == "connection") {
      request_.setCloseConnection(lowerCase(value) == "close");
    }
    request_.setHeader(field, value);
    headerBytes_ += lineLength + 2;
    buf->retrieve(lineLength + 2);
  }
  return kComplete;
}
