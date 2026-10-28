QT       += core gui widgets network

CONFIG   += c++11 dll
TARGET    = PTHKCore
TEMPLATE  = lib

DEFINES  += PTHKCORE_LIBRARY

INCLUDEPATH += $$PWD/.. \
               $$PWD/../Widgets \
               $$PWD/../BASS/bass24 \
               $$PWD/../BASS/bassmidi24 \
               $$PWD/../3rdParty/onnxruntime/include \
               $$PWD/../3rdParty/pocketfft

win32: LIBS += -L$$PWD/../3rdParty/onnxruntime/lib -lonnxruntime

win32 {
    LIBS += -L$$PWD/../BASS/bass24/x64 -lbass \
            -L$$PWD/../BASS/bassmidi24/x64 -lbassmidi
}

HEADERS += pthkcore_global.h \
           PTHKCore.h

SOURCES += PTHKCore.cpp \
           ../Config.cpp \
           ../Midi/AiVocal.cpp
