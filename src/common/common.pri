INCLUDEPATH += $$PWD
include($$PWD/security.pri)

HEADERS += \
    $$PWD/atomicstate.h \
    $$PWD/connmanutil.h \
    $$PWD/networkrecord.h \
    $$PWD/notificationtoken.h \
    $$PWD/reconciler.h

SOURCES += \
    $$PWD/atomicstate.cpp \
    $$PWD/connmanutil.cpp \
    $$PWD/networkrecord.cpp \
    $$PWD/notificationtoken.cpp \
    $$PWD/reconciler.cpp
