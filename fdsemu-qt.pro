#-------------------------------------------------
#
# Project created by QtCreator 2015-11-25T10:12:10
#
#-------------------------------------------------

QT       += core gui

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = fdsemu-qt
TEMPLATE = app


SOURCES += main.cpp\
        mainwindow.cpp \
    device.cpp \
    spi.cpp \
    os.cpp \
    fds.cpp \
    flashwrite.cpp

HEADERS  += mainwindow.h \
    device.h \
    spi.h \
    os.h \
    hidapi/hidapi.h \
    fds.h \
    flashwrite.h

FORMS    += mainwindow.ui \
    flashwrite.ui

win32 {
	LIBS += -lsetupapi
	SOURCES += hidapi/hid-windows.c
}
macx {
	LIBS += -framework IOKit -framework CoreFoundation -liconv
	SOURCES += hidapi/hid-mac.c
}

