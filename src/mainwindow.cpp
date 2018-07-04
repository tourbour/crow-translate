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

#include "mainwindow.h"

#include <QClipboard>
#include <QNetworkProxy>
#include <QSettings>

#if defined(Q_OS_WIN)
#include <QMimeData>
#include <QTimer>
#include <windows.h>

#include "updaterwindow.h"
#endif

#include "ui_mainwindow.h"
#include "popupwindow.h"
#include "settingsdialog.h"

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui (new Ui::MainWindow),
    languagesMenu (new QMenu),
    trayMenu (new QMenu(this)),
    trayIcon (new QSystemTrayIcon(this)),
    closeWindowsShortcut (new QShortcut(this)),
    translateSelectionHotkey (new QHotkey(this)),
    playSelectionHotkey (new QHotkey(this)),
    playTranslatedSelectionHotkey (new QHotkey(this)),
    stopSelectionHotkey (new QHotkey(this)),
    showMainWindowHotkey (new QHotkey(this)),
    sourceButtonGroup (new QButtonGroup(this)),
    translationButtonGroup (new QButtonGroup(this))
{
    // Localization
    QSettings settings;
    QString localeCode = settings.value("Language", "auto").toString();
    if (localeCode != "auto")
        QLocale::setDefault(QLocale(localeCode));
    if (interfaceTranslator.load(QLocale(), QString("crow"), QString("_"), QString(":/translations")))
        qApp->installTranslator(&interfaceTranslator);

    ui->setupUi(this);

    // Shortcuts
    connect(closeWindowsShortcut, &QShortcut::activated, this, &MainWindow::close);
    connect(showMainWindowHotkey, &QHotkey::activated, this, &MainWindow::display);
    connect(playSelectionHotkey, &QHotkey::activated, this, &MainWindow::playSelection);
    connect(playTranslatedSelectionHotkey, &QHotkey::activated, this, &MainWindow::playTranslatedSelection);
    connect(stopSelectionHotkey, &QHotkey::activated, &selectionPlayer, &QMediaPlayer::stop);
    connect(translateSelectionHotkey, &QHotkey::activated, this, &MainWindow::translateSelection);

    // Text speaking
    sourcePlayer.setPlaylist(&sourcePlaylist); // Use playlist to split long queries due Google limit
    translationPlayer.setPlaylist(&translationPlaylist);
    selectionPlayer.setPlaylist(&selectionPlaylist);
    connect(ui->sourceEdit, &QPlainTextEdit::textChanged, &sourcePlayer, &QMediaPlayer::stop);

    // Players buttons
    connect(&sourcePlayer, &QMediaPlayer::stateChanged, [&](QMediaPlayer::State state){
        switch (state) {
        case QMediaPlayer::PlayingState:
        {
            ui->playSourceButton->setIcon(QIcon::fromTheme("media-playback-pause"));
            emit playSourceButtonIconChanged(ui->playSourceButton->icon());
            ui->stopSourceButton->setEnabled(true);
            emit stopSourceButtonEnabled(true);
            break;
        }
        case QMediaPlayer::PausedState:
        {
            ui->playSourceButton->setIcon(QIcon::fromTheme("media-playback-start"));
            emit playSourceButtonIconChanged(ui->playSourceButton->icon());
            break;
        }
        case QMediaPlayer::StoppedState:
        {
            ui->playSourceButton->setIcon(QIcon::fromTheme("media-playback-start"));
            emit playSourceButtonIconChanged(ui->playSourceButton->icon());
            ui->stopSourceButton->setEnabled(false);
            emit stopSourceButtonEnabled(false);
            break;
        }
        }
    });
    connect(&translationPlayer, &QMediaPlayer::stateChanged, [&](QMediaPlayer::State state){
        switch (state) {
        case QMediaPlayer::PlayingState:
        {
            ui->playTranslationButton->setIcon(QIcon::fromTheme("media-playback-pause"));
            emit playTranslationButtonIconChanged(ui->playTranslationButton->icon());
            ui->stopTranslationButton->setEnabled(true);
            emit stopTranslationButtonEnabled(true);
            break;
        }
        case QMediaPlayer::PausedState:
        {
            ui->playTranslationButton->setIcon(QIcon::fromTheme("media-playback-start"));
            emit playTranslationButtonIconChanged(ui->playTranslationButton->icon());
            break;
        }
        case QMediaPlayer::StoppedState:
        {
            ui->playTranslationButton->setIcon(QIcon::fromTheme("media-playback-start"));
            emit playTranslationButtonIconChanged(ui->playTranslationButton->icon());
            ui->stopTranslationButton->setEnabled(false);
            emit stopTranslationButtonEnabled(false);
            break;
        }
        }
    });

    // Source button group
    sourceButtonGroup->addButton(ui->autoSourceButton, 0);
    sourceButtonGroup->addButton(ui->firstSourceButton, 1);
    sourceButtonGroup->addButton(ui->secondSourceButton, 2);
    sourceButtonGroup->addButton(ui->thirdSourceButton, 3);
    connect(sourceButtonGroup, qOverload<QAbstractButton*, bool>(&QButtonGroup::buttonToggled), this, &MainWindow::toggleSourceButton);

    // Translation button group
    translationButtonGroup->addButton(ui->autoTranslationButton, 0);
    translationButtonGroup->addButton(ui->firstTranslationButton, 1);
    translationButtonGroup->addButton(ui->secondTranslationButton, 2);
    translationButtonGroup->addButton(ui->thirdTranslationButton, 3);
    connect(translationButtonGroup, qOverload<QAbstractButton*, bool>(&QButtonGroup::buttonToggled), this, &MainWindow::toggleTranslationButton);

    // System tray icon
    trayMenu->addAction(QIcon::fromTheme("window"), tr("Show window"), this, &MainWindow::show);
    trayMenu->addAction(QIcon::fromTheme("dialog-object-properties"), tr("Settings"), this, &MainWindow::on_settingsButton_clicked);
    trayMenu->addAction(QIcon::fromTheme("application-exit"), tr("Exit"), qApp, &QApplication::quit);
    trayIcon->setContextMenu(trayMenu);
    connect(trayIcon, &QSystemTrayIcon::activated, [&](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger) {
            if (!this->isVisible())
                display();
            else
                hide();
        }
    });

    // Timer for automatic translation
    autoTranslateTimer.setSingleShot(true);
    connect(ui->sourceEdit, &QPlainTextEdit::textChanged, [&]() {
        autoTranslateTimer.start(500);
    });
    connect(&autoTranslateTimer, &QTimer::timeout, [&]() {
        if (ui->translateButton->isEnabled())
            on_translateButton_clicked();
        else
            autoTranslateTimer.start(500);
    });

    // Add languages menu to auto-language buttons
    onlineTranslator = new QOnlineTranslator(this);
    languagesMenu->addActions(languagesList());
    ui->autoSourceButton->setMenu(languagesMenu);
    ui->autoTranslationButton->setMenu(languagesMenu);

    // Load settings
    loadLanguageButtons(sourceButtonGroup, "Source");
    loadLanguageButtons(translationButtonGroup, "Translation");
    loadSettings();
    loadProxy();
    restoreGeometry(settings.value("MainWindowGeometry").toByteArray());

    // Check for updates
