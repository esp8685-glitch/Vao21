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
#include "ethernet_manager.h"

/*
 Root CA sertifikaat HiveMQ Cloud jaoks
*/
ESP_SSLClient sslClient;
PubSubClient mqtt(sslClient);

unsigned long lastReconnect = 0;
unsigned long lastHeartbeat = 0;
volatile bool detectorEmailRequest = false;
volatile bool clearDetectorsRequest = false;


portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
String mqttQueue[MQTT_QUEUE_SIZE];
volatile int mqIn = 0;
volatile int mqOut = 0;

void mqttDisconnect(){mqtt.disconnect();}

void mqttCallback(char* topic, byte* payload, unsigned int length){
    String msg;
    for (unsigned int i = 0; i < length; i++){
        msg += (char)payload[i];
    }
    logInfo("[MQTT] " + String(topic) + " => " + msg);
    
    // Handle test mode state
    if (String(topic) == "vao21/state/test_mode")
    {
        if (msg == "ON")
        {
            setTestMode(true);
            logInfo("Test mode enabled via MQTT");
        }
        else if (msg == "OFF")
        {
            setTestMode(false);
            logInfo("Test mode disabled via MQTT");
        }
    }
    else if (String(topic) == "vao22/cmd")
    {
        int next = (mqIn + 1) % MQTT_QUEUE_SIZE;

        if (next != mqOut)
        {
            portENTER_CRITICAL(&mux);
            mqttQueue[mqIn] = msg;
            mqIn = next;
            portEXIT_CRITICAL(&mux);
        }
    }
}
    

bool mqttReconnect()
{
    if (!lockEthernetBus(pdMS_TO_TICKS(10000)))
    {
        logError("[MQTT] ETH LOCK FAIL");
        return false;
    }

    ethClient.stop();
    delay(100);

    String clientId = "vao22-";
    clientId += String((uint32_t)ESP.getEfuseMac(), HEX);

    bool ok = mqtt.connect(
        clientId.c_str(),
        mqtt_user.c_str(),
        mqtt_pass.c_str(),
        "vao22/status",
        1,
        true,
        "offline"
    );

    if (ok)
    {
        mqtt.publish("vao22/status", "online", true);
        mqtt.subscribe("vao22/cmd");
        mqtt.subscribe("vao21/state/test_mode");
        logInfo("[MQTT] Connected");
    }
    else
    {
        logError("[MQTT] Failed rc=" + String(mqtt.state()));
    }

    unlockEthernetBus();

    return ok;
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

if (lockEthernetBus(pdMS_TO_TICKS(1000)))
{
    mqtt.loop();
    unlockEthernetBus();
}

while (true)
{
    String cmd;
    portENTER_CRITICAL(&mux);
    if (mqOut == mqIn){
        portEXIT_CRITICAL(&mux);
        break;
    }
    cmd = mqttQueue[mqOut];
    mqOut = (mqOut + 1) % MQTT_QUEUE_SIZE;
    portEXIT_CRITICAL(&mux);
    processMqttCommand(cmd);
}
//logInfo("MQTT queue processed");

    if (millis() - lastHeartbeat > 60000){
        lastHeartbeat = millis();
        String hb = String(millis());
        mqtt.publish("vao22/heartbeat", hb.c_str());
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
    else if (cmd == "enable_email2"){
        email2Enabled = true;
        logInfo("Email2 enabled");
    }
    else if (cmd == "disable_email2"){
        email2Enabled = false;
        logInfo("Email2 disabled");
    }
    else if (cmd == "silent_on"){
        silentMode = true;
        logInfo("Silent mode enabled");
    }
    else if (cmd == "silent_off"){
        silentMode = false;
        logInfo("Silent mode disabled");
    }
    else if (cmd == "reboot"){
            delay(1000);
            ESP.restart();
    }
    else if (cmd == "ping"){
            mqtt.publish("vao22/status", "online");
    }    
}

