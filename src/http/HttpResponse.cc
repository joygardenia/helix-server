#include "http/HttpResponse.h"

#include <sstream>

namespace {

const char *defaultStatusMessage(HttpResponse::HttpStatusCode code) {
  switch (code) {
  case HttpResponse::k200Ok:
    return "200 OK";
  case HttpResponse::k400BadRequest:
    return "400 Bad Request";
  case HttpResponse::k404NotFound:
    return "404 Not Found";
  case HttpResponse::k411LengthRequired:
    return "411 Length Required";
  case HttpResponse::k413PayloadTooLarge:
    return "413 Payload Too Large";
  case HttpResponse::k500InternalServerError:
    return "500 Internal Server Error";
  case HttpResponse::k503ServiceUnavailable:
    return "503 Service Unavailable";
  default:
    return "200 OK";
  }
}

} // namespace

HttpResponse::HttpResponse(bool closeConnection)
    : statusCode_(k200Ok), statusMessage_("200 OK"),
      closeConnection_(closeConnection), contentType_("application/json") {}

void HttpResponse::setStatusCode(HttpStatusCode code) {
  statusCode_ = code;
  statusMessage_ = defaultStatusMessage(code);
}

void HttpResponse::setStatusMessage(const std::string &msg) {
  statusMessage_ = msg;
}

void HttpResponse::setCloseConnection(bool on) { closeConnection_ = on; }
bool HttpResponse::closeConnection() const { return closeConnection_; }

void HttpResponse::setContentType(const std::string &ct) { contentType_ = ct; }

void HttpResponse::addHeader(const std::string &field,
                             const std::string &value) {
  headers_[field] = value;
}

void HttpResponse::setBody(const std::string &body) { body_ = body; }
const std::string &HttpResponse::body() const { return body_; }

std::string HttpResponse::toString() const {
  std::ostringstream oss;
  oss << "HTTP/1.1 " << statusMessage_ << "\r\n";
  oss << "Content-Type: " << contentType_ << "\r\n";
  for (const auto &kv : headers_)
    oss << kv.first << ": " << kv.second << "\r\n";
  oss << "Content-Length: " << body_.size() << "\r\n";
  oss << "Connection: " << (closeConnection_ ? "close" : "keep-alive")
      << "\r\n\r\n";
  oss << body_;
  return oss.str();
}
