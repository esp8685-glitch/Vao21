#include "mqtt_manager.h"
#include <Arduino.h>
#include <SPI.h>
#include <Ethernet2.h>
#include <ESP_SSLClient.h>
#include <PubSubClient.h>
#include "config.h"
#include "certs.h"
#include "ethernet_shared.h"

//const char* mqtt_server ="739b8ed00a7a430ebe58d2dec6e7166b.s1.eu.hivemq.cloud";
//const int mqtt_port = 8883;
//const char* MQTT_USER = "esp8685";
//const char* MQTT_PASS = "#V#G.bU8n6DwN44";

/*
 Root CA sertifikaat HiveMQ Cloud jaoks
*/
ESP_SSLClient sslClient;
PubSubClient mqtt(sslClient);

unsigned long lastReconnect = 0;
unsigned long lastHeartbeat = 0;

void mqttCallback(char* topic, byte* payload, unsigned int length)
{
    String msg;

    for (unsigned int i = 0; i < length; i++)
    {
        msg += (char)payload[i];
    }

    Serial.printf("[MQTT] %s => %s\n", topic, msg.c_str());

    if (String(topic) == "vao21/cmd")
    {
        if (msg == "reboot")
        {
            delay(1000);
            ESP.restart();
        }

        if (msg == "ping")
        {
            mqtt.publish("vao21/status", "online");
        }
    }
}

bool mqttReconnect()
{
    String clientId = "vao21-";
    clientId += String((uint32_t)ESP.getEfuseMac(), HEX);

    if (mqtt.connect(
        clientId.c_str(),
        MQTT_USER,
        MQTT_PASS,
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

    mqtt.setServer(MQTT_HOST, MQTT_PORT);
    mqtt.setCallback(mqttCallback);
}
void mqttLoop()
{
    if (!mqtt.connected())
    {
        if (millis() - lastReconnect > 5000)
        {
            lastReconnect = millis();

            mqttReconnect();
        }

        return;
    }

    mqtt.loop();

    if (millis() - lastHeartbeat > 60000)
    {
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

bool mqttConnected()
{
    return mqtt.connected();
}
