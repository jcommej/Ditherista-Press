# Unit tests for the screen-printing additions. Plain QtTest, no GUI shown, no files written outside build/.
#   build and run: tests\run_tests.ps1 (Windows) - needs libdither built first (cd libdither && make libdither)
QT += core gui testlib
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
    $$APP/export/filmwriter.cpp \
    $$APP/screening/cellresample.cpp \
    $$APP/screening/matrixstretch.cpp \
    $$APP/adjust/filters.cpp \
    $$APP/adjust/tonecurve.cpp \
    $$APP/imagehash/imagehash.cpp \
    $$APP/imagehash/imagehashmono.cpp \
    $$APP/imagehash/imagehashcolor.cpp

HEADERS += \
    $$APP/imagehash/imagehash.h \
    $$APP/imagehash/imagehashmono.h \
    $$APP/imagehash/imagehashcolor.h