#if defined(Q_OS_WIN)
    int updateInterval = settings.value("CheckForUpdatesInterval", 1).toInt();
    if (updateInterval < Interval::Never) {
        QDate checkDate = settings.value("LastUpdateCheckDate", QDate::currentDate()).toDate();
        switch (updateInterval) {
        case Interval::Day:
        {
            checkDate = checkDate.addDays(1);
            break;
        }
        case Interval::Week:
        {
            checkDate = checkDate.addDays(7);
            break;
        }
        case Interval::Month:
        {
            checkDate = checkDate.addMonths(1);
            break;
        }
        }
        if (QDate::currentDate() >= checkDate) {
            QGitRelease release("Shatur95", "Crow-Translate");
            if (!release.error() && qApp->applicationVersion() == release.tagName()) {
                UpdaterWindow *updaterWindow = new UpdaterWindow(release, this);
                updaterWindow->show();
            }
        }
    }
#endif
}

MainWindow::~MainWindow()
{
    QSettings settings;
    settings.setValue("MainWindowGeometry", saveGeometry());
    delete ui;
}

void MainWindow::on_translateButton_clicked()
{
    if (ui->sourceEdit->toPlainText() != "") {
        ui->translateButton->setEnabled(false);

        // Languages
        QSettings settings;
        QString translatorlanguage = settings.value("Language", "auto").toString();
        QString sourcelanguage;
        if (ui->autoSourceButton->isChecked())
            sourcelanguage = "auto";
        else
            sourcelanguage = sourceButtonGroup->checkedButton()->toolTip();
        QString translationlanguage;
        if (ui->autoTranslationButton->isChecked()) {
            // Determine the target language
            translationlanguage = settings.value("Translation/PrimaryLanguage", "auto").toString();
            if (translationlanguage == "auto")
                translationlanguage = QOnlineTranslator::defaultLocaleToCode();
            if (translationlanguage == sourcelanguage) {
                translationlanguage = settings.value("Translation/SecondaryLanguage", "en").toString();
                if (translationlanguage == "auto")
                    translationlanguage = QOnlineTranslator::defaultLocaleToCode();
            }
        }
        else
            translationlanguage = translationButtonGroup->checkedButton()->toolTip();

        onlineTranslator->translate(ui->sourceEdit->toPlainText(), translationlanguage, sourcelanguage, translatorlanguage);

        // Check for network error
        if (onlineTranslator->error()) {
            ui->translationEdit->setHtml(onlineTranslator->translation());
            ui->translateButton->setEnabled(true);
            emit translationTextChanged(onlineTranslator->translation());
            return;
        }

        // Re-translate to a secondary or a primary language if the autodetected source language and the source language are the same
        if (ui->autoTranslationButton->isChecked() && onlineTranslator->sourceLanguage() == onlineTranslator->translationLanguage()) {
            QString primaryLanguage = settings.value("Translation/PrimaryLanguage", "auto").toString();
            QString secondaryLanguage = settings.value("Translation/SecondaryLanguage", "en").toString();
            if (primaryLanguage == "auto")
                primaryLanguage = QOnlineTranslator::defaultLocaleToCode();
            if (secondaryLanguage == "auto")
                secondaryLanguage = QOnlineTranslator::defaultLocaleToCode();
            if (translationlanguage == primaryLanguage)
                translationlanguage = secondaryLanguage;
            else
                translationlanguage = primaryLanguage;

            onlineTranslator->translate(ui->sourceEdit->toPlainText(), translationlanguage, sourcelanguage, translatorlanguage);

            // Check for network error
            if (onlineTranslator->error()) {
                ui->translationEdit->setHtml(onlineTranslator->translation());
                ui->translateButton->setEnabled(true);
                emit translationTextChanged(onlineTranslator->translation());
                return;
            }
        }

        // Reset the playback of the text
        translationPlayer.stop();

        // Display languages on "Auto" buttons.
        if (ui->autoSourceButton->isChecked() && onlineTranslator->sourceLanguage() != ui->autoSourceButton->toolTip()) {
            ui->autoSourceButton->setText(tr("Auto") + " (" + onlineTranslator->codeToLanguage(onlineTranslator->sourceLanguage()) + ")");
            ui->autoSourceButton->setToolTip(onlineTranslator->sourceLanguage());
            emit sourceButtonChanged(ui->autoSourceButton, 0);
            connect(ui->sourceEdit, &QPlainTextEdit::textChanged, this, &MainWindow::resetAutoSourceButtonText);
        }
        if (ui->autoTranslationButton->isChecked()) {
            if (onlineTranslator->translationLanguage() != ui->autoTranslationButton->toolTip()) {
                ui->autoTranslationButton->setText(tr("Auto") + " (" + onlineTranslator->codeToLanguage(onlineTranslator->translationLanguage()) + ")");
                ui->autoTranslationButton->setToolTip(onlineTranslator->translationLanguage());
                emit translationButtonChanged(ui->autoTranslationButton, 0);
            }
        }
        else {
            if (ui->autoTranslationButton->toolTip() != "auto") {
                ui->autoTranslationButton->setText(tr("Auto"));
                ui->autoTranslationButton->setToolTip("auto");
                emit translationButtonChanged(ui->autoTranslationButton, 0);
            }
        }

        // Show translation and transcription
        ui->translationEdit->setHtml(onlineTranslator->translation().toHtmlEscaped().replace("\n", "<br>"));
        if (onlineTranslator->translationTranscription() != "" && settings.value("Translation/ShowTranslationTransliteration", true).toBool())
            ui->translationEdit->append("<font color=\"grey\"><i>/" + onlineTranslator->translationTranscription().replace("\n", "/<br>/") + "/</i></font>");
        if (onlineTranslator->sourceTranscription() != "" && settings.value("Translation/ShowSourceTransliteration", true).toBool())
            ui->translationEdit->append("<font color=\"grey\"><i><b>(" + onlineTranslator->sourceTranscription().replace("\n", "/<br>/") + ")</b></i></font>");
        ui->translationEdit->append("");

        // Show translation options
        if (!onlineTranslator->translationOptionsList().isEmpty() && settings.value("Translation/TranslationOptions", true).toBool()) {
            ui->translationEdit->append("<font color=\"grey\"><i>" + onlineTranslator->source() + "</i> – " + tr("translation options:") + "</font>");
            foreach (auto translationOptions, onlineTranslator->translationOptionsList()) {
                ui->translationEdit->append("<b>" + translationOptions.typeOfSpeech() + "</b>");
                QTextBlockFormat indent;
                indent.setTextIndent(20);
                ui->translationEdit->textCursor().setBlockFormat(indent);
                for (auto i = 0; i <  translationOptions.count(); i++) {
                    QString wordLine;
                    if (!translationOptions.gender(i).isEmpty())
                        wordLine.append("<i>" + translationOptions.gender(i) + "</i> ");
                    wordLine.append(translationOptions.word(i) + ": ");

                    wordLine.append("<font color=\"grey\"><i>");
                    wordLine.append(translationOptions.translations(i).at(0));
                    for (auto j = 1; j < translationOptions.translations(i).size(); j++) {
                        wordLine.append(", " + translationOptions.translations(i).at(j));
                    }
                    wordLine.append("</i></font>");
                    ui->translationEdit->append(wordLine);
                }
                indent.setTextIndent(0);
                ui->translationEdit->textCursor().setBlockFormat(indent);
                ui->translationEdit->append("");
            }
        }

        // Show definitions
        if (!onlineTranslator->definitionsList().isEmpty() && settings.value("Translation/Definitions", true).toBool()) {
            ui->translationEdit->append("<font color=\"grey\"><i>" + onlineTranslator->source() + "</i> – " + tr("definitions:") + "</font>");
            foreach (auto definition, onlineTranslator->definitionsList()) {
                ui->translationEdit->append("<b>" + definition.typeOfSpeech() + "</b>");
                QTextBlockFormat indent;
                indent.setTextIndent(20);
                ui->translationEdit->textCursor().setBlockFormat(indent);
                ui->translationEdit->append(definition.description());
                ui->translationEdit->append("<font color=\"grey\"><i>" + definition.example()+ "</i></font>");
                indent.setTextIndent(0);
                ui->translationEdit->textCursor().setBlockFormat(indent);
                ui->translationEdit->append("");
            }
        }

        ui->translationEdit->moveCursor(QTextCursor::Start);
        emit translationTextChanged(ui->translationEdit->toHtml());
        ui->translateButton->setEnabled(true);
    }
    else
        ui->translationEdit->clear();
}

