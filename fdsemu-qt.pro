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
    writestatus.cpp \
    diskreaddialog.cpp \
    writefilesdialog.cpp \
    ../fdsemu-lib/Device.cpp \
    ../fdsemu-lib/DiskImage.cpp \
    ../fdsemu-lib/DiskSide.cpp \
    ../fdsemu-lib/Flash.cpp \
    ../fdsemu-lib/FlashUtil.cpp \
    ../fdsemu-lib/Sram.cpp \
    ../fdsemu-lib/System.cpp

HEADERS  += mainwindow.h \
    hidapi/hidapi.h \
    writestatus.h \
    diskreaddialog.h \
    writefilesdialog.h \
    ../fdsemu-lib/Device.h \
    ../fdsemu-lib/DiskImage.h \
    ../fdsemu-lib/DiskSide.h \
    ../fdsemu-lib/Flash.h \
    ../fdsemu-lib/FlashUtil.h \
    ../fdsemu-lib/Sram.h \
    ../fdsemu-lib/System.h

FORMS    += mainwindow.ui \
    writestatus.ui \
    diskreaddialog.ui \
    writefilesdialog.ui

win32 {
	LIBS += -lsetupapi
	SOURCES += hidapi/hid-windows.c
}
macx {
	LIBS += -framework IOKit -framework CoreFoundation -liconv
	SOURCES += hidapi/hid-mac.c
}

