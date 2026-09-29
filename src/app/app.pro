TEMPLATE = app
TARGET = harbour-flotsam

QT += core dbus gui qml quick
CONFIG += c++11 link_pkgconfig
PKGCONFIG += sailfishapp

INCLUDEPATH += ../common
HEADERS += applicationactivation.h appcontroller.h ../common/networkrecord.h
SOURCES += applicationactivation.cpp appcontroller.cpp main.cpp ../common/networkrecord.cpp

qml.files = qml/main.qml
qml.path = /usr/share/harbour-flotsam/qml

qmlpages.files = \
    qml/pages/AboutPage.qml \
    qml/pages/CoverPage.qml \
    qml/pages/EditPage.qml \
    qml/pages/MainPage.qml \
    qml/pages/NetworkPage.qml \
    qml/pages/QrPage.qml \
    qml/pages/ScanPage.qml \
    qml/pages/SetupPage.qml
qmlpages.path = /usr/share/harbour-flotsam/qml/pages

qmlimages.files = qml/images/cover-background.svg
qmlimages.path = /usr/share/harbour-flotsam/qml/images

target.path = /usr/bin
INSTALLS += target qml qmlpages qmlimages
