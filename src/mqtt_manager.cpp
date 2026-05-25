#include "mqtt_manager.h"
#include "config_manager.h"
#include <Arduino.h>
#include <SPI.h>
#include <Ethernet2.h>
#include <ESP_SSLClient.h>
#include <PubSubClient.h>
#include "config.h"
#include "certs.h"
#include "ethernet_shared.h"
#include "detector_manager.h"
#include "logger.h"

/*
 Root CA sertifikaat HiveMQ Cloud jaoks
*/
ESP_SSLClient sslClient;
PubSubClient mqtt(sslClient);

unsigned long lastReconnect = 0;
unsigned long lastHeartbeat = 0;
volatile bool detectorEmailRequest = false;
volatile bool clearDetectorsRequest = false;
std::vector<String> mqttQueue;

void mqttCallback(char* topic, byte* payload, unsigned int length){
    String msg;
    for (unsigned int i = 0; i < length; i++){
        msg += (char)payload[i];
    }
    Serial.printf("[MQTT] %s => %s\n", topic, msg.c_str());
    if (String(topic) == "vao21/cmd") mqttQueue.push_back(msg);
    
}

bool mqttReconnect()
{
    String clientId = "vao21-";
    clientId += String((uint32_t)ESP.getEfuseMac(), HEX);

    if (mqtt.connect(
        clientId.c_str(),
        mqtt_user.c_str(),
        mqtt_pass.c_str(),
        "vao21/status",
        1,
        true,
        "offline"))
    {
        mqtt.publish("vao21/status", "online", true);
        mqtt.subscribe("vao21/cmd");
        Serial.println("[MQTT] Connected");
        return true;
    }
    Serial.printf(
        "[MQTT] Failed rc=%d\n",
        mqtt.state());

    return false;
}

void mqttSetup()
{
    sslClient.setClient(&ethClient);
    sslClient.setCACert(HIVEMQ_CA_CERT);

    mqtt.setServer(mqtt_host.c_str(), mqtt_port);
    mqtt.setCallback(mqttCallback);
}
void mqttLoop()
{
    if (!mqtt.connected())
    {
        if (millis() - lastReconnect > 5000){
            lastReconnect = millis();
            mqttReconnect();
        }

        return;
    }

    mqtt.loop();

    if (millis() - lastHeartbeat > 60000){
        lastHeartbeat = millis();
        mqtt.publish(
            "vao21/heartbeat",
            String(millis()).c_str());
    }
}

void mqttPublish(
    const String &topic,
    const String &payload)
    {
        if (mqtt.connected())
        {
            mqtt.publish(
                topic.c_str(),
                payload.c_str());
        }
    }

bool mqttConnected(){
    return mqtt.connected();
}

void processMqttCommand(const String &cmd){
    if (cmd.startsWith("add_detector:"))
    {
        String address = cmd.substring(strlen("add_detector:"));
        address.trim();
        addOrUpdateDetector(address, "manual");
    }
    else if (cmd.startsWith("remove_detector:"))
    {
        String address = cmd.substring(strlen("remove_detector:"));
        address.trim();
        removeDetector(address);
    }
    else if (cmd == "clear_detectors"){clearDetectorList();}
    else if (cmd == "email_detectors"){sendDetectorListEmail();}
    else if (cmd == "silent_on"){
        silentMode = true;
        logInfo("Silent mode enabled");
    }
    else if (cmd == "silent_off"){
        silentMode = false;
        logInfo("Silent mode disabled");
    }
}

