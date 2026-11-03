// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
#pragma once
// Nur fuer die Sensor-Module (nicht fuer die App)
#include "sensors.h"
void sensorenInternMelde(uint8_t quelle, float roh);          // vom internen Sensor-Task
void sensorenMqttWert(const char* topic, const char* nutzlast, int laenge);   // Zustand eines Topics
void sensorenMqttAvail(const char* topic, bool online);
void sensorenKandidatMelde(const SensorKandidat& k);
void sensorenInternAnlegen();                                 // interne Sensoren in der Liste anlegen
void sensorenInternStart();                                   // sensor_internal.cpp
void sensorenMqttStart();                                     // sensor_mqtt.cpp
void sensorenMqttNeuAbonnieren();                             // nach Aenderung der Liste
bool sensorenHoleMqttIntern(MqttEinstellungen* out);
void sensorenMqttNeuVerbinden();
int  sensorenTopicsHole(char (*out)[96], int max);            // alle Zustands- und Erreichbarkeits-Topics der Liste
extern bool g_internVorhanden;
