#ifndef MINI_HTTPLIB_H
#define MINI_HTTPLIB_H

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netdb.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

namespace httplib {

namespace detail {

#ifdef _WIN32
using sock_t = SOCKET;
inline const sock_t BAD_SOCK = INVALID_SOCKET;
inline void close_sock(sock_t s) { closesocket(s); }
#else
using sock_t = int;
inline const sock_t BAD_SOCK = -1;
inline void close_sock(sock_t s) { close(s); }
#endif

#ifdef MSG_NOSIGNAL
constexpr int NO_SIGPIPE = MSG_NOSIGNAL;
#else
constexpr int NO_SIGPIPE = 0;
#endif

constexpr size_t MAX_HEADER_BYTES = 16 * 1024;
constexpr size_t MAX_BODY_BYTES = 1024 * 1024;
constexpr int MAX_CONNECTIONS = 256;
constexpr int IO_TIMEOUT_SECONDS = 10;

inline std::string lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return (char)std::tolower(c); });
  return s;
}

inline std::string trim(const std::string &s) {
  size_t a = s.find_first_not_of(" \t\r\n");
  if (a == std::string::npos) return "";
  return s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
}

inline std::string url_decode(const std::string &s, bool plus_is_space) {
  std::string out;
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '%' && i + 2 < s.size() && std::isxdigit((unsigned char)s[i + 1]) &&
        std::isxdigit((unsigned char)s[i + 2])) {
      out += (char)std::stoi(s.substr(i + 1, 2), nullptr, 16);
      i += 2;
    } else if (s[i] == '+' && plus_is_space) {
      out += ' ';
    } else {
      out += s[i];
    }
  }
  return out;
}

inline std::vector<std::string> split_path(const std::string &p) {
  std::vector<std::string> parts;
  std::stringstream ss(p);
  std::string seg;
  while (std::getline(ss, seg, '/'))
    if (!seg.empty()) parts.push_back(seg);
  return parts;
}

inline const char *reason(int status) {
  switch (status) {
    case 200: return "OK";
    case 201: return "Created";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 409: return "Conflict";
    case 411: return "Length Required";
    case 413: return "Payload Too Large";
    case 431: return "Request Header Fields Too Large";
    case 500: return "Internal Server Error";
    case 503: return "Service Unavailable";
    default: return "Unknown";
  }
}

inline std::string mime_type(const std::string &ext) {
  static const std::map<std::string, std::string> types = {
      {".html", "text/html; charset=utf-8"}, {".css", "text/css; charset=utf-8"},
      {".js", "text/javascript; charset=utf-8"}, {".json", "application/json"},
      {".txt", "text/plain; charset=utf-8"}, {".svg", "image/svg+xml"},
      {".png", "image/png"}, {".jpg", "image/jpeg"}, {".jpeg", "image/jpeg"},
      {".gif", "image/gif"}, {".ico", "image/x-icon"}, {".woff2", "font/woff2"}};
  auto it = types.find(lower(ext));
  return it == types.end() ? "application/octet-stream" : it->second;
}

inline bool send_all(sock_t s, const std::string &data) {
  size_t sent = 0;
  while (sent < data.size()) {
    int n = (int)::send(s, data.data() + sent,
                        (int)std::min<size_t>(data.size() - sent, 1 << 20), NO_SIGPIPE);
    if (n <= 0) return false;
    sent += (size_t)n;
  }
  return true;
}

}

struct Request {
  std::string method, path, body;
  std::map<std::string, std::string> headers;
  std::map<std::string, std::string> params;
  std::map<std::string, std::string> path_params;

  std::string get_header_value(const std::string &key) const {
    auto it = headers.find(detail::lower(key));
    return it == headers.end() ? "" : it->second;
  }
  std::string get_param_value(const std::string &key) const {
    auto it = params.find(key);
    return it == params.end() ? "" : it->second;
  }
};

struct Response {
  int status = 200;
  std::string body;
  std::map<std::string, std::string> headers;

