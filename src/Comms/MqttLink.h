#ifndef MQTTLINK_H
#define MQTTLINK_H

#include <QObject>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QThread>
#include <QtCore/QLoggingCategory>
#include <mqtt/async_client.h>

class ActionListener : public virtual mqtt::iaction_listener{

private:
    std::string mName;
public:
    ActionListener(const std::string& name);
    void on_failure(const mqtt::token& tok) override;
    void on_success(const mqtt::token& tok) override;

};

class MqttLinkCallback : public QObject, public virtual mqtt::callback, public virtual mqtt::iaction_listener
{
    Q_OBJECT
private:
    int mRetryNum{0};
    int mMaxRetryNum{10};
    std::string mSubTopic{""};
    mqtt::async_client_ptr mMqttClientPtr{nullptr};
    mqtt::connect_options mConnectOptions;
    ActionListener mSubActionListener;
signals:
    void messageArrived(const std::string &payload);
public:
    MqttLinkCallback();
    MqttLinkCallback(const std::string &subTopic, mqtt::async_client_ptr cli, mqtt::connect_options connOpts);
    void reconnect();
    void on_failure(const mqtt::token& tok) override;
    void on_success(const mqtt::token& tok) override;
    void connected(const std::string& cause) override;
    void connection_lost(const std::string& cause) override;
    void message_arrived(mqtt::const_message_ptr msg) override;
    void delivery_complete(mqtt::delivery_token_ptr token) override;
};

class MqttLink : public QObject
{
    Q_OBJECT

public:
    MqttLink(const QString &serverAddr, const QString &subTopic);

public slots:
    void start();
    void subscribedMessage(const std::string &payload);

signals:
    void notifyMessage(const QVariantMap newSetting);

private:
    bool mStartup{true};
    QString mServerAddr{};
    QString mSubTopic{""};
    QString mClientId{"qgc-mqtt-client-id"};
    MqttLinkCallback *mMqttLinkCallback = nullptr;
    mqtt::connect_options mConnectOptions;
    mqtt::async_client_ptr mAsyncMqttClientPtr = nullptr;
};
#endif // MQTTLINK_H