void MainWindow::on_swapButton_clicked()
{
    QString sourceLanguage = sourceButtonGroup->checkedButton()->toolTip();
    QString translationLanguage = translationButtonGroup->checkedButton()->toolTip();

    // Insert current translation language to source buttons
    if (translationLanguage == "auto")
        sourceButtonGroup->button(0)->setChecked(true);
    else
        insertLanguage(sourceButtonGroup, "Source", translationLanguage);

    // Insert current source language to translation buttons
    if (sourceLanguage == "auto")
        translationButtonGroup->button(0)->setChecked(true);
    else
        insertLanguage(translationButtonGroup, "Translation", sourceLanguage);
}

void MainWindow::on_copyToSourceButton_clicked()
{
    ui->sourceEdit->setPlainText(onlineTranslator->translation());
    ui->sourceEdit->moveCursor(QTextCursor::End);
}

void MainWindow::on_settingsButton_clicked()
{
    SettingsDialog config(languagesMenu, this);
    if (config.exec()) {
        if (config.languageChanged()) {
            // Reload localization
            QSettings settings;
            QString localeCode = settings.value("Language", "auto").toString();
            if (localeCode == "auto") {
                QLocale::setDefault(QLocale::system());
                localeCode = QOnlineTranslator::defaultLocaleToCode();
            }
            else
                QLocale::setDefault(QLocale(localeCode));
            if (interfaceTranslator.load(QLocale(), QString("crow"), QString("_"), QString(":/translations")))
                qApp->installTranslator(&interfaceTranslator);

            // Reload UI
            delete onlineTranslator;
            onlineTranslator = new QOnlineTranslator(this);
            ui->retranslateUi(this);
            trayMenu->actions().at(0)->setText(tr("Show window"));
            trayMenu->actions().at(1)->setText(tr("Settings"));
            trayMenu->actions().at(2)->setText(tr("Exit"));
            languagesMenu->clear();
            languagesMenu->addActions(languagesList());
            loadLanguageButtons(sourceButtonGroup, "Source");
            loadLanguageButtons(translationButtonGroup, "Translation");
            ui->autoTranslationButton->setToolTip("auto");
            ui->autoTranslationButton->setText(tr("Auto"));
            ui->autoSourceButton->setToolTip("auto");
            ui->autoSourceButton->setText(tr("Auto"));
        }
        if (config.proxyChanged())
            loadProxy();

        config.done(0);
        loadSettings();
    }
}

