TEMPLATE = app
TARGET = harbour-flotsam-connman-helper

QT -= gui
QT += core dbus network
CONFIG += c++11 link_pkgconfig

INCLUDEPATH += ../common

HEADERS += \
    connmanbackend.h \
    connmanhelper.h \
    ../common/connmanutil.h \
    ../common/networkrecord.h

SOURCES += \
    connmanbackend.cpp \
    connmanhelper.cpp \
    main.cpp \
    ../common/connmanutil.cpp \
    ../common/networkrecord.cpp

target.path = /usr/libexec
INSTALLS += target
