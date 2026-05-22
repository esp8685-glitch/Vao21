#include <LittleFS.h>
#include <vector>

#include "usb_task.h"
#include "email_queue.h"
#include "logger.h"
#include "config.h"
#include "detector_manager.h"

bool isValidChar(uint8_t c)
{
    return (c >= 32 && c <= 126) || c == '\r' || c == '\n';
}

bool isValidLeadingChar(char c)
{
    return (c >= '0' && c <= '5') ||
           (c >= '7' && c <= '9') ||
           (c >= 'A' && c <= 'Z') ||
           c == '/';
}

// Helper function to extract detector address from a line
// Looks for hex addresses patterns like AA:BB:CC:DD:EE:FF or AABBCCDDEEFF
bool tryExtractDetectorAddress(const String &line, String &address)
{
    // Pattern 1: MAC address format AA:BB:CC:DD:EE:FF
    if (line.length() >= 17)
    {
        // Check for pattern like XX:XX:XX:XX:XX:XX
        int colonCount = 0;
        int startPos = -1;
        
        for (int i = 0; i <= line.length() - 17; i++)
        {
            colonCount = 0;
            bool isValidMac = true;
            
            // Check 17 character segment
            for (int j = 0; j < 17; j++)
            {
                char c = line[i + j];
                if (j % 3 == 2)
                {
                    if (c != ':')
                    {
                        isValidMac = false;
                        break;
                    }
                    colonCount++;
                }
                else
                {
                    if (!isxdigit(c))
                    {
                        isValidMac = false;
                        break;
                    }
                }
            }
            
            if (isValidMac && colonCount == 5)
            {
                address = line.substring(i, i + 17);
                address.toUpperCase();
                return true;
            }
        }
    }

    // Pattern 2: Continuous hex without colons (12 characters)
    if (line.length() >= 12)
    {
        for (int i = 0; i <= line.length() - 12; i++)
        {
            bool isValidHex = true;
            for (int j = 0; j < 12; j++)
            {
                if (!isxdigit(line[i + j]))
                {
                    isValidHex = false;
                    break;
                }
            }
            
            if (isValidHex)
            {
                String hexAddr = line.substring(i, i + 12);
                hexAddr.toUpperCase();
                // Format as MAC address
                address = hexAddr.substring(0, 2) + ":" + 
                         hexAddr.substring(2, 4) + ":" +
                         hexAddr.substring(4, 6) + ":" +
                         hexAddr.substring(6, 8) + ":" +
                         hexAddr.substring(8, 10) + ":" +
                         hexAddr.substring(10, 12);
                return true;
            }
        }
    }

    return false;
}

class SerialFTDI : public EspUsbHostSerial_FTDI
{
private:
    String lineBuffer;
    std::vector<String> activeBuffer;

    unsigned long groupStartTime = 0;

    void onNew() override
    {
        writeLog("USB FTDI connected");
        writeLog(("Manufacturer: " + getManufacturer()).c_str());
        writeLog(("Product: " + getProduct()).c_str());
        writeLog("Baud rate: " + BAUD_RATE);
        lineBuffer.clear();
        activeBuffer.clear();
        groupStartTime = 0;
    }

  void onReceive(const uint8_t *data, const size_t length) override {
    for (size_t i = 0; i < length; i++) {
      uint8_t c = data[i];

      if (isValidChar(c)) {
        if (c == '\r' || c == '\n') {
          if (lineBuffer.length() > 0) {
            processLine(lineBuffer);
            lineBuffer.clear();
          }
        } else {
          lineBuffer += (char)c;
        }
      } else {
        handleGarbage();
      }
    }
  }
String currentEvent = "";
String currentSubject = "";
unsigned long lastEventTime = 0;

bool isTimestampLine(const String &line)
{
    return line.length() > 16 &&
           isdigit(line[0]) &&
           isdigit(line[1]) &&
           line[2] == '/' &&
           isdigit(line[3]);
}
String formatHtml(String text)
{
    text.replace("\r\n", "\n");
    text.replace("\r", "\n");

    text.replace("&", "&amp;");
    text.replace("<", "&lt;");
    text.replace(">", "&gt;");

    text.replace(" ", "&nbsp;");
    text.replace("\n", "<br>");

    return
        "<div style='font-family: Arial; font-size: 10pt;'>"
        + text +
        "</div>";
}
void processLine(String line){

    if (line.length() < 2) {
      Serial.printf("Skipping too short 0_line: '%s'\n", line.c_str());
      return;
    }

    if (!line.isEmpty() && !isValidLeadingChar(line[0])) {
      line.remove(0, 1);
    }
    //-----------------
    // Check for noise: a single uppercase letter followed by whitespace
    if (line.length() > 2 && isupper(line[0]) && isspace(line[1])) {
      int firstUsefulChar = 1;
      // Skip over the letter and any spaces after it
      while (firstUsefulChar < line.length() && isspace(line[firstUsefulChar])) {
        firstUsefulChar++;
      }
      line = line.substring(firstUsefulChar);
      Serial.printf("Filtered noise prefix, new line: '%s'\n", line.c_str());
    }

    if (line.length() < 2) {
      Serial.printf("Skipping too short 1_line: '%s'\n", line.c_str());
      return;
    }
    //----------------
    Serial.printf("[%lu] Received valid line: %s\n", millis(), line.c_str());

//-----------------    
    //line.trim();
    while (line.endsWith("\r") || line.endsWith("\n"))
    {
        line.remove(line.length() - 1);
    }

    if (line.isEmpty())
        return;

    Serial.println("USB RX: " + line);

    // Try to extract detector address from this line
    String detectorAddr;
    if (tryExtractDetectorAddress(line, detectorAddr))
    {
        if (!detectorExists(detectorAddr))
        {
            if (addDetectorAddress(detectorAddr))
            {
                Serial.println("NEW DETECTOR: " + detectorAddr);
                writeLog("Discovered detector: " + detectorAddr);
            }
        }
    }

    if (isTimestampLine(line))
    {
        // SEND PREVIOUS EVENT
        if (!currentEvent.isEmpty())
        {
            queueEmail(
                currentSubject,
                formatHtml(currentEvent)
            );
        }

        // START NEW EVENT
        currentEvent = line + "\n";

        if (line.length() >= 46)
        {
            currentSubject = line.substring(23, 46);
            currentSubject.trim();
        }
        else
        {
            currentSubject = "VAO21 EVENT";
        }
    }
    else
    {
        currentEvent += line + "\n";
    }

    lastEventTime = millis();
}
  void handleGarbage() {
    static int garbageCount = 0;
    garbageCount++;

    if (garbageCount > 5) {
      //Serial.println("Too much garbage! Dropping current line.");
      lineBuffer.clear();
      garbageCount = 0;
    }
  }
public:
void checkTimeout()
{
    if (currentEvent.isEmpty())
        return;

    if (millis() - lastEventTime > 5000)
    {
        queueEmail(
            currentSubject,
            formatHtml(currentEvent)
        );

        currentEvent.clear();
        currentSubject.clear();
    }
}
};
SerialFTDI usbSerial;

void usbTask(void *pv)
{
    writeLog("Baud rate: " + BAUD_RATE);
    usbSerial.begin(BAUD_RATE);

    while (true)
    {
        usbSerial.task();
        usbSerial.checkTimeout();

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
