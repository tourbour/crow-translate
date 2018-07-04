/*
 *  Copyright © 2018 Gennady Chernyshchuk <genaloner@gmail.com>
 *
 *  This file is part of Crow Translate.
 *
 *  Crow Translate is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a get of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QSystemTrayIcon>
#include <QTranslator>
#include <QShortcut>
#include <QTimer>
#include <QButtonGroup>
#include <QMediaPlayer>

#include "qhotkey.h"
#include "qonlinetranslator.h"

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
#if defined(Q_OS_WIN)
    enum Interval {
        Day,
        Week,
        Month,
        Never
    };
#endif

    explicit MainWindow(QWidget *parent = 0);
    ~MainWindow();

signals:
    void translationTextChanged(const QString &text);
    void sourceButtonChanged(QAbstractButton *button, const int &id);
    void translationButtonChanged(QAbstractButton *button, const int &id);
    void playSourceButtonIconChanged(QIcon icon);
    void stopSourceButtonEnabled(bool enabled);
    void playTranslationButtonIconChanged(QIcon icon);
    void stopTranslationButtonEnabled(bool enabled);

private slots:
    void on_translateButton_clicked();
    void on_swapButton_clicked();
    void on_copyToSourceButton_clicked();
    void on_settingsButton_clicked();

    void on_playSourceButton_clicked();
    void on_playTranslationButton_clicked();

    void on_stopSourceButton_clicked();
    void on_stopTranslationButton_clicked();

    void on_copySourceButton_clicked();
    void on_copyTranslationButton_clicked();
    void on_copyAllTranslationButton_clicked();

    void on_autoSourceButton_triggered(QAction *language);
    void on_autoTranslationButton_triggered(QAction *language);

    void toggleSourceButton(QAbstractButton *button, const bool &checked);
    void toggleTranslationButton(QAbstractButton *button, const bool &checked);

    void on_autoTranslateCheckBox_toggled(const bool &state);

    void translateSelection();
    void playSelection();
    void playTranslatedSelection();

    void display();
    void loadProxy();
    void resetAutoSourceButtonText();

private:
    void loadSettings();
    QList<QAction *> languagesList();
    QString selectedText();

    void loadLanguageButtons(QButtonGroup *group, const QString &settingsName);
    void insertLanguage(QButtonGroup *group, const QString &settingsName, const QString &languageCode);
    void checkSourceButton(const int &id, const bool &checked);
    void checkTranslationButton(const int &id, const bool &checked);

    Ui::MainWindow *ui;
    QTranslator interfaceTranslator;
    QTimer autoTranslateTimer;
    QOnlineTranslator *onlineTranslator;
    QMenu *languagesMenu;

    QMediaPlayer sourcePlayer;
    QMediaPlaylist sourcePlaylist;
    QMediaPlayer translationPlayer;
    QMediaPlaylist translationPlaylist;
    QMediaPlayer selectionPlayer;
    QMediaPlaylist selectionPlaylist;

    QMenu *trayMenu;
    QSystemTrayIcon *trayIcon;

    QShortcut *closeWindowsShortcut;
    QHotkey *translateSelectionHotkey;
    QHotkey *playSelectionHotkey;
    QHotkey *playTranslatedSelectionHotkey;
    QHotkey *stopSelectionHotkey;
    QHotkey *showMainWindowHotkey;

    QButtonGroup *sourceButtonGroup;
    QButtonGroup *translationButtonGroup;
};

#endif // MAINWINDOW_H
