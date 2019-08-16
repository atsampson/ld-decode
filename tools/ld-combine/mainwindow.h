/************************************************************************

    mainwindow.h

    ld-combine - TBC combination and enhancement tool
    Copyright (C) 2019 Simon Inns

    This file is part of ld-decode-tools.

    ld-combine is free software: you can redistribute it and/or
    modify it under the terms of the GNU General Public License as
    published by the Free Software Foundation, either version 3 of the
    License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

************************************************************************/

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QFileDialog>
#include <QMessageBox>
#include <QLabel>

#include "aboutdialog.h"
#include "busydialog.h"
#include "reportdialog.h"

#include "configuration.h"
#include "tbcsources.h"

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    // Menu slots
    void on_actionOpen_new_source_triggered();
    void on_actionClose_current_source_triggered();
    void on_actionExit_triggered();
    void on_actionAbout_ld_combine_triggered();

    // MainWindow widget slots
    void on_previousFramePushButton_clicked();
    void on_nextFramePushButton_clicked();
    void on_sourceSelectComboBox_currentIndexChanged(int index);
    void on_frameNumberSpinBox_valueChanged(int arg1);
    void on_frameNumberHorizontalSlider_valueChanged(int value);

    // TbcSources class slots
    void on_setBusy(QString message, bool showProgress, qint32 progress);
    void on_clearBusy();
    void on_updateSources(bool isSuccessful);

    void on_actionSource_report_triggered();

private:
    Ui::MainWindow *ui;
    Configuration configuration;
    TbcSources tbcSources;
    QLabel applicationStatus;

    // Dialogue objects
    AboutDialog *aboutDialog;
    BusyDialog *busyDialog;
    ReportDialog *reportDialog;

    // GUI update methods
    void updateGUIsourcesAvailable();
    void updateGUInoSourcesAvailable();
    void sourceChanged();
    void updateSourceSelectionCombobox();
    void showFrame();
};

#endif // MAINWINDOW_H