void MainWindow::on_playSourceButton_clicked()
{
    if (ui->sourceEdit->toPlainText() != "") {
        switch (sourcePlayer.state()) {
        case QMediaPlayer::PlayingState:
        {
            sourcePlayer.pause();
            break;
        }
        case QMediaPlayer::PausedState:
        {
            // Pause other players
            if (translationPlayer.state() == QMediaPlayer::PlayingState)
                translationPlayer.pause();
            else
                if (selectionPlayer.state() == QMediaPlayer::PlayingState)
                    selectionPlayer.pause();

            sourcePlayer.play();
            break;
        }
        case QMediaPlayer::StoppedState:
        {
            // Pause other players
            if (translationPlayer.state() == QMediaPlayer::PlayingState)
                translationPlayer.pause();
            else
                if (selectionPlayer.state() == QMediaPlayer::PlayingState)
                    selectionPlayer.pause();

            sourcePlaylist.clear();
            sourcePlaylist.addMedia(QOnlineTranslator::media(ui->sourceEdit->toPlainText(), sourceButtonGroup->checkedButton()->toolTip()));
            sourcePlayer.play();
            break;
        }
        }
    }
    else
        qDebug() << tr("Text field is empty");
}

void MainWindow::on_playTranslationButton_clicked()
{
    if (ui->translationEdit->toPlainText() != "") {
        switch (translationPlayer.state()) {
        case QMediaPlayer::PlayingState:
        {
            translationPlayer.pause();
            break;
        }
        case QMediaPlayer::PausedState:
        {
            if (sourcePlayer.state() == QMediaPlayer::PlayingState)
                sourcePlayer.pause();
            else
                if (selectionPlayer.state() == QMediaPlayer::PlayingState)
                    selectionPlayer.pause();
            translationPlayer.play();
            break;
        }
        case QMediaPlayer::StoppedState:
        {
            if (sourcePlayer.state() == QMediaPlayer::PlayingState)
                sourcePlayer.pause();
            else
                if (selectionPlayer.state() == QMediaPlayer::PlayingState)
                selectionPlayer.pause();
            translationPlaylist.clear();
            translationPlaylist.addMedia(onlineTranslator->translationMedia());
            translationPlayer.play();
            break;
        }
        }
    }
    else
        qDebug() << tr("Text field is empty");
}

void MainWindow::on_stopSourceButton_clicked()
{
    sourcePlayer.stop();
}

void MainWindow::on_stopTranslationButton_clicked()
{
    translationPlayer.stop();
}

void MainWindow::on_copySourceButton_clicked()
{
    if (ui->sourceEdit->toPlainText() != "")
        QApplication::clipboard()->setText(onlineTranslator->source());
    else
        qDebug() << tr("Text field is empty");
}

void MainWindow::on_copyTranslationButton_clicked()
{
    if (ui->translationEdit->toPlainText() != "")
        QApplication::clipboard()->setText(onlineTranslator->translation());
    else
        qDebug() << tr("Text field is empty");
}

void MainWindow::on_copyAllTranslationButton_clicked()
{
    if (ui->translationEdit->toPlainText() != "")
        QApplication::clipboard()->setText(ui->translationEdit->toPlainText());
    else
        qDebug() << tr("Text field is empty");
}

void MainWindow::on_autoSourceButton_triggered(QAction *language)
{
    insertLanguage(sourceButtonGroup, "Source", language->toolTip());
}

void MainWindow::on_autoTranslationButton_triggered(QAction *language)
{
    insertLanguage(translationButtonGroup, "Translation", language->toolTip());
}

