# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Jan Althaus (JPPeterson-lab)
# WetterCube Fusion: ESP-IDF-Projekt (Waveshare ESP32-P4 7" als Ziel; bis das Board da ist laeuft derselbe Code auf dem
# ESP32-S3-Touch-LCD-7B). Reines ESP-IDF, ohne Arduino-ESP32; die kleine Ersatzschicht liegt in src/platform/.
# Bauen:  . ~/esp/esp-idf-v5.5.5/export.sh && idf.py set-target esp32s3 && idf.py build   (oder: bash build.sh)
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# LVGL sucht seine lv_conf.h im Include-Pfad: die Projektkopie liegt in conf/
include_directories("${CMAKE_CURRENT_LIST_DIR}/conf")
# lodepng (PNG-Dekoder der Warnkarte) soll den PSRAM nutzen: die Allokatoren stehen in src/app/data_warn.cpp
idf_build_set_property(COMPILE_OPTIONS "-DLODEPNG_NO_COMPILE_ALLOCATORS" APPEND)
# Warnungen nicht als Fehler behandeln (Fremdkomponenten)
idf_build_set_property(COMPILE_OPTIONS "-Wno-error" APPEND)
idf_build_set_property(COMPILE_OPTIONS "-Wno-error=stringop-truncation;-Wno-error=format-truncation;-Wno-error=restrict;-Wno-error=array-bounds;-Wno-error=maybe-uninitialized;-Wno-error=address-of-packed-member" APPEND)
# Testfirmware fuer die Rollback-Pruefung: TESTHANG=1 TESTVERSION=9.9.9-test bash build.sh
if(FUSION_TEST_HANG)
  idf_build_set_property(COMPILE_OPTIONS "-DFUSION_TEST_HANG=1" APPEND)
  idf_build_set_property(COMPILE_OPTIONS "-DFIRMWARE_VERSION=\"${FUSION_TEST_VERSION}\"" APPEND)
endif()
# Die Version im Firmware-Kopf (esp_app_desc_t, vom Rollback-Hinweis und der Notfall-App gelesen) kommt aus app_config.h
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/src/app/app_config.h")   # neu konfigurieren, wenn sich die Version aendert
file(READ "${CMAKE_CURRENT_LIST_DIR}/src/app/app_config.h" _fusion_cfg)
string(REGEX MATCH "#define FIRMWARE_VERSION \"([^\"]+)\"" _fusion_m "${_fusion_cfg}")
set(PROJECT_VER "${CMAKE_MATCH_1}")
if(FUSION_TEST_HANG)
  set(PROJECT_VER "${FUSION_TEST_VERSION}")
endif()
project(wettercube_fusion)
