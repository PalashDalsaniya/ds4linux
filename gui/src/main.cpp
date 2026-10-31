// ds4linux GUI — Application entry point

#include "ds4linux/constants.h"
#include "ds4linux/main_window.h"

#include <QApplication>
#include <QCommandLineParser>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("DS4Linux");
    app.setApplicationVersion(QString::fromLatin1(ds4linux::kVersion.data(),
                                                  static_cast<qsizetype>(ds4linux::kVersion.size())));
    app.setOrganizationName("ds4linux");
    app.setDesktopFileName("ds4linux");

    QCommandLineParser parser;
    parser.setApplicationDescription("Configure and test controllers handled by ds4linux-daemon");
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption socketOpt(
        "socket", "Daemon control socket.", "path",
        QString::fromLatin1(ds4linux::kSocketPath.data(),
                            static_cast<qsizetype>(ds4linux::kSocketPath.size())));
    parser.addOption(socketOpt);
    parser.process(app);

    // Prefer dark palette for a modern look
    QPalette dark;
    dark.setColor(QPalette::Window,          QColor(42, 42, 42));
    dark.setColor(QPalette::WindowText,      Qt::white);
    dark.setColor(QPalette::Base,            QColor(30, 30, 30));
    dark.setColor(QPalette::AlternateBase,   QColor(50, 50, 50));
    dark.setColor(QPalette::ToolTipBase,     Qt::white);
    dark.setColor(QPalette::ToolTipText,     Qt::black);
    dark.setColor(QPalette::Text,            Qt::white);
    dark.setColor(QPalette::Button,          QColor(55, 55, 55));
    dark.setColor(QPalette::ButtonText,      Qt::white);
    dark.setColor(QPalette::BrightText,      Qt::red);
    dark.setColor(QPalette::Highlight,       QColor(0, 120, 215));
    dark.setColor(QPalette::HighlightedText, Qt::white);
    dark.setColor(QPalette::Disabled, QPalette::Text,       QColor(130, 130, 130));
    dark.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(130, 130, 130));
    app.setPalette(dark);

    ds4linux::gui::MainWindow w(parser.value(socketOpt));
    w.show();

    return app.exec();
}
