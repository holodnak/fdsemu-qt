#include "flashwrite.h"
#include "ui_flashwrite.h"
#include <QApplication>
#include "fds.h"

void FlashWrite::write_callback(void *data,int bytes)
{
    FlashWrite *fw = (FlashWrite*)data;

    fw->ui->progressBar->setValue(bytes);
    qApp->processEvents();
}

FlashWrite::FlashWrite(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::FlashWrite)
{
    ui->setupUi(this);
}

FlashWrite::~FlashWrite()
{
    delete ui;
}

void FlashWrite::write(QString filename)
{
    int slot, sides;
    QString str;

    sides = FDS_getDiskSides((char*)filename.toStdString().c_str());
    slot = FDS_getEmptySlot(sides);

    str.sprintf("sides = %d, slot = %d",sides,slot);
    ui->label->setText(filename);
    ui->label->adjustSize();
    ui->progressBar->setRange(0,sides * 65500);
    show();

    if(FDS_writeFlash((char*)filename.toStdString().c_str(), slot, this, write_callback) == false) {
        hide();
        exec();
    }

    else {
        hide();
    }
}
