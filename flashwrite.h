#ifndef FLASHWRITE_H
#define FLASHWRITE_H

#include <QDialog>

namespace Ui {
class FlashWrite;
}

class FlashWrite : public QDialog
{
    Q_OBJECT

public:
    explicit FlashWrite(QWidget *parent = 0);
    ~FlashWrite();

    void write(QString filename);

private:
    Ui::FlashWrite *ui;
    static void write_callback(void *data,int bytes);
};

#endif // FLASHWRITE_H
