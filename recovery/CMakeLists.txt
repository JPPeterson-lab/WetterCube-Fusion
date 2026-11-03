# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Jan Althaus (JPPeterson-lab)
# WetterCube Fusion – Notfall-App (Wiederherstellung). Reines ESP-IDF ohne Arduino; läuft aus der Partition "factory" und
# stellt Firmware von der Speicherkarte oder aus dem anderen Slot wieder her, setzt Einstellungen zurück und startet normal.
# Bauen: cd recovery && idf.py build   (oder: bash build.sh recovery im Hauptverzeichnis)
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# lv_conf.h der Notfall-App (kleinere Kopie der Projekt-Konfiguration)
include_directories("${CMAKE_CURRENT_LIST_DIR}/conf")
idf_build_set_property(COMPILE_OPTIONS "-Wno-error" APPEND)
# Die Version im Firmware-Kopf (esp_app_desc_t) steht in main/version.h
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/main/version.h")   # neu konfigurieren, wenn sich die Version aendert
file(READ "${CMAKE_CURRENT_LIST_DIR}/main/version.h" _rec_ver)
string(REGEX MATCH "#define NOTFALL_VERSION \"([^\"]+)\"" _rec_m "${_rec_ver}")
set(PROJECT_VER "${CMAKE_MATCH_1}")
project(wettercube_fusion_notfall)
