#include <QDragEnterEvent>
#include <QMimeData>
#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "device.h"
#include "fds.h"
#include "spi.h"
#include "flashwrite.h"

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    QString str;

    statusLabel = new QLabel("Ready");
    ui->setupUi(this);
    ui->statusBar->addWidget(statusLabel);

    setAcceptDrops(true);

    ui->listWidget->addAction(ui->action_Save);
    ui->listWidget->addAction(ui->action_Info);
    ui->listWidget->addAction(ui->action_Delete);

    dev_open(str);
    statusLabel->setText(str);
    statusLabel->adjustSize();

    updateList();

}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::updateList()
{
    QStringList list;
    QString str;
    int empty = 0;

    FDS_list(list,&empty);
    str.sprintf("%d empty slots.", empty);
    ui->listWidget->clear();
    ui->listWidget->addItems(list);
    ui->label->setText(str);
    ui->label->adjustSize();
}

void MainWindow::openFiles(QStringList &list)
{
    FlashWrite *fw = new FlashWrite(this);

    for (int i = 0; i < list.size() && i < 32;i++) {
        QString filename = list.at(i);

        fw->write(filename);
    }

    updateList();
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *event)
{
    const QMimeData* mimeData = event->mimeData();

    // check for our needed mime type, here a file or a list of files
    if (mimeData->hasUrls()) {
        QStringList pathList;
        QList<QUrl> urlList = mimeData->urls();

        // extract the local paths of the files
        for (int i = 0; i < urlList.size() && i < 64; i++) {
            pathList.append(urlList.at(i).toLocalFile());
        }

        event->acceptProposedAction();

        // call a function to open the files
        openFiles(pathList);
    }
}

void MainWindow::on_actionE_xit_triggered()
{
    this->close();
}

void MainWindow::on_action_Delete_triggered()
{
    QString str = ui->listWidget->selectedItems().at(0)->text();
    int slot = FDS_findSlot((char*)str.toStdString().c_str());

    FDS_eraseSlot(slot);
    updateList();
}
