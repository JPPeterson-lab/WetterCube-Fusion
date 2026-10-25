// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
#pragma once
#include <stdint.h>
// Fängt im Einrichtungsmodus alle DNS-Anfragen ab und beantwortet sie mit der Adresse des Geräts (Captive Portal).
void dnsPortalStart(uint32_t ipLwip);   // IP in lwIP-Byteordnung (wie IPAddress::raw())
void dnsPortalStop();
