/****************************************
 *
 *   INSERT-PROJECT-NAME-HERE - INSERT-GENERIC-NAME-HERE
 *   Copyright (C) 2020 Victor Tran
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * *************************************/
#include "filecolumn.h"
#include "ui_filecolumn.h"

#include <QDir>
#include <QUrl>
#include <QMenu>
#include <QInputDialog>
#include <QClipboard>
#include <resourcemanager.h>
#include <tlogger.h>
#include "filemodel.h"
#include <tjobmanager.h>
#include <ttoast.h>
#include <tpopover.h>
#include <QMessageBox>
#include <QDesktopServices>
#include <MimeAssociations/mimeassociationmanager.h>
#include "hiddenfilesproxymodel.h"
#include "jobs/filetransferjob.h"
#include "popovers/deletepermanentlypopover.h"

struct FileColumnPrivate {
    QUrl url;
    QUrl selectedUrl;
    FileModel* model = nullptr;
    HiddenFilesProxyModel* proxy;
};

FileColumn::FileColumn(QUrl url, QWidget* parent) :
    QWidget(parent),
    ui(new Ui::FileColumn) {
    ui->setupUi(this);

    d = new FileColumnPrivate();
    d->url = url;

    d->proxy = new HiddenFilesProxyModel();
    ui->folderView->setModel(d->proxy);

    ui->folderView->setItemDelegate(new FileDelegate());

    reload();
}

FileColumn::~FileColumn() {
    delete ui;
    delete d;
}

void FileColumn::setUrl(QUrl url) {
    bool pathSame = false;
    if (d->url.path() == url.path()) pathSame = true;
    if ((d->url.path().isEmpty() || d->url.path() == "/") && (url.path().isEmpty() || url.path() == "/")) pathSame = true;
    if (d->url.scheme() == url.scheme() && pathSame) return;
    d->url = url;

    emit urlChanged();
    reload();
}

void FileColumn::setSelected(QUrl url) {
    d->selectedUrl = url;
    ensureUrlSelected();
}

QString FileColumn::columnTitle() {
    if (d->url.path() == "/") {
        if (d->url.scheme() == "file") return tr("Root");
        if (d->url.scheme() == "trash") return tr("Trash");
        return d->url.scheme();
    } else {
        if (d->url.scheme() == "file") {
            if (QDir(d->url.path()) == QDir::home()) return tr("Home");
        }
        return QFileInfo(QFileInfo(d->url.path()).canonicalFilePath()).fileName();
    }
}

void FileColumn::cut() {
    QStringList clipboardData;
    clipboardData.append("x-special/nautilus-clipboard");
    clipboardData.append("cut");

    QModelIndexList sel = ui->folderView->selectionModel()->selectedIndexes();
    QList<QUrl> urls;
    for (QModelIndex index : sel) {
        urls.append(index.data(FileModel::UrlRole).toUrl());
    }
    clipboardData.append(QUrl::toStringList(urls));

    QMimeData* mimeData = new QMimeData();
    mimeData->setData("text/plain", clipboardData.join("\n").toUtf8());
    mimeData->setData("COMPOUND_TEXT", clipboardData.join("\n").toUtf8());
    mimeData->setData("text/plain;charset=utf-8", clipboardData.join("\n").toUtf8());
    mimeData->setData("application/x-thesuite-thefile-clipboardoperation", "cut");
    mimeData->setUrls(urls);
    QApplication::clipboard()->setMimeData(mimeData);
}

void FileColumn::copy() {
    QStringList clipboardData;
    clipboardData.append("x-special/nautilus-clipboard");
    clipboardData.append("copy");

    QModelIndexList sel = ui->folderView->selectionModel()->selectedIndexes();
    QList<QUrl> urls;
    for (QModelIndex index : sel) {
        urls.append(index.data(FileModel::UrlRole).toUrl());
    }
    clipboardData.append(QUrl::toStringList(urls));

    QMimeData* mimeData = new QMimeData();
    mimeData->setData("text/plain", clipboardData.join("\n").toUtf8());
    mimeData->setData("COMPOUND_TEXT", clipboardData.join("\n").toUtf8());
    mimeData->setData("text/plain;charset=utf-8", clipboardData.join("\n").toUtf8());
    mimeData->setData("application/x-thesuite-thefile-clipboardoperation", "copy");
    mimeData->setUrls(urls);
    QApplication::clipboard()->setMimeData(mimeData);
}

