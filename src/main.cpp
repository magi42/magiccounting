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

    QString accountingFolder;
    const QStringList arguments = app.arguments();
    if (arguments.size() > 1) {
        accountingFolder = QDir(arguments.at(1)).absolutePath();
    } else {
        accountingFolder = AppConfig::accountingFolder();
    }
    if (accountingFolder.isEmpty()) {
        accountingFolder = DataStore::defaultDataFolder();
    }

    MainWindow window(accountingFolder);
    window.show();

    return app.exec();
}