void MainWindow::toggleSourceButton(QAbstractButton *button, const bool &checked)
{
    if (checked) {
        // If the target and source languages are the same (and they are not automatic translation buttons), then change target language to previous source language       
        QSettings settings;
        if (button != sourceButtonGroup->button(0) && !translationButtonGroup->button(0)->isChecked() &&
                button->toolTip() == translationButtonGroup->checkedButton()->toolTip())  {
            insertLanguage(translationButtonGroup, "Translation", sourceButtonGroup->button(settings.value("Buttons/CheckedSourceButton", 0).toInt())->toolTip());
            settings.setValue("Buttons/CheckedTranslationButton", translationButtonGroup->checkedId()); // Save the pressed button
        }

        // Translate the text automatically if "Automatically translate" is checked or if a pop-up window is open
        if (ui->autoTranslateCheckBox->isChecked() || this->isHidden())
            autoTranslateTimer.start(300);

        settings.setValue("Buttons/CheckedSourceButton", sourceButtonGroup->checkedId());
    }
}

void MainWindow::toggleTranslationButton(QAbstractButton *button, const bool &checked)
{
    if (checked) {
        // If the target and source languages are the same (and they are not automatic translation buttons), then source target language to previous target language
        QSettings settings;
        if (button != translationButtonGroup->button(0) && !sourceButtonGroup->button(0)->isChecked() &&
                button->toolTip() == sourceButtonGroup->checkedButton()->toolTip()) {
            insertLanguage(sourceButtonGroup, "Source", translationButtonGroup->button(settings.value("Buttons/CheckedTranslationButton", 0).toInt())->toolTip());
            settings.setValue("Buttons/CheckedSourceButton", sourceButtonGroup->checkedId());
        }

        // Translate the text automatically if "Automatically translate" is checked or if a pop-up window is open
        if (ui->autoTranslateCheckBox->isChecked() || this->isHidden())
            autoTranslateTimer.start(300);

        settings.setValue("Buttons/CheckedTranslationButton", translationButtonGroup->checkedId());
    }
}

void MainWindow::on_autoTranslateCheckBox_toggled(const bool &state)
{
    if (state) {
        if (ui->translateButton->isEnabled())
            on_translateButton_clicked();
        else
            autoTranslateTimer.start(500);
    }

    autoTranslateTimer.blockSignals(!state);

    QSettings settings;
    settings.setValue("AutoTranslate", state);
}

void MainWindow::translateSelection()
{
    // Prevent pressing the translation hotkey again
    translateSelectionHotkey->blockSignals(true);

    // Stop previous playback
    sourcePlayer.stop();
    translationPlayer.stop();

    QSettings settings;
    if (this->isHidden() && settings.value("WindowMode", 0).toInt() == 0) {
        // Show popup
        PopupWindow *popup = new PopupWindow(languagesMenu, sourceButtonGroup, translationButtonGroup, this);
        connect(this, &MainWindow::translationTextChanged, popup->translationEdit(), &QTextEdit::setHtml);

        connect(this, &MainWindow::sourceButtonChanged, popup, &PopupWindow::loadSourceButton);
        connect(this, &MainWindow::translationButtonChanged, popup, &PopupWindow::loadTranslationButton);

        connect(this, &MainWindow::playSourceButtonIconChanged, popup->playSourceButton(), &QToolButton::setIcon);
        connect(this, &MainWindow::stopSourceButtonEnabled, popup->stopSourceButton(), &QToolButton::setEnabled);
        connect(this, &MainWindow::playTranslationButtonIconChanged, popup->playTranslationButton(), &QToolButton::setIcon);
        connect(this, &MainWindow::stopTranslationButtonEnabled, popup->stopTranslationButton(), &QToolButton::setEnabled);

        connect(sourceButtonGroup, qOverload<int, bool>(&QButtonGroup::buttonToggled), popup, &PopupWindow::checkSourceButton);
        connect(translationButtonGroup, qOverload<int, bool>(&QButtonGroup::buttonToggled), popup, &PopupWindow::checkTranslationButton);

        connect(popup, &PopupWindow::destroyed, &sourcePlayer, &QMediaPlayer::stop);
        connect(popup, &PopupWindow::destroyed, &translationPlayer, &QMediaPlayer::stop);

        connect(popup->sourceButtons(),  qOverload<int, bool>(&QButtonGroup::buttonToggled), this, &MainWindow::checkSourceButton);
        connect(popup->translationButtons(),  qOverload<int, bool>(&QButtonGroup::buttonToggled), this, &MainWindow::checkTranslationButton);

        connect(popup->autoSourceButton(), &QToolButton::triggered, this, &MainWindow::on_autoSourceButton_triggered);
        connect(popup->autoTranslationButton(), &QToolButton::triggered, this, &MainWindow::on_autoTranslationButton_triggered);

        connect(popup->swapButton(), &QToolButton::clicked, this, &MainWindow::on_swapButton_clicked);
        connect(popup->playSourceButton(), &QToolButton::clicked, this, &MainWindow::on_playSourceButton_clicked);
        connect(popup->stopSourceButton(), &QToolButton::clicked, &sourcePlayer, &QMediaPlayer::stop);
        connect(popup->copySourceButton(), &QToolButton::clicked, this, &MainWindow::on_copySourceButton_clicked);
        connect(popup->playTranslationButton(), &QToolButton::clicked, this, &MainWindow::on_playTranslationButton_clicked);
        connect(popup->stopTranslationButton(), &QToolButton::clicked, &translationPlayer, &QMediaPlayer::stop);
        connect(popup->copyTranslationButton(), &QToolButton::clicked, this, &MainWindow::on_copyTranslationButton_clicked);
        connect(popup->copyAllTranslationButton(), &QToolButton::clicked, this, &MainWindow::on_copyAllTranslationButton_clicked);

        // Restore the keyboard shortcut
        connect(popup, &PopupWindow::destroyed, [this] {
            translateSelectionHotkey->blockSignals(false);
        });

        // Send selected text to source field and translate it
        if (ui->autoTranslateCheckBox->isChecked()) {
            ui->sourceEdit->blockSignals(true);
            ui->sourceEdit->setPlainText(selectedText());
            ui->sourceEdit->blockSignals(false);
        }
        else
            ui->sourceEdit->setPlainText(selectedText());

        on_translateButton_clicked();
        popup->show();
    }
    else {
        // Send selected text to source field and translate it
        if (ui->autoTranslateCheckBox->isChecked()) {
            ui->sourceEdit->blockSignals(true);
            ui->sourceEdit->setPlainText(selectedText());
            ui->sourceEdit->blockSignals(false);
        }
        else
            ui->sourceEdit->setPlainText(selectedText());

        display();
        on_translateButton_clicked();

        // Restore the keyboard shortcut
        translateSelectionHotkey->blockSignals(false);
    }
}

