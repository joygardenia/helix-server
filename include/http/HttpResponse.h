#pragma once

#include <map>
#include <string>

class HttpResponse {
public:
  enum HttpStatusCode {
    kUnknown = 0,
    k200Ok = 200,
    k400BadRequest = 400,
    k404NotFound = 404,
    k411LengthRequired = 411,
    k413PayloadTooLarge = 413,
    k500InternalServerError = 500,
    k503ServiceUnavailable = 503
  };

  explicit HttpResponse(bool closeConnection);
  void setStatusCode(HttpStatusCode code);
  void setStatusMessage(const std::string& msg);
  void setCloseConnection(bool on);
  bool closeConnection() const;
  void setContentType(const std::string& ct);
  void addHeader(const std::string& field, const std::string& value);
  void setBody(const std::string& body);
  const std::string& body() const;
  std::string toString() const;

private:
  HttpStatusCode statusCode_;
  std::string statusMessage_;
  bool closeConnection_;
  std::string contentType_;
  std::map<std::string, std::string> headers_;
  std::string body_;
};
