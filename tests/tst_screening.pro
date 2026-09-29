# Unit tests for the screen-printing additions. Plain QtTest, no GUI shown, no files written.
#   build and run: tests\run_tests.ps1 (Windows) - needs libdither built first (cd libdither && make libdither)
QT += core gui testlib
CONFIG += c++20 console testcase
CONFIG -= app_bundle
TARGET = tst_screening

APP = ../src/app
INCLUDEPATH += $$APP ../libdither/src/libdither

!win32-msvc* {
    LIBS += -L$$PWD/../libdither/dist -ldither
}
win32-msvc {
    LIBS += -L$$PWD/../libdither/dist/Release -llibdither
}

SOURCES += \
    tst_screening.cpp \
    $$APP/screening/cellresample.cpp \
    $$APP/screening/matrixstretch.cpp \
    $$APP/imagehash/imagehash.cpp \
    $$APP/imagehash/imagehashmono.cpp \
    $$APP/imagehash/imagehashcolor.cpp

HEADERS += \
    $$APP/imagehash/imagehash.h \
    $$APP/imagehash/imagehashmono.h \
    $$APP/imagehash/imagehashcolor.h