void MainWindow::playSelection()
{
    QString selection = selectedText();
    if (selection != "") {
        // Pause other players
        if (translationPlayer.state() == QMediaPlayer::PlayingState)
            translationPlayer.pause();
        else
            if (sourcePlayer.state() == QMediaPlayer::PlayingState)
                sourcePlayer.pause();

        selectionPlaylist.clear();
        selectionPlaylist.addMedia(QOnlineTranslator::media(selection));
        selectionPlayer.play();
    }
    else
        qDebug() << tr("The selection does not contain text");
}

void MainWindow::playTranslatedSelection()
{
    QString selection = selectedText();
    if (selection != "") {
        // Pause other players
        if (translationPlayer.state() == QMediaPlayer::PlayingState)
            translationPlayer.pause();
        else
            if (sourcePlayer.state() == QMediaPlayer::PlayingState)
                sourcePlayer.pause();

        // Detect languages
        QSettings settings;
        QString primaryLanguage = settings.value("Translation/PrimaryLanguage", "auto").toString();
        QString secondaryLanguage = settings.value("Translation/SecondaryLanguage", "en").toString();
        if (primaryLanguage == "auto")
            primaryLanguage = QOnlineTranslator::defaultLocaleToCode();
        if (secondaryLanguage == "auto")
            secondaryLanguage = QOnlineTranslator::defaultLocaleToCode();

        // Translate text
        QOnlineTranslator onlineTranslator(selection, primaryLanguage);
        if (onlineTranslator.sourceLanguage() == primaryLanguage)
            onlineTranslator.translate(selection, secondaryLanguage);

        selectionPlaylist.clear();
        selectionPlaylist.addMedia(QOnlineTranslator::media(onlineTranslator.translation()));
        selectionPlayer.play();
    }
    else
        qDebug() << tr("The selection does not contain text");
}

void MainWindow::display()
{
    showNormal();
    activateWindow();
}

void MainWindow::loadProxy()
{
    QSettings settings;
    QNetworkProxy proxy;
    proxy.setType(static_cast<QNetworkProxy::ProxyType>(settings.value("Connection/ProxyType", QNetworkProxy::DefaultProxy).toInt()));
    if (proxy.type() == QNetworkProxy::HttpProxy || proxy.type() == QNetworkProxy::Socks5Proxy) {
        proxy.setHostName(settings.value("Connection/ProxyHost", "").toString());
        proxy.setPort(settings.value("Connection/ProxyPort", 8080).toInt());
        if (settings.value("Connection/ProxyAuthEnabled", false).toBool()) {
            proxy.setUser(settings.value("Connection/ProxyUsername", "").toString());
            proxy.setPassword(settings.value("Connection/ProxyPassword", "").toString());
        }
    }
    QNetworkProxy::setApplicationProxy(proxy);
}

void MainWindow::resetAutoSourceButtonText()
{
    ui->autoSourceButton->setText(tr("Auto"));
    ui->autoSourceButton->setToolTip("auto");
    disconnect(ui->sourceEdit, &QPlainTextEdit::textChanged, this, &MainWindow::resetAutoSourceButtonText);
    emit sourceButtonChanged(ui->autoSourceButton, 0);
}