void FileColumn::paste() {
    const QMimeData* data = QApplication::clipboard()->mimeData();
    if (data->hasUrls()) {
        FileTransferJob::TransferType transferType = FileTransferJob::Copy;
        if (data->hasFormat("application/x-thesuite-thefile-clipboardoperation")) {
            QString type = data->data("application/x-thesuite-thefile-clipboardoperation");
            if (type == "cut") transferType = FileTransferJob::Move;
        }

        //Prepare a copy job
        FileTransferJob* job = new FileTransferJob(transferType, data->urls(), d->url, this->window());
        tJobManager::trackJobDelayed(job);

        //Clear the clipboard after a cut operation
        if (transferType == FileTransferJob::Move) QApplication::clipboard()->clear();
    }
}

void FileColumn::newFolder() {
    bool ok;
    QString folderName = QInputDialog::getText(this, tr("New Folder"), tr("Folder name"), QLineEdit::Normal, tr("New Folder"), &ok);
    if (ok) {
        ResourceManager::mkpath(d->url.resolved(folderName));
    }
}

void FileColumn::moveToTrash() {
    QModelIndexList sel = ui->folderView->selectionModel()->selectedIndexes();
    for (QModelIndex index : sel) {
        ResourceManager::trash(index.data(FileModel::UrlRole).toUrl());
    }

    tToast* toast = new tToast(this->window());
    toast->setTitle(tr("Trash"));
    toast->setText(tr("Moved %n items to the trash", nullptr, sel.count()));
    connect(toast, &tToast::dismissed, toast, &tToast::deleteLater);
    toast->show(this->window());
}

void FileColumn::deleteFile() {
    QList<QUrl> urlsToDelete;

    QModelIndexList sel = ui->folderView->selectionModel()->selectedIndexes();
    for (QModelIndex index : sel) {
        urlsToDelete.append(index.data(FileModel::UrlRole).toUrl());
    }

    DeletePermanentlyPopover* jp = new DeletePermanentlyPopover(urlsToDelete);
    tPopover* popover = new tPopover(jp);
    popover->setPopoverWidth(SC_DPI(-200));
    popover->setPopoverSide(tPopover::Bottom);
    connect(jp, &DeletePermanentlyPopover::done, popover, &tPopover::dismiss);
    connect(popover, &tPopover::dismissed, popover, &tPopover::deleteLater);
    connect(popover, &tPopover::dismissed, jp, &DeletePermanentlyPopover::deleteLater);
    popover->show(this->window());
}

void FileColumn::rename() {
    QModelIndexList sel = ui->folderView->selectionModel()->selectedIndexes();
    if (sel.count() == 1) {
        QModelIndex item = sel.first();

        bool ok;
        QString newName = QInputDialog::getText(this, tr("Rename"), tr("Enter a new name"), QLineEdit::Normal, item.data(Qt::DisplayRole).toString(), &ok);
        if (ok) {
            QUrl oldUrl = item.data(FileModel::UrlRole).toUrl();
            QUrl newUrl = oldUrl.resolved(QUrl("./" + newName));
            ResourceManager::move(oldUrl, newUrl);
        }
    }
}

