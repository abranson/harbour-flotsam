TEMPLATE = app
TARGET = harbour-flotsam-syncd

QT -= gui
QT += core dbus network
CONFIG += c++11 link_pkgconfig
PKGCONFIG += accounts-qt5 sailfishaccounts connman-qt5

include(../common/common.pri)
HEADERS += ../common/uibus.h
SOURCES += ../common/uibus.cpp

HEADERS += \
    helpercall.h \
    accountprovider.h \
    notificationmanager.h \
    syncdaemon.h \
    webdavclient.h \
    webdavcredentials.h

SOURCES += \
    helpercall.cpp \
    accountprovider.cpp \
    main.cpp \
    notificationmanager.cpp \
    syncdaemon.cpp \
    webdavclient.cpp

target.path = /usr/libexec
INSTALLS += target