void MainWindow::loadSettings()
{
    QSettings settings;

    // Autotranslation
    ui->autoTranslateCheckBox->setChecked(settings.value("AutoTranslate", true).toBool());
    on_autoTranslateCheckBox_toggled(ui->autoTranslateCheckBox->isChecked());

    // System tray icon
    QString iconName = settings.value("TrayIcon", "crow-translate-tray").toString();
    if (iconName == "custom") {
        QIcon customIcon(settings.value("CustomIconPath", "").toString());
        if (customIcon.isNull())
            trayIcon->setIcon(QIcon::fromTheme("dialog-error"));
        else
            trayIcon->setIcon(customIcon);
    }
    else {
        QIcon crowIcon = QIcon::fromTheme(iconName);
        if (crowIcon.isNull())
            trayIcon->setIcon(QIcon::fromTheme("dialog-error"));
        else
            trayIcon->setIcon(crowIcon);
    }
    trayIcon->setVisible(settings.value("TrayIconVisible", true).toBool());
    QApplication::setQuitOnLastWindowClosed(!settings.value("TrayIconVisible", true).toBool());

    // Language buttons style
    ui->firstSourceButton->setToolButtonStyle(qvariant_cast<Qt::ToolButtonStyle>(settings.value("WindowLanguagesStyle", Qt::ToolButtonTextBesideIcon)));
    ui->secondSourceButton->setToolButtonStyle(qvariant_cast<Qt::ToolButtonStyle>(settings.value("WindowLanguagesStyle", Qt::ToolButtonTextBesideIcon)));
    ui->thirdSourceButton->setToolButtonStyle(qvariant_cast<Qt::ToolButtonStyle>(settings.value("WindowLanguagesStyle", Qt::ToolButtonTextBesideIcon)));
    ui->firstTranslationButton->setToolButtonStyle(qvariant_cast<Qt::ToolButtonStyle>(settings.value("WindowLanguagesStyle", Qt::ToolButtonTextBesideIcon)));
    ui->secondTranslationButton->setToolButtonStyle(qvariant_cast<Qt::ToolButtonStyle>(settings.value("WindowLanguagesStyle", Qt::ToolButtonTextBesideIcon)));
    ui->thirdTranslationButton->setToolButtonStyle(qvariant_cast<Qt::ToolButtonStyle>(settings.value("WindowLanguagesStyle", Qt::ToolButtonTextBesideIcon)));

    // Control buttons style
    ui->playSourceButton->setToolButtonStyle(qvariant_cast<Qt::ToolButtonStyle>(settings.value("WindowControlsStyle", Qt::ToolButtonIconOnly)));
    ui->stopSourceButton->setToolButtonStyle(qvariant_cast<Qt::ToolButtonStyle>(settings.value("WindowControlsStyle", Qt::ToolButtonIconOnly)));
    ui->copySourceButton->setToolButtonStyle(qvariant_cast<Qt::ToolButtonStyle>(settings.value("WindowControlsStyle", Qt::ToolButtonIconOnly)));
    ui->playTranslationButton->setToolButtonStyle(qvariant_cast<Qt::ToolButtonStyle>(settings.value("WindowControlsStyle", Qt::ToolButtonIconOnly)));
    ui->stopTranslationButton->setToolButtonStyle(qvariant_cast<Qt::ToolButtonStyle>(settings.value("WindowControlsStyle", Qt::ToolButtonIconOnly)));
    ui->copyTranslationButton->setToolButtonStyle(qvariant_cast<Qt::ToolButtonStyle>(settings.value("WindowControlsStyle", Qt::ToolButtonIconOnly)));
    ui->copyAllTranslationButton->setToolButtonStyle(qvariant_cast<Qt::ToolButtonStyle>(settings.value("WindowControlsStyle", Qt::ToolButtonIconOnly)));
    ui->settingsButton->setToolButtonStyle(qvariant_cast<Qt::ToolButtonStyle>(settings.value("WindowControlsStyle", Qt::ToolButtonIconOnly)));

    // Shortcuts
    translateSelectionHotkey->setShortcut(settings.value("Hotkeys/TranslateSelection", "Ctrl+Alt+E").toString(), true);
    playSelectionHotkey->setShortcut(settings.value("Hotkeys/PlaySelection", "Ctrl+Alt+S").toString(), true);
    stopSelectionHotkey->setShortcut(settings.value("Hotkeys/StopSelection", "Ctrl+Alt+G").toString(), true);
    playTranslatedSelectionHotkey->setShortcut(settings.value("Hotkeys/PlayTranslatedSelection", "Ctrl+Alt+F").toString(), true);
    showMainWindowHotkey->setShortcut(settings.value("Hotkeys/ShowMainWindow", "Ctrl+Alt+C").toString(), true);
    ui->translateButton->setShortcut(settings.value("Hotkeys/Translate", "Ctrl+Return").toString());
    ui->playSourceButton->setShortcut(settings.value("Hotkeys/PlaySource", "Ctrl+S").toString());
    ui->stopSourceButton->setShortcut(settings.value("Hotkeys/StopSource", "Ctrl+G").toString());
    ui->playTranslationButton->setShortcut(settings.value("Hotkeys/PlayTranslation", "Ctrl+Shift+S").toString());
    ui->stopTranslationButton->setShortcut(settings.value("Hotkeys/StopTranslation", "Ctrl+Shift+G").toString());
    ui->copyTranslationButton->setShortcut(settings.value("Hotkeys/CopyTranslation", "Ctrl+Shift+C").toString());
    closeWindowsShortcut->setKey(settings.value("Hotkeys/CloseWindow", "Ctrl+Q").toString());
}

QList<QAction *> MainWindow::languagesList()
{
    // Load all languages and codes from QOnlineTranslator
    QList<QAction *> languagesList;
    for (auto i=1; i<onlineTranslator->languages().size(); i++) {
        QAction *action = new QAction(onlineTranslator->languages().at(i));
        action->setToolTip(onlineTranslator->codes().at(i));
        action->setIcon(QIcon(":/icons/flags/" + action->toolTip() + ".svg"));
        languagesList.append(action);
    }

    // Sort alphabetically for easy access
    std::sort(languagesList.begin() + 1, languagesList.end(), [](QAction *first, QAction *second) {
        return first->text() < second->text();
    } );

    return languagesList;
}

