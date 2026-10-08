#include "http/HttpRequest.h"

#include <algorithm>
#include <cctype>

namespace {

std::string lower(const std::string &s) {
  std::string r(s);
  std::transform(r.begin(), r.end(), r.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return r;
}

}

HttpRequest::HttpRequest() : method_(kInvalid), closeConnection_(false) {}

void HttpRequest::setMethod(Method m) { method_ = m; }
HttpRequest::Method HttpRequest::method() const { return method_; }

const char *HttpRequest::methodString() const {
  switch (method_) {
  case kGet:
    return "GET";
  case kPost:
    return "POST";
  case kHead:
    return "HEAD";
  case kPut:
    return "PUT";
  case kDelete:
    return "DELETE";
  default:
    return "INVALID";
  }
}

void HttpRequest::setPath(const std::string &p) { path_ = p; }
const std::string &HttpRequest::path() const { return path_; }

void HttpRequest::setVersion(const std::string &v) { version_ = v; }
const std::string &HttpRequest::version() const { return version_; }

void HttpRequest::setHeader(const std::string &field, const std::string &value) {
  headers_[lower(field)] = value;
}

std::string HttpRequest::getHeader(const std::string &field) const {
  auto it = headers_.find(lower(field));
  return it == headers_.end() ? std::string() : it->second;
}

const std::map<std::string, std::string> &HttpRequest::headers() const {
  return headers_;
}

void HttpRequest::setBody(const std::string &b) { body_ = b; }
const std::string &HttpRequest::body() const { return body_; }

void HttpRequest::setCloseConnection(bool on) { closeConnection_ = on; }
bool HttpRequest::closeConnection() const { return closeConnection_; }

void HttpRequest::reset() {
  method_ = kInvalid;
  path_.clear();
  version_.clear();
  headers_.clear();
  body_.clear();
  closeConnection_ = false;
}
