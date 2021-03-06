/************************************************************************

    configuration.h

    ld-process-efm - EFM data decoder
    Copyright (C) 2019 Simon Inns

    This file is part of ld-decode-tools.

    ld-process-efm is free software: you can redistribute it and/or
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

#ifndef CONFIGURATION_H
#define CONFIGURATION_H

#include <QObject>
#include <QCoreApplication>
#include <QSettings>
#include <QStandardPaths>
#include <QApplication>
#include <QDir>
#include <QDebug>

class Configuration : public QObject
{
    Q_OBJECT
public:
    explicit Configuration(QObject *parent = nullptr);
    ~Configuration() override;

    void writeConfiguration(void);
    void readConfiguration(void);

    // Get and set methods - Directories
    void setSourceDirectory(QString sourceDirectory);
    QString getSourceDirectory(void);
    void setAudioDirectory(QString audioDirectory);
    QString getAudioDirectory(void);
    void setDataDirectory(QString dataDirectory);
    QString getDataDirectory(void);

    // Get and set methods - windows
    void setMainWindowGeometry(QByteArray mainWindowGeometry);
    QByteArray getMainWindowGeometry(void);

private:
    QSettings *configuration;

    // Directories
    struct Directories {
        QString sourceDirectory; // Last used directory for .efm files
        QString audioDirectory; // Last used directory for .pcm files
        QString dataDirectory; // Last used directory for .dat files
    };

    // Window geometry and settings
    struct Windows {
        QByteArray mainWindowGeometry;
    };

    // Overall settings structure
    struct Settings {
        qint32 version;
        Directories directories;
        Windows windows;
    } settings;

    void setDefault(void);
};

#endif // CONFIGURATION_H
