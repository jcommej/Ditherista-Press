# Unit tests for the screen-printing additions. Plain QtTest, no GUI shown, no files written outside build/.
#   build and run: tests\run_tests.ps1 (Windows) - needs libdither built first (cd libdither && make libdither)
QT += core gui widgets testlib
CONFIG += c++20 console
CONFIG -= app_bundle
TARGET = tests

APP = ../src/app
INCLUDEPATH += $$APP ../libdither/src/libdither

!win32-msvc* {
    LIBS += -L$$PWD/../libdither/dist -ldither
}
win32-msvc {
    LIBS += -L$$PWD/../libdither/dist/Release -llibdither
}

SOURCES += \
    main.cpp \
    tst_screening.cpp \
    tst_adjust.cpp \
    tst_export.cpp \
    tst_preview.cpp \
    tst_separation.cpp \
    tst_psd.cpp \
    tst_presets.cpp \
    tst_colour.cpp \
    tst_palette.cpp \
    tst_preferences.cpp \
    tst_render.cpp \
    $$APP/preferences/preferences.cpp \
    $$APP/ui_elements/mouseeventfilter.cpp \
    $$APP/color/colorspace.cpp \
    $$APP/palette/palettemodel.cpp \
    $$APP/palette/labpanel.cpp \
    $$APP/palette/colourpickerdialog.cpp \
    $$APP/presets/presetstore.cpp \
    $$APP/export/psdwriter.cpp \
    $$APP/screening/separation.cpp \
    $$APP/viewport/graphicsview.cpp \
    $$APP/viewport/renderglyphbutton.cpp \
    $$APP/viewport/graphicspixmapitem.cpp \
    $$APP/export/filmwriter.cpp \
    $$APP/screening/cellresample.cpp \
    $$APP/screening/matrixstretch.cpp \
    $$APP/adjust/filters.cpp \
    $$APP/adjust/tonecurve.cpp \
    $$APP/imagehash/imagehash.cpp \
    $$APP/imagehash/imagehashmono.cpp \
    $$APP/imagehash/imagehashcolor.cpp

HEADERS += \
    $$APP/viewport/graphicsview.h \
    $$APP/viewport/renderglyphbutton.h \
    $$APP/viewport/graphicspixmapitem.h \
    $$APP/imagehash/imagehash.h \
    $$APP/imagehash/imagehashmono.h \
    $$APP/imagehash/imagehashcolor.h \
    $$APP/palette/labpanel.h \
    $$APP/palette/colourpickerdialog.h \
    $$APP/ui_elements/mouseeventfilter.h
