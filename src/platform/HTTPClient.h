// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
#pragma once
// HTTPS-Abrufe auf Basis von esp_http_client (eigener Code). Das Serverzertifikat wird gegen das eingebaute CA-Paket geprüft.
#include "Arduino.h"
#include "esp_http_client.h"

// Hält den letzten Fehler eines Abrufs (für das /log); "setInsecure" gibt es nicht mehr, es wird immer geprüft.
class WiFiClientSecure {
 public:
  int lastError(char* buf, size_t n) const;
  esp_err_t err_ = ESP_OK;
  int fehlernr_ = 0;
};

class HTTPClient {
 public:
  ~HTTPClient() { end(); }
  bool begin(WiFiClientSecure& c, const String& url) { fehler_ = &c; return begin(url); }
  bool begin(const String& url);
  void addHeader(const String& name, const String& wert);
  void setTimeout(uint32_t ms);
  int GET();                       // holt die ganze Antwort (in den PSRAM bei großen Antworten); Rückgabe: HTTP-Code oder -1 bei Fehler
  String getString() { String r(std::move(body_)); body_ = String(); return r; }
  int getSize() { return laenge_; }
  int POST(const String& body);    // schickt body als Anfragekoerper (Content-Type per addHeader setzen); Rueckgabe: HTTP-Code oder -1
  int GETStream();                 // nur die Kopfzeilen holen; danach mit read() lesen (große Dateien)
  int read(uint8_t* buf, size_t n);   // Rückgabe: gelesene Bytes, 0 = Ende, <0 Fehler
  void end();
  void anhaengen(const char* d, int n) { if (!streamen_ && body_.length() < 8u * 1024 * 1024) body_.concat(d, n); }   // beim Streamen liefert read() die Daten, nichts zusaetzlich sammeln
 private:
  esp_http_client_handle_t h_ = nullptr;
  WiFiClientSecure* fehler_ = nullptr;
  String body_;
  int laenge_ = -1;
  bool streamen_ = false;
  uint32_t timeout_ = 15000;
};