  void set_header(const std::string &key, const std::string &value) { headers[key] = value; }
  void set_content(const std::string &content, const std::string &content_type) {
    body = content;
    headers["Content-Type"] = content_type;
  }
};

class Server {
 public:
  using Handler = std::function<void(const Request &, Response &)>;
  using ExceptionHandler = std::function<void(const Request &, Response &, std::exception_ptr)>;

  Server &Get(const std::string &pattern, Handler h) { return add("GET", pattern, h); }
  Server &Post(const std::string &pattern, Handler h) { return add("POST", pattern, h); }
  Server &Delete(const std::string &pattern, Handler h) { return add("DELETE", pattern, h); }

  Server &set_exception_handler(ExceptionHandler h) {
    exception_handler_ = h;
    return *this;
  }

  bool set_mount_point(const std::string &mount_point, const std::string &dir) {
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) return false;
    mount_ = mount_point;
    dir_ = dir;
    return true;
  }


  bool listen(const std::string &host, int port) {
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
#else
    signal(SIGPIPE, SIG_IGN);
#endif
    addrinfo hints{}, *ai = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &ai) != 0) return false;

    detail::sock_t srv = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (srv == detail::BAD_SOCK) {
      freeaddrinfo(ai);
      return false;
    }
#ifndef _WIN32
    int on = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, (const char *)&on, sizeof on);
