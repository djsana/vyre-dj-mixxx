#include "skin/legacy/launchimage.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QProgressBar>
#include <QStyleOption>
#include <QVBoxLayout>

#include "moc_launchimage.cpp"

LaunchImage::LaunchImage(QWidget* pParent, const QString& styleSheet)
        : QWidget(pParent) {
    if (styleSheet.isEmpty()) {
        setStyleSheet(
                "LaunchImage { background-color: #090d12; }"
                "QLabel { "
                "image: url(:/images/vyre/vyre-dj-logo.svg);"
                "padding:0;"
                "margin:0;"
                "border:none;"
                "min-width: 240px;"
                "min-height: 58px;"
                "max-width: 240px;"
                "max-height: 58px;"
                "}"
                "QProgressBar {"
                "background-color: #17212c; "
                "border:none;"
                "border-radius: 2px;"
                "min-width: 240px;"
                "min-height: 3px;"
                "max-width: 240px;"
                "max-height: 3px;"
                "}"
                "QProgressBar::chunk { background-color: #42d7c5; }");
    } else {
        setStyleSheet(styleSheet);
    }

    QLabel *label = new QLabel(this);

    m_pProgressBar = new QProgressBar(this);
    m_pProgressBar->setTextVisible(false);

    QHBoxLayout* hbox = new QHBoxLayout(this);
    QVBoxLayout* vbox = new QVBoxLayout();
    vbox->addStretch();
    vbox->addWidget(label);
    vbox->addWidget(m_pProgressBar);
    vbox->addStretch();
    hbox->addStretch();
    hbox->addLayout(vbox);
    hbox->addStretch();
}

void LaunchImage::progress(int value, const QString& serviceName) {
    m_pProgressBar->setValue(value);
    // TODO: show serviceName
    Q_UNUSED(serviceName);
}

void LaunchImage::paintEvent(QPaintEvent *)
{
    QStyleOption opt;
    opt.initFrom(this);
    QPainter p(this);
    style()->drawPrimitive(QStyle::PE_Widget, &opt, &p, this);
}
