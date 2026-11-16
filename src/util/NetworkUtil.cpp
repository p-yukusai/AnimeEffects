#include "NetworkUtil.h"
#include "qapplication.h"
#include "qdir.h"
#include "qjsonarray.h"
#include "gui/menu/menu_ProgressReporter.h"
#include "qmessagebox.h"
#include "qpushbutton.h"
#include <QEventLoop>
#include <QJsonObject>
#include <QRegularExpression>
#include <QDesktopServices>
#include <QAbstractButton>
#include <QtConcurrent/QtConcurrent>

namespace util {

NetworkUtil::NetworkUtil() {}
QByteArray NetworkUtil::getByteArray(QString aURL) {
    QProcess mProcess;
    QByteArray response;
    QList<QString> arguments = libArgs({aURL}, "json");
    if (arguments[1] == "failed") {
        return QByteArray::fromStdString("NetworkUtil Error");
    }
    const QString program = arguments[0];
    arguments.pop_front();
    mProcess.start(program, arguments);
    // Waits for a second, todo: make this only apply to start check
    mProcess.waitForFinished();
    if (mProcess.exitCode() == 0) {
        response = mProcess.readAll();
    } else {
        qDebug("-----------");
        qDebug() << "program : " << program;
        qDebug() << "args : " << arguments;
        qDebug() << "std err : " << mProcess.readAllStandardError();
        qDebug() << "std out : " << mProcess.readAllStandardOutput();
        qDebug("-----------");
        return QByteArray::fromStdString("NetworkUtil Error");
    }
    // qDebug() << response.data();
    return response;
}

QJsonDocument NetworkUtil::getJsonFrom(const QString& aURL) {
    QByteArray data = getByteArray(aURL);
    if (data == "NetworkUtil Error") {
        return {};
    }
    return QJsonDocument::fromJson(data.data());
}

bool NetworkUtil::libExists(const QString &aLib, const QString &versionType, const QString &versionStr) {
    QProcess process;
    process.start(aLib, {versionType}, QProcess::ReadWrite);
    process.waitForFinished();
    if (process.exitStatus() != 0) {
        return false;
    }
    // Checks for part of the version string existing
    return QString(process.readAll().data()).contains(versionStr);
}
bool NetworkUtil::libExists(const char* libName) {
    QProcess process;
    #ifdef Q_OS_WIN
        process.start("where.exe", {libName}, QProcess::ReadWrite);
    #elif defined(Q_OS_MAC)
        process.start("which", {libName}, QProcess::ReadWrite);
    #else
        process.start("which", {libName}, QProcess::ReadWrite);
    #endif
    process.waitForFinished();
    if (process.exitStatus() != 0) {
        return false;
    }
    return true;
}

// The "json" type accepts only lists of size 1.
// The "download" type requires index 0 to be the url and index 1 to be the path.
QList<QString> NetworkUtil::libArgs(QList<QString> aArgument, QString aType) {
    QString lib;
    QList<QString> args;
    if (os() == "win" || os() == "mac") {
        lib = "curl";
    } else if (os() == "linux") {
        lib = "wget";
    }
    if (!libExists(lib, "-V", lib == "curl"? lib: "Wget")) {
        qDebug() << lib << " couldn't be found.";
        auto failedLib = lib;
        if (lib == "wget") {
            lib = "curl";
        } else {
            lib = "wget";
        }
        if (!libExists(lib, "-V", lib == "curl"? lib: "Wget")) {
            return QList<QString>({failedLib, "failed"});
        }
    }
    // Arguments to get a json string through standard output
    if (aType == "json") {
        if (lib == "curl") {
            args << "curl";
            args << aArgument[0] << "-H 'Accept: application/json'";
        } else if (lib == "wget") {
            args << "wget";
            args << "-qO-" << aArgument[0] << "--header='Content-Type: application/json'";
        }
    }
    // Arguments to download a file to a certain path. We use "-L" with curl to avoid issues with redirections.
    if (aType == "download") {
        if (lib == "curl") {
            args << "curl";
            args << "-L" << aArgument[0] << "--output" << aArgument[1];
        } else if (lib == "wget") {
            args << "wget";
            args << aArgument[0] << "-O" << aArgument[1];
        }
    }
    return args;
}

// Function written in consideration of the "releases/{version}" api, e.g.,
// https://api.github.com/repos/author/project/releases/latest
QFileInfo NetworkUtil::downloadGithubFile(const QString& aURL, const QString& aFile, int aID, QWidget* aParent) {
    const QJsonObject jsonResponse = getJsonFrom(aURL).object();
    QString downloadURL = "null";

    // Get name of assets and then check against file and ID.
    for (auto assets : jsonResponse.value("assets").toArray()) {
        qDebug() << assets.toObject().value("name").toString();
        if (assets.toObject().value("name").toString() == aFile) {
            QSettings settings;
            auto checkResID = settings.value("res_id_check");
            const bool checkID = checkResID.isValid()? checkResID.toBool(): true;
            // If ID field, check it.
            if (aID != 0 && checkID) {
                const int urlID = assets.toObject().value("id").toInt();
                if (aID == urlID) {
                    downloadURL = assets.toObject().value("browser_download_url").toString();
                } else {
                    QMessageBox msgbox;
                    msgbox.setIcon(QMessageBox::Warning);
                    msgbox.setWindowTitle("Invalid file ID");
                    msgbox.setText("Invalid ID for selected file, this probably indicates that you need to update AnimeEffects to its latest version.\nExpected File ID:" +
                                 QString::number(aID) + "- Parsed File ID:" + QString::number(urlID));
                    msgbox.exec();
                    return {};
                }
            }
            // Otherwise just get the asset url
            else {
                downloadURL = assets.toObject().value("browser_download_url").toString();
            }
        }
    }
    if (downloadURL == "null") {
        QMessageBox msgbox;
        msgbox.setIcon(QMessageBox::Warning);
        msgbox.setWindowTitle("Download error");
        msgbox.setText("The asset requested from \"" + aURL + "\" returned null, please check your internet connection and try again.");
        msgbox.exec();
        return {};
    }
    QString downloadPath = QDir::tempPath() + "/" + aFile;
    QList<QString> args = libArgs({downloadURL, downloadPath}, "download");
    // qDebug() << args;
    QScopedPointer<QProcess> mProcess;
    mProcess.reset(new QProcess(nullptr));
    mProcess->setProcessChannelMode(QProcess::MergedChannels);
    const auto processData = mProcess.data();
    const QString program = args[0];
    gui::menu::ProgressReporter progress(true, aParent);
    gui::menu::ProgressReporter* progressptr = &progress;
    progress.setProgress(0);
    args.pop_front();
    mProcess->connect(processData, &QProcess::readyRead, [=]() mutable {
        const QString data = processData->readAll().data();
        if (program == "curl") {
            qDebug() << "Download percentage : " << data.mid(1, 3);
            progressptr->setProgress(data.mid(1, 3).toInt());
        } else if (program == "wget") {
            // Finds a number with a percentage sign.
            QString percentage = QRegularExpression("(\\d{1,3})%").match(data).captured(0);
            percentage.remove("%");
            if (percentage.toInt() < 101 && percentage.toInt() != 0) {
                qDebug() << "Download percentage : " << percentage.toInt();
                progressptr->setProgress(percentage.toInt());
            }
        }
    });
    progress.setSection(QCoreApplication::translate("NetworkUtil", "Currently downloading:") + "\n\n" + aFile);
    progress.setMaximum(100);
    mProcess->start(program, args);
    if (mProcess->waitForReadyRead(15000)) {
        // Max time for this is five minutes.
        mProcess->waitForFinished(60000 * 5);
    }
    if (os() != "win"){
        // Make executable
        QScopedPointer<QProcess> chmodProc;
        chmodProc.reset(new QProcess(nullptr));
        chmodProc->start("chmod", QStringList({"u+x", downloadPath}));
        chmodProc->waitForFinished();
    }
    if (QFile(downloadPath).exists()) {
        return QFileInfo(QFile(downloadPath));
    }
    QMessageBox msgbox;
    msgbox.setIcon(QMessageBox::Warning);
    msgbox.setWindowTitle("Download error");
    msgbox.setText("The download for the asset at \"" + downloadURL + "\" was aborted as it either timed out or the HTTP utility requested (curl/wget) could not be launched, please download the asset manually.");
    msgbox.exec();
    return {};
}

void NetworkUtil::checkForUpdate(const QString& url, QWidget* aParent, const bool showWithoutUpdate) {
    qDebug("--------");
    qInfo() << "Checking for updates on : " << url;

    const QFuture<QJsonDocument> jsonPromise = QtConcurrent::run(getJsonFrom, url);
    const QString currentVersion = QString::number(AE_MAJOR_VERSION) + "." + QString::number(AE_MINOR_VERSION) + "." +
        QString::number(AE_MICRO_VERSION);
    // qDebug()<< "Response : " << jsonPromise.result().toJson().data();
    // qDebug()<< "Current version: " << currentVersion << "\n" << "Latest stable: " <<
    // jsonPromise.result()[0]["name"].toString().replace("v", "");
    const QJsonDocument jsonResponse = jsonPromise.result();
    const QString latestVersion = !jsonResponse.isEmpty() ? jsonResponse[0]["name"].toString().replace("v", "") : "null";

    if (latestVersion != "") {
        auto* updateBox = new QMessageBox(aParent);

        bool onLatest = false;
        bool onPreview = false;
        bool failed = false;

        // Failed
        if (latestVersion == "null") {
            qDebug() << "Failed to get version";
            updateBox->setWindowTitle(QApplication::translate("NetworkCheckForUpdate", "Failed"));
            updateBox->setText(QApplication::translate(
                "NetworkCheckForUpdate",
                "<center>Unable to get latest version. <br>Please check your internet "
                "connection and if you have curl or wget installed.</center>"
            ));
            failed = true;
        }
        // Latest version
        if (latestVersion == currentVersion) {
            qDebug() << "On latest version :" << latestVersion;
            updateBox->setWindowTitle(QApplication::translate("NetworkCheckForUpdate", "On latest"));
            updateBox->setText(
                QApplication::translate(
                    "NetworkCheckForUpdate",
                    "<center>You already have the latest stable release available. <br>Version: "
                ) +
                currentVersion + "</center>"
            );
            onLatest = true;
        }
        // Preview version
        else if (latestVersion < currentVersion) {
            qDebug() << "On preview version :" << currentVersion;

            updateBox->setWindowTitle(QApplication::translate("NetworkCheckForUpdate", "On preview"));
            updateBox->setText(
                QApplication::translate(
                    "NetworkCheckForUpdate",
                    "<center>Your current version is higher than the latest stable release. "
                    "<br>Version: "
                ) +
                currentVersion + "</center>"
            );
            onPreview = true;
        }
        // Old version
        else {
            qDebug() << "On version :" << currentVersion;
            updateBox->setWindowTitle(QApplication::translate("NetworkCheckForUpdate", "New release available"));
            updateBox->setText(
                QApplication::translate(
                    "NetworkCheckForUpdate", "<center>A new stable release is available, version: "
                ) +
                latestVersion +
                QApplication::translate(
                    "NetworkCheckForUpdate", ".<br>Do you wish to download it or to go to the GitHub page?</center>"
                )
            );
        }

        if (onLatest || onPreview || failed) {
            updateBox->setStandardButtons(QMessageBox::Ok);
            updateBox->setDefaultButton(QMessageBox::Ok);
        } else {
            // Boilerplate go brr
            QPushButton download(QApplication::translate("NetworkCheckForUpdate", "Download"));
            QAbstractButton* downloadButton = &download;
            QPushButton goTo(QApplication::translate("NetworkCheckForUpdate", "Go to page"));
            QAbstractButton* gotoButton = &goTo;
            updateBox->addButton(downloadButton, QMessageBox::AcceptRole);
            updateBox->addButton(gotoButton, QMessageBox::AcceptRole);
            updateBox->addButton(QMessageBox::Cancel);
            updateBox->exec();

            if (updateBox->clickedButton() == gotoButton) {
                QDesktopServices::openUrl(QUrl("https://github.com/AnimeEffectsDevs/AnimeEffects/releases/latest"));
            } else if (updateBox->clickedButton() == downloadButton) {
                const QString os = NetworkUtil::os();
                QString file;
                if (os == "win") {
                    file = "AnimeEffects-Windows.zip";
                } else if (os == "linux") {
                    file = "AnimeEffects-Linux.zip";
                } else if (os == "mac") {
                    if (QSysInfo::buildCpuArchitecture() == "arm64") {
                        file = "AnimeEffects-MacOS-ARM.zip";
                    }
                    else {
                        file = "AnimeEffects-MacOS.zip";
                    }
                }
                const QFileInfo aeUpdate = downloadGithubFile(
                    "https://api.github.com/repos/AnimeEffectsDevs/AnimeEffects/releases/latest", file, 0, aParent
                );
                QDesktopServices::openUrl(QUrl::fromLocalFile(aeUpdate.absoluteFilePath()));
            }
            qDebug("--------");
            return;
        }
        if (showWithoutUpdate) {
            updateBox->exec();
        }
        qDebug("--------");
    }
}

} // namespace util
