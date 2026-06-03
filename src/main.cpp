#include "MainWindow.h"

#include "AppConfig.h"
#include "DataStore.h"
#include "LanguageManager.h"

#include <QApplication>
#include <QDir>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName("MagicCounting");
    QApplication::setApplicationName("MagicCounting");
    LanguageManager::install(&app, AppConfig::languageCode());

    QString accountingFile;
    const QStringList arguments = app.arguments();
    if (arguments.size() > 1) {
        accountingFile = QDir(arguments.at(1)).absolutePath();
    } else {
        accountingFile = AppConfig::accountingFile();
    }

    MainWindow window(accountingFile);
    window.show();

    return app.exec();
}
