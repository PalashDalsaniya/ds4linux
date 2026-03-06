// ds4linux GUI — Application entry point

#include "ds4linux/main_window.h"

#include <QApplication>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("DS4Linux");
    app.setApplicationVersion("0.1.0");
    app.setOrganizationName("ds4linux");

    // Prefer dark palette for a modern look
    QPalette dark;
    dark.setColor(QPalette::Window,          QColor(42, 42, 42));
    dark.setColor(QPalette::WindowText,      Qt::white);
    dark.setColor(QPalette::Base,            QColor(30, 30, 30));
    dark.setColor(QPalette::AlternateBase,   QColor(50, 50, 50));
    dark.setColor(QPalette::ToolTipBase,     Qt::white);
    dark.setColor(QPalette::ToolTipText,     Qt::white);
    dark.setColor(QPalette::Text,            Qt::white);
    dark.setColor(QPalette::Button,          QColor(55, 55, 55));
    dark.setColor(QPalette::ButtonText,      Qt::white);
    dark.setColor(QPalette::BrightText,      Qt::red);
    dark.setColor(QPalette::Highlight,       QColor(0, 120, 215));
    dark.setColor(QPalette::HighlightedText, Qt::white);
    app.setPalette(dark);

    ds4linux::gui::MainWindow w;
    w.show();

    return app.exec();
}