#endif
    if (::bind(srv, ai->ai_addr, (int)ai->ai_addrlen) != 0 || ::listen(srv, 128) != 0) {
      freeaddrinfo(ai);
      detail::close_sock(srv);
      return false;
    }
    freeaddrinfo(ai);

    for (;;) {
      detail::sock_t c = ::accept(srv, nullptr, nullptr);
      if (c == detail::BAD_SOCK) continue;
      if (active_ >= detail::MAX_CONNECTIONS) {
        detail::close_sock(c);
        continue;
      }
      set_timeouts(c);
      active_++;
      std::thread([this, c] {
        handle(c);
        ::shutdown(c, 1);
        detail::close_sock(c);
        active_--;
      }).detach();
    }
  }

 private:
  struct Route {
    std::string method;
    std::vector<std::string> segments;
    Handler handler;
  };

  std::vector<Route> routes_;
  ExceptionHandler exception_handler_;
  std::string mount_, dir_;
  std::atomic<int> active_{0};

  Server &add(const std::string &method, const std::string &pattern, Handler h) {
    routes_.push_back({method, detail::split_path(pattern), h});
    return *this;
  }

  static void set_timeouts(detail::sock_t c) {
#ifdef _WIN32
    DWORD tv = detail::IO_TIMEOUT_SECONDS * 1000;
#else
    timeval tv{detail::IO_TIMEOUT_SECONDS, 0};
#endif
    setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof tv);
    setsockopt(c, SOL_SOCKET, SO_SNDTIMEO, (const char *)&tv, sizeof tv);
  }

 static bool read_request(detail::sock_t s, Request &req, int &err) {
    std::string buf;
    char tmp[4096];
    size_t end;
    while ((end = buf.find("\r\n\r\n")) == std::string::npos) {
      if (buf.size() > detail::MAX_HEADER_BYTES) return err = 431, false;
      int n = (int)::recv(s, tmp, sizeof tmp, 0);
      if (n <= 0) return err = 0, false;
      buf.append(tmp, (size_t)n);
    }

    std::istringstream head(buf.substr(0, end));
    std::string line, target, version;
    std::getline(head, line);
    std::istringstream request_line(line);
    request_line >> req.method >> target >> version;
    if (req.method.empty() || target.empty() || target[0] != '/') return err = 400, false;

    size_t q = target.find('?');
    req.path = detail::url_decode(target.substr(0, q), false);
    if (q != std::string::npos) {
      std::stringstream query(target.substr(q + 1));
      std::string pair;
      while (std::getline(query, pair, '&')) {
        size_t eq = pair.find('=');
        std::string key = detail::url_decode(pair.substr(0, eq), true);
        std::string val = eq == std::string::npos ? "" : detail::url_decode(pair.substr(eq + 1), true);
        if (!key.empty()) req.params[key] = val;
      }
    }

    while (std::getline(head, line)) {
      size_t colon = line.find(':');
      if (colon != std::string::npos)
        req.headers[detail::lower(detail::trim(line.substr(0, colon)))] = detail::trim(line.substr(colon + 1));
    }

    if (req.headers.count("transfer-encoding")) return err = 411, false;
    size_t length = 0;
    auto it = req.headers.find("content-length");
    if (it != req.headers.end()) {
      length = (size_t)std::strtoull(it->second.c_str(), nullptr, 10);
      if (length > detail::MAX_BODY_BYTES) return err = 413, false;
    }

    req.body = buf.substr(end + 4);
    while (req.body.size() < length) {
      int n = (int)::recv(s, tmp, sizeof tmp, 0);
      if (n <= 0) return err = 0, false;
      req.body.append(tmp, (size_t)n);
    }
    req.body.resize(length);
    return true;
  }

  bool match(const Route &r, const std::vector<std::string> &parts, Request &req) const {
    if (r.segments.size() != parts.size()) return false;
    std::map<std::string, std::string> found;
    for (size_t i = 0; i < parts.size(); i++) {
      const std::string &want = r.segments[i];
      if (!want.empty() && want[0] == ':') found[want.substr(1)] = parts[i];
      else if (want != parts[i]) return false;
    }
    req.path_params = found;
    return true;
  }

  bool serve_file(const std::string &path, Response &res) const {
    if (mount_.empty() || path.compare(0, mount_.size(), mount_) != 0) return false;
    if (path.find("..") != std::string::npos || path.find('\\') != std::string::npos ||
        path.find(':') != std::string::npos || path.find('\0') != std::string::npos)
      return false;

    std::string rel = path.substr(mount_.size());
    while (!rel.empty() && rel[0] == '/') rel.erase(0, 1);
    if (rel.empty() || rel.back() == '/') rel += "index.html";

    std::filesystem::path file = std::filesystem::path(dir_) / rel;
    std::error_code ec;
    if (!std::filesystem::is_regular_file(file, ec)) return false;
    std::ifstream in(file, std::ios::binary);
    if (!in) return false;
    std::ostringstream content;
    content << in.rdbuf();
    res.set_content(content.str(), detail::mime_type(file.extension().string()));
    return true;
  }

  void handle(detail::sock_t s) const {
    Request req;
    Response res;
    int err = 0;
    if (!read_request(s, req, err)) {
      if (err == 0) return;
      res.status = err;
      res.set_content(detail::reason(err), "text/plain");
    } else {
      dispatch(req, res);
    }

    std::ostringstream out;
    out << "HTTP/1.1 " << res.status << ' ' << detail::reason(res.status) << "\r\n";
    for (const auto &h : res.headers) out << h.first << ": " << h.second << "\r\n";
    out << "Content-Length: " << res.body.size() << "\r\nConnection: close\r\n\r\n" << res.body;
    detail::send_all(s, out.str());
  }

  void dispatch(Request &req, Response &res) const {
    std::vector<std::string> parts = detail::split_path(req.path);
    bool path_exists = false;

    for (const Route &r : routes_) {
      if (!match(r, parts, req)) continue;
      path_exists = true;
      if (r.method != req.method) continue;
      try {
        r.handler(req, res);
      } catch (...) {
        res.status = 500;
        res.body.clear();
        try {
          if (exception_handler_) exception_handler_(req, res, std::current_exception());
          else res.set_content("Internal Server Error", "text/plain");
        } catch (...) {
        }
      }
      return;
    }

    if (path_exists) {
      res.status = 405;
      res.set_content("Method Not Allowed", "text/plain");
    } else if (!(req.method == "GET" && serve_file(req.path, res))) {
      res.status = 404;
      res.set_content("Not Found", "text/plain");
    }
  }
};

}
#endif
