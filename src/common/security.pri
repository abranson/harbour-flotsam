INCLUDEPATH += $$PWD
# Qt supplies PIC code; link executables as PIE with immediate binding/full
# RELRO, and protect stack frames in every process crossing this boundary.
QMAKE_CXXFLAGS += -fstack-protector-strong
QMAKE_LFLAGS += -pie -Wl,-z,relro,-z,now
HEADERS += $$PWD/security.h $$PWD/privatebus.h
SOURCES += $$PWD/security.cpp $$PWD/privatebus.cpp
