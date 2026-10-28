#include "Remote/RemoteDialog.h"
#include "Remote/RemoteServer.h"
#include "Remote/QrCode.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>

QrWidget::QrWidget(QWidget *parent) : QWidget(parent)
{
    setMinimumSize(240, 240);
}

void QrWidget::setText(const QString &text)
{
    modules = text.isEmpty() ? std::vector<std::vector<bool> >()
                             : qr::encode(text.toUtf8().constData());
    update();
}

void QrWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), Qt::white);

    if (modules.empty())
    {
        p.setPen(QColor("#777777"));
        p.drawText(rect(), Qt::AlignCenter, tr("ยังไม่ได้เปิดใช้งาน"));
        return;
    }

    const int n = int(modules.size());
    const int quiet = 3;
    const int total = n + quiet * 2;
    const int cell = qMax(1, qMin(width(), height()) / total);
    const int side = cell * total;
    const int ox = (width() - side) / 2 + quiet * cell;
    const int oy = (height() - side) / 2 + quiet * cell;

    p.setPen(Qt::NoPen);
    p.setBrush(Qt::black);
    for (int y = 0; y < n; y++)
        for (int x = 0; x < n; x++)
            if (modules[y][x])
                p.drawRect(ox + x * cell, oy + y * cell, cell, cell);
}

// ---------------------------------------------------------------------------

RemoteDialog::RemoteDialog(RemoteServer *srv, QWidget *parent)
    : QDialog(parent), server(srv)
{
    setWindowTitle(tr("ควบคุมผ่านมือถือ"));
    setProperty("pthkRemoteSkip", true);     // never mirror this dialog itself

    QVBoxLayout *lay = new QVBoxLayout(this);
    lay->setSpacing(10);

    QLabel *intro = new QLabel(tr("เปิดใช้งานแล้วใช้มือถือสแกน QR Code\n"
                                  "หน้าจอโปรแกรมจะขึ้นบนมือถือ และแตะควบคุมได้"), this);
    intro->setAlignment(Qt::AlignCenter);
    lay->addWidget(intro);

    QHBoxLayout *ipRow = new QHBoxLayout();
    ipRow->addWidget(new QLabel(tr("เครือข่าย (IP):"), this));
    cbIp = new QComboBox(this);
    cbIp->addItems(RemoteServer::localAddresses());
    ipRow->addWidget(cbIp, 1);
    lay->addLayout(ipRow);

    qrw = new QrWidget(this);
    lay->addWidget(qrw, 0, Qt::AlignCenter);

    lbUrl = new QLabel(this);
    lbUrl->setAlignment(Qt::AlignCenter);
    lbUrl->setTextInteractionFlags(Qt::TextSelectableByMouse);
    lbUrl->setWordWrap(true);
    lay->addWidget(lbUrl);

    lbStatus = new QLabel(this);
    lbStatus->setAlignment(Qt::AlignCenter);
    lay->addWidget(lbStatus);

    btn = new QPushButton(this);
    lay->addWidget(btn);

    lbHint = new QLabel(tr("มือถือและคอมพิวเตอร์ต้องอยู่ Wi-Fi/เครือข่ายเดียวกัน\n"
                           "ถ้า Windows ถามเรื่องไฟร์วอลล์ ให้กด อนุญาต (เครือข่ายส่วนตัว)\n"
                           "ใครสแกน QR นี้ได้จะควบคุมโปรแกรมได้ จึงควรปิดเมื่อเลิกใช้"), this);
    lbHint->setAlignment(Qt::AlignCenter);
    lbHint->setWordWrap(true);
    QFont f = lbHint->font();
    f.setPointSizeF(f.pointSizeF() * 0.9);
    lbHint->setFont(f);
    lay->addWidget(lbHint);

    connect(btn, &QPushButton::clicked, this, &RemoteDialog::toggle);
    connect(cbIp, QOverload<int>::of(&QComboBox::activated), this, [this](int) {
        if (server->isRunning())
        {
            server->start(cbIp->currentText());
            refresh();
        }
    });
    connect(server, &RemoteServer::clientsChanged, this, &RemoteDialog::refresh);

    refresh();
}

void RemoteDialog::toggle()
{
    if (server->isRunning())
    {
        server->stop();
    }
    else
    {
        if (cbIp->currentText().isEmpty())
        {
            lbStatus->setText(tr("ไม่พบเครือข่าย (ไม่ได้เชื่อมต่อ Wi-Fi/LAN)"));
            return;
        }
        if (!server->start(cbIp->currentText()))
        {
            lbStatus->setText(tr("เปิดพอร์ตไม่สำเร็จ ลองใหม่อีกครั้ง"));
            return;
        }
    }
    refresh();
}

void RemoteDialog::refresh()
{
    const bool on = server->isRunning();
    qrw->setText(on ? server->url() : QString());
    lbUrl->setText(on ? server->url() : QString());
    btn->setText(on ? tr("ปิดการควบคุมผ่านมือถือ") : tr("เปิดการควบคุมผ่านมือถือ"));
    cbIp->setEnabled(true);

    if (on)
    {
        int n = server->clientCount();
        lbStatus->setText(n > 0 ? tr("มือถือเชื่อมต่ออยู่: %1 เครื่อง").arg(n)
                                : tr("เปิดใช้งานแล้ว รอมือถือสแกน..."));
    }
    else
    {
        lbStatus->setText(tr("ปิดอยู่"));
    }
}
