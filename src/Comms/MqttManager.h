#ifndef MQTTMANAGER_H
#define MQTTMANAGER_H

#include <QObject>
#include <QThread>
#include "QGCToolbox.h"
#include <QDateTime>
#include "MqttLink.h"

class SettingManager;
class QGCApplication;

class MqttManager : public QGCTool
{
    Q_OBJECT
public:
    explicit MqttManager(QGCApplication *app, QGCToolbox *toolbox);
    ~MqttManager();
    void start();
    Q_INVOKABLE void changeGear(int value);
    void loadConfig();

public slots:
    void handleMessage(const QVariantMap newSetting);

signals:
    void updateMessage(const QVariantMap newSetting);

private:
    QThread mMqttLinkWorkThread;
    MqttLink *mMqttLink = nullptr;
    QString mMqttServerAddr{};
    QString mMqttSubTopic{};
    QString mMqttPubTopic{};
};
#endif // MQTTMANAGER_H
