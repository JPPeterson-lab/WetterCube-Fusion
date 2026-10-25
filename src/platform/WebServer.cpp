// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
#include "WebServer.h"
#include "esp_log.h"

static const char* TAG = "web";

static esp_err_t dispatch(httpd_req_t* req) {
  WebServer::Eintrag* e = (WebServer::Eintrag*)req->user_ctx;
  return e->s->bearbeite(req, e->fn);
}
static esp_err_t fehler404(httpd_req_t* req, httpd_err_code_t) {
  WebServer* s = (WebServer*)httpd_get_global_user_ctx(req->handle);
  if (s) s->notFoundAufrufen(req);
  return ESP_OK;
}

void WebServer::on(const char* uri, http_method m, THandlerFunction fn) {
  routen_.push_back({String(uri), m, new Eintrag{this, fn}});
}

void WebServer::begin() {
  if (h_) return;
  httpd_config_t c = HTTPD_DEFAULT_CONFIG();
  c.server_port = port_;
  c.max_uri_handlers = (uint16_t)(routen_.size() + 4);
  c.max_open_sockets = 7;
  c.stack_size = 8192;              // klein halten: der Stapel liegt im knappen internen RAM (große Texte liegen auf dem Heap)
  c.core_id = 0;                    // wie WLAN und Netzwerk-Task; LVGL läuft auf Core 1
  c.lru_purge_enable = true;
  c.recv_wait_timeout = 10;
  c.send_wait_timeout = 10;
  c.global_user_ctx = this;
  if (httpd_start(&h_, &c) != ESP_OK) { ESP_LOGE(TAG, "HTTP-Server startet nicht"); h_ = nullptr; return; }
  for (auto& r : routen_) {
    httpd_uri_t u = {};
    u.uri = r.uri.c_str(); u.method = r.m; u.handler = dispatch; u.user_ctx = r.e;
    httpd_register_uri_handler(h_, &u);
  }
  if (notFound_) httpd_register_err_handler(h_, HTTPD_404_NOT_FOUND, fehler404);
}

void WebServer::stop() { if (h_) { httpd_stop(h_); h_ = nullptr; } }

// ── Anfrage ──────────────────────────────────────────────────────────────────
static String urlDecode(const char* s, size_t n) {
  String r;
  for (size_t i = 0; i < n; i++) {
    if (s[i] == '+') r += ' ';
    else if (s[i] == '%' && i + 2 < n + 0 && isxdigit((unsigned char)s[i + 1]) && isxdigit((unsigned char)s[i + 2])) {
      char h[3] = {s[i + 1], s[i + 2], 0}; r += (char)strtol(h, nullptr, 16); i += 2;
    } else r += s[i];
  }
  return r;
}

static void paareLesen(const char* buf, size_t n, std::vector<std::pair<String, String>>& out) {
  size_t i = 0;
  while (i < n) {
    size_t e = i; while (e < n && buf[e] != '&') e++;
    size_t g = i; while (g < e && buf[g] != '=') g++;
    out.emplace_back(urlDecode(buf + i, g - i), g < e ? urlDecode(buf + g + 1, e - g - 1) : String());
    i = e + 1;
  }
}

void WebServer::argsLaden() {
  if (argsGeladen_ || !req_) return;
  argsGeladen_ = true;
  size_t ql = httpd_req_get_url_query_len(req_);
  if (ql > 0 && ql < 2048) {
    char* q = (char*)malloc(ql + 1);
    if (q && httpd_req_get_url_query_str(req_, q, ql + 1) == ESP_OK) paareLesen(q, ql, args_);
    free(q);
  }
  if (req_->method == HTTP_POST && req_->content_len > 0 && req_->content_len <= 256 * 1024) {
    String body;
    char* buf = (char*)malloc(2048);
    if (!buf) return;
    size_t rest = req_->content_len;
    while (rest > 0) {
      int n = httpd_req_recv(req_, buf, rest < 2048 ? rest : 2048);
      if (n == HTTPD_SOCK_ERR_TIMEOUT) continue;
      if (n <= 0) break;
      body.concat(buf, (size_t)n); rest -= (size_t)n;
    }
    free(buf);
    char typ[64] = "";
    httpd_req_get_hdr_value_str(req_, "Content-Type", typ, sizeof(typ));
    if (strncmp(typ, "application/x-www-form-urlencoded", 33) == 0) paareLesen(body.c_str(), body.length(), args_);
    else args_.emplace_back(String("plain"), body);
  }
}

