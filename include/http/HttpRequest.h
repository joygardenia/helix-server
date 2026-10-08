#pragma once

#include <map>
#include <string>

class HttpRequest {
public:
  enum Method { kInvalid, kGet, kPost, kHead, kPut, kDelete };

  HttpRequest();
  void setMethod(Method m);
  Method method() const;
  const char* methodString() const;
  void setPath(const std::string& p);
  const std::string& path() const;
  void setVersion(const std::string& v);
  const std::string& version() const;
  void setHeader(const std::string& field, const std::string& value);
  std::string getHeader(const std::string& field) const;
  const std::map<std::string, std::string>& headers() const;
  void setBody(const std::string& b);
  const std::string& body() const;
  void setCloseConnection(bool on);
  bool closeConnection() const;
  void reset();

private:
  Method method_;
  std::string path_;
  std::string version_;
  std::map<std::string, std::string> headers_;
  std::string body_;
  bool closeConnection_;
};
