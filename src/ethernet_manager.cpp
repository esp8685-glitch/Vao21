#include "config.h"
#include "logger.h"
#include "ethernet_manager.h"
#include "storage.h"

byte mac[6] = {
    0x02, 0xAB, 0xCD, 0xEF, 0x12, 0x34
};

bool ethConnected = false;

bool lockEthernetBus(TickType_t timeoutTicks)
{
    if (ethMutex != NULL && xSemaphoreTake(ethMutex, timeoutTicks) != pdTRUE)
        return false;

    if (!lockSPI(timeoutTicks))
    {
        if (ethMutex != NULL)
            xSemaphoreGive(ethMutex);
        return false;
    }

    return true;
}

void unlockEthernetBus()
{
    digitalWrite(ETH_CS, HIGH);
    unlockSPI();

    if (ethMutex != NULL)
        xSemaphoreGive(ethMutex);
}

// ======================================================
// ETHERNET INIT
// ======================================================

bool ethernetConnect()
{
    writeLog("Initializing Ethernet");

    if (!lockEthernetBus(pdMS_TO_TICKS(30000)))
    {
        writeLog("ETH LOCK TIMEOUT");
        return false;
    }
    pinMode(ETH_RST, OUTPUT);
    SPI.begin(
        ETH_SCK,
        ETH_MISO,
        ETH_MOSI,
        ETH_CS
    );

    digitalWrite(ETH_RST, LOW);
    delay(100);

    digitalWrite(ETH_RST, HIGH);
    delay(500);

    Ethernet.init(ETH_CS);
    if (Ethernet.begin(mac) == 0)
    {
        ethConnected = false;
        unlockEthernetBus();
        writeLog("DHCP FAILED");
        return false;
    }
    ethConnected = true;
    IPAddress ip = Ethernet.localIP();
    IPAddress gateway = Ethernet.gatewayIP();
    IPAddress dns = Ethernet.dnsServerIP();

if (dns == IPAddress(0,0,0,0))
{
    writeLog("DNS INVALID -> SET 1.1.1.1");

    Ethernet.setDnsServerIP(IPAddress(1,1,1,1));

    dns = Ethernet.dnsServerIP();
}
    unlockEthernetBus();
    writeLog("IP: " + ip.toString());
    writeLog("Gateway: " + gateway.toString());
    writeLog("DNS: " + dns.toString());
    if (syncSystemTime())
    {
        timeInitialized = true;
        lastTimeSync = millis();
    }
    return true;
}

// ======================================================
// CHECK ETHERNET
// ======================================================

bool ethernetOK()
{
    if (!lockEthernetBus(pdMS_TO_TICKS(2000)))
        return false;

    Ethernet.maintain();

    IPAddress ip = Ethernet.localIP();

    if (ip == IPAddress(0, 0, 0, 0))
    {
        ethConnected = false;
        unlockEthernetBus();
        return false;
    }
IPAddress dns = Ethernet.dnsServerIP();

if (dns == IPAddress(0,0,0,0))
{
    writeLog("DNS LOST");
    Ethernet.setDnsServerIP(IPAddress(1,1,1,1));
}
    ethConnected = true;
    unlockEthernetBus();

    return true;
}

// ======================================================
// MONITOR TASK
// ======================================================

void ethernetTask(void *pv)
{
    while(true)
    {
        if (!ethernetOK())
        {
            writeLog("ETH LOST");
            ethernetConnect();
        }else
        {
            // daily NTP resync
            if (millis() - lastTimeSync > TIME_SYNC_INTERVAL){
                if (syncSystemTime()) {
                    timeInitialized = true;
                    lastTimeSync = millis();
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(ETH_TASK_INTERVAL_MS));
    }
}
