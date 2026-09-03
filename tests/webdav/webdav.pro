TEMPLATE = app
TARGET = tst_flotsam_webdav

QT -= gui
QT += core network testlib
CONFIG += c++11 testcase

INCLUDEPATH += ../../src/syncd
HEADERS += ../../src/syncd/webdavclient.h ../../src/syncd/webdavcredentials.h
SOURCES += tst_webdav.cpp ../../src/syncd/webdavclient.cpp

check.commands = ./tst_flotsam_webdav
QMAKE_EXTRA_TARGETS += check