String WebServer::arg(const String& name) {
  argsLaden();
  for (auto& p : args_) if (p.first == name) return p.second;
  return String();
}
bool WebServer::hasArg(const String& name) {
  argsLaden();
  for (auto& p : args_) if (p.first == name) return true;
  return false;
}
String WebServer::uri() {
  if (!req_) return String();
  String u(req_->uri); int q = u.indexOf('?');
  return q >= 0 ? u.substring(0, q) : u;
}
String WebServer::hostHeader() {
  char h[96] = "";
  if (req_) httpd_req_get_hdr_value_str(req_, "Host", h, sizeof(h));
  return String(h);
}
String WebServer::header(const String& name) {
  if (!req_) return String();
  size_t n = httpd_req_get_hdr_value_len(req_, name.c_str());
  if (n == 0 || n > 200) return String();
  char buf[208];
  if (httpd_req_get_hdr_value_str(req_, name.c_str(), buf, sizeof(buf)) != ESP_OK) return String();
  return String(buf);
}
size_t WebServer::clientContentLength() { return req_ ? req_->content_len : 0; }
int WebServer::bodyLesen(void* buf, size_t n) {
  if (!req_) return -1;
  int r;
  do { r = httpd_req_recv(req_, (char*)buf, n); } while (r == HTTPD_SOCK_ERR_TIMEOUT);
  return r;
}

// ── Antwort ──────────────────────────────────────────────────────────────────
static const char* statusText(int code) {
  switch (code) {
    case 200: return "200 OK";
    case 204: return "204 No Content";
    case 302: return "302 Found";
    case 304: return "304 Not Modified";
    case 400: return "400 Bad Request";
    case 401: return "401 Unauthorized";
    case 403: return "403 Forbidden";
    case 429: return "429 Too Many Requests";
    case 404: return "404 Not Found";
    case 409: return "409 Conflict";
    case 500: return "500 Internal Server Error";
    default:  return "200 OK";
  }
}

void WebServer::antwortKopf(int code, const char* type) {
  httpd_resp_set_status(req_, statusText(code));
  typ_ = type ? type : "text/plain";
  httpd_resp_set_type(req_, typ_.c_str());
  for (auto& k : kopf_) httpd_resp_set_hdr(req_, k.first.c_str(), k.second.c_str());
}

void WebServer::send(int code, const char* type, const String& body) {
  if (!req_ || gesendet_) return;
  antwortKopf(code, type);
  if (laengeGesetzt_ && body.isEmpty()) { stueckweise_ = true; return; }   // Rumpf folgt per sendContent()
  httpd_resp_send(req_, body.c_str(), body.length());
  gesendet_ = true;
}

void WebServer::sendContent(const char* d, size_t n) {
  if (!req_ || !stueckweise_ || gesendet_) return;
  if (n == 0) { httpd_resp_send_chunk(req_, nullptr, 0); gesendet_ = true; return; }
  httpd_resp_send_chunk(req_, d, (ssize_t)n);
}

esp_err_t WebServer::bearbeite(httpd_req_t* req, THandlerFunction& fn) {
  req_ = req; argsGeladen_ = false; gesendet_ = false; stueckweise_ = false; laengeGesetzt_ = false; laenge_ = 0;
  kopf_.clear(); args_.clear();
  if (pruefer_ && !pruefer_()) {                       // Zugriff nicht erlaubt: die Pruefung hat schon geantwortet
    if (!gesendet_) { httpd_resp_send(req_, "", 0); gesendet_ = true; }
    req_ = nullptr;
    return ESP_OK;
  }
  fn();
  if (!gesendet_) {                                   // Handler hat nichts oder eine Antwort in Stücken nicht beendet
    if (stueckweise_) httpd_resp_send_chunk(req_, nullptr, 0);
    else httpd_resp_send(req_, "", 0);
    gesendet_ = true;
  }
  req_ = nullptr;
  return ESP_OK;
}

void WebServer::notFoundAufrufen(httpd_req_t* req) {
  THandlerFunction f = notFound_;
  if (f) bearbeite(req, f);
}
