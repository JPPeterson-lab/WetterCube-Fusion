// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
#pragma once
// Kleine Webserver-Hülle auf esp_http_server (eigener Code) mit den Aufrufen, die dieses Projekt nutzt: on(), arg(), hasArg(), send(),
// sendHeader(), sendContent(). Die Handler laufen im HTTP-Server-Task (nicht im Netzwerk-Task) und werden nacheinander ausgeführt.
// Antworten ohne bekannte Länge (setContentLength vor send mit leerem Inhalt) gehen in Stücken (chunked) hinaus.
#include "Arduino.h"
#include <functional>
#include <list>
#include <vector>
#include <utility>
#include "esp_http_server.h"

#define CONTENT_LENGTH_UNKNOWN ((size_t)-1)
typedef std::function<void()> THandlerFunction;

class WebServer {
 public:
  explicit WebServer(int port = 80) : port_(port) {}
  void on(const char* uri, http_method m, THandlerFunction fn);
  void onNotFound(THandlerFunction fn) { notFound_ = fn; }
  void begin();
  void stop();
  // Zugriffspruefung: wird vor jedem Handler aufgerufen; gibt sie false zurueck, hat sie selbst geantwortet (z. B. 401/403)
  void setZugriffsPruefer(std::function<bool()> f) { pruefer_ = f; }
  bool istGet() { return req_ && (req_->method == HTTP_GET || req_->method == HTTP_HEAD); }
  void handleClient() {}                 // nicht nötig: der HTTP-Server-Task bedient die Anfragen selbst

  // Anfrage
  String arg(const String& name);          // Parameter aus der URL, einem Formular; arg("plain") = Rumpf anderer Anfragen (z. B. JSON)
  bool hasArg(const String& name);
  String uri();
  String hostHeader();
  String header(const String& name);     // beliebiger Anfrage-Header (leer, wenn nicht vorhanden)
  size_t clientContentLength();          // Länge des Anfragerumpfs
  int bodyLesen(void* buf, size_t n);    // Rumpf direkt lesen (Datei-Upload); >0 gelesen, 0 Ende, <0 Fehler

  // Antwort
  void send(int code, const char* type, const String& body);
  void send(int code) { send(code, "text/plain", String()); }
  void send(int code, const char* type, const char* body) { send(code, type, String(body)); }
  void send_P(int code, const char* type, const char* body) { send(code, type, String(body)); }
  void sendHeader(const String& name, const String& wert) { kopf_.emplace_back(name, wert); }
  void setContentLength(size_t n) { laengeGesetzt_ = true; laenge_ = n; }
  void sendContent(const String& s) { sendContent(s.c_str(), s.length()); }
  void sendContent(const char* d, size_t n);   // n == 0 beendet eine Antwort in Stücken
  void sendContent_P(const char* d) { sendContent(d, strlen(d)); }

  // intern
  struct Eintrag { WebServer* s; THandlerFunction fn; };
  esp_err_t bearbeite(httpd_req_t* req, THandlerFunction& fn);
  void notFoundAufrufen(httpd_req_t* req);

 private:
  void argsLaden();
  void antwortKopf(int code, const char* type);
  int port_;
  httpd_handle_t h_ = nullptr;
  struct Route { String uri; http_method m; Eintrag* e; };
  std::vector<Route> routen_;
  THandlerFunction notFound_;
  std::function<bool()> pruefer_;
  // laufende Anfrage
  httpd_req_t* req_ = nullptr;
  bool argsGeladen_ = false, gesendet_ = false, stueckweise_ = false, laengeGesetzt_ = false;
  size_t laenge_ = 0;
  String typ_;
  std::list<std::pair<String, String>> kopf_;
  std::vector<std::pair<String, String>> args_;
};
