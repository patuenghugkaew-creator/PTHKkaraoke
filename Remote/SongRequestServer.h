#ifndef SONGREQUESTSERVER_H
#define SONGREQUESTSERVER_H

// Song requests from phones in the karaoke room. Customers scan their QR code, search the song
// library or YouTube and ask for a song; the owner approves it (on the PC or on the owner's phone,
// which has its own QR code) before it joins the queue. Separate from the remote control
// (RemoteServer), which gives full control of the program and is for the owner only.

#include <QObject>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <functional>

class SongRequestServer : public QObject
{
    Q_OBJECT

public:
    struct Request {
        int id = 0;
        QString who;            // name the customer typed
        QString ip;
        QString key;            // "L:<songtype>|<path>" (library) or "Y:<video id>"
        QString title, artist;
        int seconds = 0;
        QString status;         // "pending", "approved", "rejected"
        qint64 time = 0;
    };

    // provided by the player
    std::function<QJsonArray(const QString &text)> searchLibrary;   // [{key,title,artist}]
    std::function<QJsonObject()> queueState;                        // {now:{title,artist}, queue:[...]}
    std::function<bool(const Request &)> addToQueue;               // an approved request

    explicit SongRequestServer(QObject *parent = nullptr);
    ~SongRequestServer() override;

    bool start(const QString &ip);      // tries port 8780.. (10 ports)
    void stop();
    bool isRunning() const { return server.isListening(); }
    QString customerUrl() const;
    QString ownerUrl() const;

    bool autoApprove() const { return autoOk; }
    void setAutoApprove(bool on);
    QList<Request> pending() const;
    void decide(int id, bool approve);

signals:
    void requestsChanged();             // new request / decided

private:
    void onNewConnection();
    void onReadyRead(QTcpSocket *s);
    void handle(QTcpSocket *s, const QByteArray &method, const QString &path,
                const QByteArray &query, const QByteArray &body);
    void reply(QTcpSocket *s, int code, const QByteArray &contentType, const QByteArray &body);
    void replyJson(QTcpSocket *s, const QJsonObject &o);
    void searchYouTube(QTcpSocket *s, const QString &text);
    QJsonObject stateFor(const QString &ip, bool owner) const;

    QTcpServer server;
    QString ipText, custToken, ownerToken;
    QList<Request> requests;
    int nextId = 1;
    bool autoOk = false;
    QElapsedTimer clock;
};

#endif // SONGREQUESTSERVER_H
