TEMPLATE = app
TARGET = tst_flotsam_common

QT -= gui
QT += core dbus network testlib
CONFIG += c++11 testcase

include(../../src/common/common.pri)
HEADERS += ../../src/common/uibus.h
SOURCES += ../../src/common/uibus.cpp
INCLUDEPATH += ../../src/app ../../src/helper

HEADERS += \
    ../../src/app/appcontroller.h \
    ../../src/app/applicationactivation.h \
    ../../src/helper/connmanhelper.h \
    ../../src/helper/connmanbackend.h
SOURCES += \
    ../../src/app/appcontroller.cpp \
    tst_common.cpp \
    ../../src/app/applicationactivation.cpp \
    ../../src/helper/connmanhelper.cpp

check.commands = ./tst_flotsam_common
QMAKE_EXTRA_TARGETS += check
