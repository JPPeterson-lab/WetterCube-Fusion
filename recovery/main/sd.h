// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
#pragma once
// Speicherkarte der Notfall-App (S3-Board: SPI-Modus, MOSI 11, SCK 12, MISO 13, Chip-Select über EXIO4 der IO-Erweiterung)
bool sdEinhaengen();
bool sdBereit();
