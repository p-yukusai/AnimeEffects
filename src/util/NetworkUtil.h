#ifndef NETWORKUTIL_H
#define NETWORKUTIL_H
#include <QProcess>
#include <QMessageBox>
#include <QJsonDocument>
#include <QFileInfo>
#include <QByteArray>
#include <QString>
#include <QDebug>
#include "gui/menu/menu_ProgressReporter.h"

namespace util {
class NetworkUtil {
public:
    NetworkUtil();
    static QByteArray getByteArray(QString aURL);
    static QJsonDocument getJsonFrom(const QString& aURL);
    static bool libExists(const QString& aLib, QString versionType = "-V");
    static QList<QString> libArgs(QList<QString> anArgument, QString aType);
    static QFileInfo downloadGithubFile(const QString& aURL, const QString& aFile, int aID, QWidget* aParent);
    static void checkForUpdate(const QString& url, NetworkUtil networking, QWidget* aParent, bool showWithoutUpdate = true);

    // This isn't necessarily a network utility, but since I'm using them here anyway I might as well
    // make them accessible.
    static QString os() {
#if defined(Q_OS_WIN)
        return "win";
#elif defined(Q_OS_LINUX)
        return "linux";
#elif defined(Q_OS_MACOS)
        return "mac";
#endif
    }

    static QString arch() {
#if Q_PROCESSOR_WORDSIZE == 4
        return "x86";
#elif Q_PROCESSOR_WORDSIZE == 8
        return "x64";
#endif
    }

private:
    /*
    ユーザー、おそらく : うわー、このコード、ゴミだ。
    私 : あなたにわからないでしょうね (っ °Д °;)っ
    */
};
} // namespace util

#endif // NETWORKUTIL_H
