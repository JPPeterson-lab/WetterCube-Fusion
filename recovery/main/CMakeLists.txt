# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Jan Althaus (JPPeterson-lab)
# Gemeinsam mit der Hauptfirmware genutzt: Board-Schicht (Display, Touch, SD), Touch-Treiber und der LVGL-Port (Quellen liegen unter ../../src)
get_filename_component(FUSION_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
if(IDF_TARGET STREQUAL "esp32p4")
    set(FUSION_BOARD "${FUSION_ROOT}/src/board/p4/board_p4.cpp")
else()
    set(FUSION_BOARD
        "${FUSION_ROOT}/src/board/s3/board_s3.cpp"
        "${FUSION_ROOT}/src/board/s3/i2c.cpp"
        "${FUSION_ROOT}/src/board/s3/io_extension.cpp"
        "${FUSION_ROOT}/src/board/s3/rgb_lcd_port.cpp")
endif()
set(FUSION_SHARED
    ${FUSION_BOARD}
    "${FUSION_ROOT}/src/hardware/gt911.cpp"
    "${FUSION_ROOT}/src/hardware/touch.cpp"
    "${FUSION_ROOT}/src/display/lvgl_port.cpp"
    "${FUSION_ROOT}/src/ui/fonts/font_montserrat_22.c"
    "${FUSION_ROOT}/src/ui/fonts/font_montserrat_32.c")
idf_component_register(
    SRCS "main.cpp" "sd.cpp" "firmware.cpp" "ui.cpp" ${FUSION_SHARED}
    INCLUDE_DIRS "." "${CMAKE_CURRENT_LIST_DIR}/../shim" "${FUSION_ROOT}" "${FUSION_ROOT}/src"
)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error)
target_compile_definitions(${COMPONENT_LIB} PRIVATE FUSION_PIXEL_CLOCK_HZ=30000000)
# Entwicklungsschalter: cmake -DNOTFALL_SNAPSHOT=1 gibt jeden Bildschirm halb skaliert als Text über die serielle Schnittstelle aus
# (tools/notfall_snap.py wandelt ihn in ein PNG um), damit sich das Layout ohne Foto prüfen lässt
if(NOTFALL_SNAPSHOT)
  target_compile_definitions(${COMPONENT_LIB} PRIVATE NOTFALL_SNAPSHOT=1)
endif()
if(NOTFALL_DEMO)   # zusätzlich: alle Bildschirme mit Beispieldaten nacheinander zeigen
  target_compile_definitions(${COMPONENT_LIB} PRIVATE NOTFALL_DEMO=1)
endif()
