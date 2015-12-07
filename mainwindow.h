#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QLabel>

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = 0);
    ~MainWindow();

protected:
    QLabel *statusLabel;

protected:
    void updateList();
    void openFiles(QStringList &list);
    void dragEnterEvent(QDragEnterEvent *event) Q_DECL_OVERRIDE;
    void dropEvent(QDropEvent *event) Q_DECL_OVERRIDE;

private slots:
    void on_actionE_xit_triggered();

    void on_action_Delete_triggered();

private:
    Ui::MainWindow *ui;
};

#endif // MAINWINDOW_H