QString MainWindow::selectedText()
{
    QString selectedText;
#if defined(Q_OS_LINUX)
    selectedText = QApplication::clipboard()->text(QClipboard::Selection);
#elif defined(Q_OS_WIN) // Send Ctrl + C to get selected text
    // Save original clipboard data
    QVariant originalClipboard;
    if (QApplication::clipboard()->mimeData()->hasImage())
        originalClipboard = QApplication::clipboard()->image();
    else
        originalClipboard = QApplication::clipboard()->text();

    // Wait until the hot key is pressed
    while (GetAsyncKeyState(translateSelectedHotkey->currentNativeShortcut().key) || GetAsyncKeyState(VK_CONTROL)
           || GetAsyncKeyState(VK_MENU) || GetAsyncKeyState(VK_SHIFT))
        Sleep(50);

    // Generate key sequence
    INPUT copyText[4];

    // Set the press of the "Ctrl" key
    copyText[0].ki.wVk = VK_CONTROL;
    copyText[0].ki.dwFlags = 0; // 0 for key press
    copyText[0].type = INPUT_KEYBOARD;

    // Set the press of the "C" key
    copyText[1].ki.wVk = 'C';
    copyText[1].ki.dwFlags = 0;
    copyText[1].type = INPUT_KEYBOARD;

    // Set the release of the "C" key
    copyText[2].ki.wVk = 'C';
    copyText[2].ki.dwFlags = KEYEVENTF_KEYUP;
    copyText[2].type = INPUT_KEYBOARD;

    // Set the release of the "Ctrl" key
    copyText[3].ki.wVk = VK_CONTROL;
    copyText[3].ki.dwFlags = KEYEVENTF_KEYUP;
    copyText[3].type = INPUT_KEYBOARD;

    // Send key sequence to system
    SendInput(4, copyText, sizeof(INPUT));

    // Wait for the clipboard to change
    QEventLoop loop;
    QTimer timer; // Add a timer for the case where the text is not selected
    loop.connect(QApplication::clipboard(), &QClipboard::changed, &loop, &QEventLoop::quit);
    loop.connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(1000);
    loop.exec();

    // Translate the text from the clipboard if the selected text was not copied
    if (timer.isActive())
        return QApplication::clipboard()->text();
    else
        timer.stop();

    // Get clipboard data
    selectedText = QApplication::clipboard()->text();

    // Return original clipboard
    if (originalClipboard.type() == QVariant::Image)
        QApplication::clipboard()->setImage(originalClipboard.value<QImage>());
    else
        QApplication::clipboard()->setText(originalClipboard.toString());
#endif
    return selectedText;
}

void MainWindow::loadLanguageButtons(QButtonGroup *group, const QString &settingsName)
{
    // Load buttons text and tooltip
    QSettings settings;
    for (auto i=1; i < 4; i++) {
        QString languageCode = settings.value("Buttons/" + settingsName + "Button" + QString::number(i), "").toString();

        // Check if the code is set
        if (languageCode != "") {
            group->button(i)->setToolTip(languageCode);
            group->button(i)->setIcon(QIcon(":/icons/flags/" + languageCode + ".svg"));
            group->button(i)->setText(onlineTranslator->codeToLanguage(languageCode));
            group->button(i)->setVisible(true);
        }
        else
            group->button(i)->setVisible(false);

        // Load checked button
        group->button(settings.value("Buttons/Checked" + settingsName + "Button", 0).toInt())->setChecked(true);
    }
}

void MainWindow::insertLanguage(QButtonGroup *group, const QString &settingsName, const QString &languageCode)
{
    // Exit the function if the current language already has a button
    for (auto i = 0; i < 4; i++) {
        if (languageCode == group->button(i)->toolTip()) {
            group->button(i)->setChecked(true);
            return;
        }
    }

    // Shift buttons (2 -> 3, 1 -> 2)
    QSettings settings;
    for (auto i = 3; i > 1; i--) {
        // Skip iteration, if previous button is not specified
        if (group->button(i-1)->toolTip() == "")
            continue;

        // Set values
        group->button(i)->setText(group->button(i-1)->text());
        group->button(i)->setToolTip(group->button(i-1)->toolTip());
        group->button(i)->setIcon(group->button(i-1)->icon());
        group->button(i)->setVisible(true);

        // Send signal
        if (group == sourceButtonGroup)
            emit sourceButtonChanged(group->button(i), i);
        else
            emit translationButtonChanged(group->button(i), i);

        // Save settings
        settings.setValue("Buttons/" + settingsName + "Button" + QString::number(i), group->button(i)->toolTip());
    }

    // Shift checked button in the settings
    if (group->checkedId() != 0 && group->checkedId() != 3)
        settings.setValue("Buttons/Checked" + settingsName + "Button", settings.value("Buttons/Checked" + settingsName + "Button", 0).toInt() + 1);

    // Insert new language to first button
    group->button(1)->setText(onlineTranslator->codeToLanguage(languageCode));
    group->button(1)->setToolTip(languageCode);
    group->button(1)->setIcon(QIcon(":/icons/flags/" + languageCode + ".svg"));
    group->button(1)->setVisible(true);

    if (group->button(1)->isChecked())
        emit group->buttonToggled(group->button(1), true);
    else
        group->button(1)->setChecked(true);

    // Send signal
    if (group == sourceButtonGroup)
        emit sourceButtonChanged(group->button(1), 1);
    else
        emit translationButtonChanged(group->button(1), 1);

    // Save first button settings
    settings.setValue("Buttons/" + settingsName + "Button" + QString::number(1), group->button(1)->toolTip());
}

void MainWindow::checkSourceButton(const int &id, const bool &checked)
{
    if (checked)
        sourceButtonGroup->button(id)->setChecked(true);
}

void MainWindow::checkTranslationButton(const int &id, const bool &checked)
{
    if (checked)
        translationButtonGroup->button(id)->setChecked(true);
}
