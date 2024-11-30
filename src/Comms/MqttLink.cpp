#include "MqttLink.h"

/********************** ActionListener **********************/
ActionListener::ActionListener(const std::string& name) : mName(name) {}

void ActionListener::on_failure(const mqtt::token &tok){
    qDebug() << "[MqttLink]Action Listener Failure.";
}

void ActionListener::on_success(const mqtt::token &tok){
    qDebug() << "[MqttLink]Action Listener Success.";
}

/********************** MqttLinkCallback **********************/
MqttLinkCallback::MqttLinkCallback() : mSubActionListener("MQTT Link Subscription"){}

MqttLinkCallback::MqttLinkCallback(const std::string &subTopic, mqtt::async_client_ptr cli, mqtt::connect_options connOpts)
    : mSubTopic(subTopic), mMqttClientPtr(cli), mConnectOptions(connOpts), mSubActionListener("MQTT Link Subscription"){}

void MqttLinkCallback::reconnect(){
    std::this_thread::sleep_for(std::chrono::milliseconds(2500));
    try {
        mMqttClientPtr->connect(mConnectOptions, nullptr, *this);
    }catch (const mqtt::exception& exc) {
        qCritical() << "[MqttLink]Reconnect Error: " << exc.what();
    }
}

void MqttLinkCallback::on_failure(const mqtt::token &tok){
    qWarning() << "[MqttLink]Connection Attempt Failed.";
    if(++mRetryNum > mMaxRetryNum){
        exit(1);
    }
    reconnect();
}

void MqttLinkCallback::on_success(const mqtt::token &tok){}

void MqttLinkCallback::connected(const std::string &cause){
    qInfo() << "[MqttLink]Connected.";
    mMqttClientPtr->subscribe(mSubTopic, 1, nullptr, mSubActionListener);
}

void MqttLinkCallback::connection_lost(const std::string &cause){
    if(cause.empty()){
        qWarning() << "[MqttLink]Connection Lost.";
    }else{
        qWarning() << "[MqttLink]Connection Lost: " << cause;
    }
    qInfo() << "[MqttLink]Reconnecting.";
    mRetryNum = 0;
    reconnect();
}

void MqttLinkCallback::message_arrived(mqtt::const_message_ptr msg){
    emit messageArrived(msg->to_string());
}

void MqttLinkCallback::delivery_complete(mqtt::delivery_token_ptr token){}

/********************** MqttLink **********************/
MqttLink::MqttLink(const QString &serverAddr, const QString &subTopic)
    : mServerAddr(serverAddr), mSubTopic(subTopic)
{
    mConnectOptions = mqtt::connect_options_builder()
    .clean_session(true)
        .finalize();
    mAsyncMqttClientPtr = std::make_shared<mqtt::async_client>(serverAddr.toStdString(), mClientId.toStdString());
    mMqttLinkCallback = new MqttLinkCallback(subTopic.toStdString(), mAsyncMqttClientPtr, mConnectOptions);
    connect(mMqttLinkCallback, &MqttLinkCallback::messageArrived, this, &MqttLink::subscribedMessage);
}

void MqttLink::subscribedMessage(const std::string &payload){
    qDebug() << "======>Data received from mqtt: " << payload;
    QJsonDocument jsonDoc = QJsonDocument::fromJson(QString::fromStdString(payload).toUtf8());
    QVariantMap msg = jsonDoc.toVariant().toMap();
    emit notifyMessage(msg);
}

void MqttLink::start(){
    try {
        mAsyncMqttClientPtr->set_callback(*mMqttLinkCallback);
        mAsyncMqttClientPtr->connect(mConnectOptions, nullptr, *mMqttLinkCallback)->wait();
    }
    catch (const mqtt::exception& exc) {
        qCritical() << "ERROR: Unable to connect to MQTT server.";
    }
    catch (...) {
        qCritical() << "ERROR: Caught unknown mqtt exception";
    }
}