void FileColumn::reload() {
    d->model = new FileModel(d->url);
    connect(d->model, &FileModel::modelAboutToBeReset, this, [ = ] {
        ui->folderView->selectionModel()->blockSignals(true);
    });
    connect(d->model, &FileModel::modelReset, this, &FileColumn::updateItems);
    connect(d->model, &FileModel::modelReset, this, &FileColumn::ensureUrlSelected);
    connect(d->model, &FileModel::modelReset, this, [ = ] {
        ui->folderView->selectionModel()->blockSignals(false);
    });
    updateItems();

    d->proxy->setSourceModel(d->model);
    ui->folderNameLabel->setText(columnTitle());
    connect(ui->folderView->selectionModel(), &QItemSelectionModel::currentChanged, this, [ = ] {
        if (ui->folderView->currentIndex().isValid()) {
            emit navigate(ui->folderView->currentIndex().data(FileModel::UrlRole).toUrl());
        }
    });
}

void FileColumn::updateItems() {
    QString error = d->model->currentError();
    if (error.isEmpty()) {
        ui->stackedWidget->setCurrentWidget(ui->folderPage);
    } else {
        QIcon icon;
        if (error == QStringLiteral("error.no-items")) {
            ui->folderErrorTitle->setText(tr("No items here!"));
            ui->folderErrorText->setText(tr("This folder is empty."));
            icon = QIcon(":/icons/folder-empty.svg");
        } else if (error == QStringLiteral("error.not-found")) {
            ui->folderErrorTitle->setText(tr("Not Found"));
            ui->folderErrorText->setText(tr("This folder doesn't exist."));
            icon = QIcon(":/icons/folder-unavailable.svg");
        } else if (error == QStringLiteral("error.permission-denied")) {
            ui->folderErrorTitle->setText(tr("Permission Denied"));
            ui->folderErrorText->setText(tr("Looks like you don't have permission to view this folder."));
            icon = QIcon(":/icons/folder-unavailable.svg");
        } else {
            ui->folderErrorTitle->setText(tr("Can't view this folder"));
            ui->folderErrorText->setText(tr("We can't show you the contents of this folder."));
            icon = QIcon(":/icons/folder-unavailable.svg");
        }
        QImage iconImage = icon.pixmap(SC_DPI_T(QSize(128, 128), QSize)).toImage();
        theLibsGlobal::tintImage(iconImage, this->palette().color(QPalette::WindowText));
        ui->folderErrorIcon->setPixmap(QPixmap::fromImage(iconImage));
        ui->stackedWidget->setCurrentWidget(ui->folderErrorPage);
    }
}

void FileColumn::addFolderMenuItems(QMenu* menu) {
    if (d->model) {
        if (d->url.scheme() == "trash") {

        } else {
            if (d->model->currentError().isEmpty() || d->model->currentError() == QStringLiteral("error.no-items")) {
                menu->addSection(tr("For this folder"));
                menu->addAction(QIcon::fromTheme("folder-new"), tr("New Folder"), this, &FileColumn::newFolder);
                menu->addAction(QIcon::fromTheme("edit-paste"), tr("Paste"), this, &FileColumn::paste);
            }
        }
    }
}

void FileColumn::ensureUrlSelected() {
    if (d->selectedUrl.isValid()) {
        for (int i = 0; i < ui->folderView->model()->rowCount(); i++) {
            QModelIndex index = ui->folderView->model()->index(i, 0);
            QUrl checkUrl = index.data(FileModel::UrlRole).toUrl();
            if (d->selectedUrl.scheme() == checkUrl.scheme() && d->selectedUrl.host() == checkUrl.host() && QDir::cleanPath(d->selectedUrl.path()) == QDir::cleanPath(checkUrl.path()) && d->selectedUrl.query() == checkUrl.query()) {
                ui->folderView->selectionModel()->select(index, QItemSelectionModel::ClearAndSelect);
            }
        }
    } else {
        ui->folderView->clearSelection();
    }
}

