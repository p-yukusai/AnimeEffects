#ifndef NETWORKUTIL_H
#define NETWORKUTIL_H
#include "gui/menu/menu_ProgressReporter.h"
#include <QByteArray>
#include <QDebug>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcess>
#include <QString>

namespace util {
class NetworkUtil {
public:
    NetworkUtil();
    QByteArray getByteArray(QString aURL);
    QJsonDocument getJsonFrom(QString aURL);
    bool libExists(QString aLib, QString versionType = "-V");
    QList<QString> libArgs(QList<QString> anArgument, QString aType);
    QFileInfo downloadGithubFile(QString aURL, QString aFile, int aID, QWidget* aParent);
    void checkForUpdate(QString url, NetworkUtil networking, QWidget* aParent, bool showWithoutUpdate = true);

    // This isn't necessarily a network utility, but since I'm using them here anyway I might as well
    // make them accessible.
    QString os() {
#if defined(Q_OS_WIN)
        return "win";
#elif defined(Q_OS_LINUX)
        return "linux";
#elif defined(Q_OS_MACOS)
        return "mac";
#endif
    }

    QString arch() {
#if Q_PROCESSOR_WORDSIZE == 4
        return "x86";
#elif Q_PROCESSOR_WORDSIZE == 8
        return "x64";
#endif
    }
    util::IProgressReporter* mProgressReporter;

private:
    /*
    ユーザー、おそらく : うわー、このコード、ゴミだ。
    私 : あなたにわからないでしょうね (っ °Д °;)っ
    */
};
} // namespace util

#endif // NETWORKUTIL_H
