// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
#include "HTTPClient.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "esp_tls.h"

int WiFiClientSecure::lastError(char* buf, size_t n) const {
  snprintf(buf, n, "%s, errno %d", esp_err_to_name(err_), fehlernr_);
  return (int)err_;
}

static esp_err_t ereignis(esp_http_client_event_t* e) {
  if (e->event_id == HTTP_EVENT_ON_DATA && e->user_data && e->data_len > 0)
    ((HTTPClient*)e->user_data)->anhaengen((const char*)e->data, e->data_len);
  return ESP_OK;
}

bool HTTPClient::begin(const String& url) {
  end();
  body_ = String(); laenge_ = -1; streamen_ = false;
  esp_http_client_config_t c = {};
  c.url = url.c_str();
  c.timeout_ms = (int)timeout_;
  c.crt_bundle_attach = esp_crt_bundle_attach;     // Zertifikat gegen das CA-Paket prüfen
  c.event_handler = ereignis;
  c.user_data = this;
  c.buffer_size = 2048;
  c.buffer_size_tx = 1024;
  c.max_redirection_count = 5;
  c.user_agent = "WetterCubeFusion";
  h_ = esp_http_client_init(&c);
  return h_ != nullptr;
}

void HTTPClient::addHeader(const String& name, const String& wert) { if (h_) esp_http_client_set_header(h_, name.c_str(), wert.c_str()); }
void HTTPClient::setTimeout(uint32_t ms) { timeout_ = ms; if (h_) esp_http_client_set_timeout_ms(h_, (int)ms); }

int HTTPClient::GET() {
  if (!h_) return -1;
  esp_err_t e = esp_http_client_perform(h_);
  if (fehler_) { fehler_->err_ = e; fehler_->fehlernr_ = esp_http_client_get_errno(h_); }
  if (e != ESP_OK) return -1;
  laenge_ = (int)esp_http_client_get_content_length(h_);
  if (laenge_ <= 0) laenge_ = (int)body_.length();
  return esp_http_client_get_status_code(h_);
}

int HTTPClient::POST(const String& body) {
  if (!h_) return -1;
  esp_http_client_set_method(h_, HTTP_METHOD_POST);
  esp_http_client_set_post_field(h_, body.c_str(), (int)body.length());
  esp_err_t e = esp_http_client_perform(h_);
  if (fehler_) { fehler_->err_ = e; fehler_->fehlernr_ = esp_http_client_get_errno(h_); }
  if (e != ESP_OK) return -1;
  return esp_http_client_get_status_code(h_);
}

int HTTPClient::GETStream() {
  if (!h_) return -1;
  streamen_ = true;
  esp_err_t e = esp_http_client_open(h_, 0);
  if (fehler_) { fehler_->err_ = e; fehler_->fehlernr_ = esp_http_client_get_errno(h_); }
  if (e != ESP_OK) return -1;
  int64_t l = esp_http_client_fetch_headers(h_);
  laenge_ = esp_http_client_is_chunked_response(h_) ? -1 : (int)l;
  return esp_http_client_get_status_code(h_);
}

int HTTPClient::read(uint8_t* buf, size_t n) { return h_ ? esp_http_client_read(h_, (char*)buf, (int)n) : -1; }

void HTTPClient::end() {
  if (h_) { esp_http_client_close(h_); esp_http_client_cleanup(h_); h_ = nullptr; }
}