void FileColumn::on_folderView_customContextMenuRequested(const QPoint& pos) {
    QMenu* menu = new QMenu(this);

    QModelIndexList sel = ui->folderView->selectionModel()->selectedIndexes();
    if (sel.count() > 0) {
        if (sel.count() == 1) {
            menu->addSection(tr("For %1").arg(QLocale().quoteString(menu->fontMetrics().elidedText(sel.first().data(Qt::DisplayRole).toString(), Qt::ElideRight, SC_DPI(300)))));
            QUrl url = sel.first().data(FileModel::UrlRole).toUrl();
            if (ResourceManager::isFile(url)) {
                menu->addAction(QIcon::fromTheme("document-open"), tr("Open"), this, [ = ] {
                    QDesktopServices::openUrl(url);
                });

                QMimeDatabase db;
                QMimeType mimeType = db.mimeTypeForFile(url.toLocalFile());

                QMenu* otherApps = new QMenu();
                otherApps->setTitle(tr("Open With..."));

                QList<ApplicationPointer> apps = MimeAssociationManager::applicationsForMimeType(mimeType.name());
                for (ApplicationPointer app : apps) {
                    otherApps->addAction(QIcon::fromTheme(app->getProperty("Icon").toString()), app->getProperty("Name").toString(), this, [ = ] {

                        QMap<QString, QString> launchArgs;
                        launchArgs.insert("%u", url.toString());
                        launchArgs.insert("%U", url.toString());
                        launchArgs.insert("%f", url.toLocalFile());
                        launchArgs.insert("%F", url.toLocalFile());
                        app->launch(launchArgs);
                    });
                }

                otherApps->addSeparator();
                otherApps->addAction(tr("Another app..."), this, [ = ] {
                    QProcess::startDetached("xdg-open", {"--force-prompt", url.toString()});
                });

                menu->addMenu(otherApps);
                menu->addSeparator();
            }
        } else if (sel.count() > 1) {
            menu->addSection(tr("For %n items", nullptr, sel.count()));
        }

        menu->addAction(QIcon::fromTheme("edit-cut"), tr("Cut"), this, &FileColumn::cut);
        menu->addAction(QIcon::fromTheme("edit-copy"), tr("Copy"), this, &FileColumn::copy);
        if (d->url.scheme() == "trash") {
            menu->addAction(QIcon::fromTheme("trash-restore"), tr("Put Back"), this, [ = ] {
                for (QModelIndex index : sel) {
                    QUrl url = index.data(FileModel::UrlRole).toUrl();
                    QVariant restorePath = ResourceManager::special("trash", "restorePath", {{"url", url}});
                    if (restorePath.isValid()) {
                        QUrl dest = restorePath.toUrl();

                        //Prepare a move job
                        FileTransferJob* job = new FileTransferJob(FileTransferJob::Move, {url}, dest, this->window());
                        tJobManager::trackJobDelayed(job);
                    }
                }
            });
            menu->addAction(QIcon::fromTheme("edit-delete"), tr("Delete Permanently"), this, &FileColumn::deleteFile);
        } else {
            menu->addAction(QIcon::fromTheme("edit-delete"), tr("Move to Trash"), this, [ = ] {
                if (qApp->queryKeyboardModifiers() & Qt::ShiftModifier) {
                    deleteFile();
                } else {
                    moveToTrash();
                }
            });
            menu->addAction(QIcon::fromTheme("edit-rename"), tr("Rename"), this, &FileColumn::rename);
        }
    }

    addFolderMenuItems(menu);

    menu->popup(ui->folderView->mapToGlobal(pos));
    connect(menu, &QMenu::aboutToHide, menu, &QMenu::deleteLater);
}

void FileColumn::on_folderErrorPage_customContextMenuRequested(const QPoint& pos) {
    QMenu* menu = new QMenu(this);
    addFolderMenuItems(menu);
    menu->popup(ui->folderView->mapToGlobal(pos));
    connect(menu, &QMenu::aboutToHide, menu, &QMenu::deleteLater);
}

void FileColumn::on_folderView_doubleClicked(const QModelIndex& index) {
    QUrl url = index.data(FileModel::UrlRole).toUrl();
    if (ResourceManager::isFile(url)) {
        QDesktopServices::openUrl(url);
    }
}
