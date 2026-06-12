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
// Looks for numeric patterns like 3.031, 3.036, etc.
bool tryExtractDetectorAddress(const String &line, String &address)
{
    // Pattern: one or more digits, dot, three or four digits (e.g., 3.031, 10.0042)
    int dotPos = -1;
    int startPos = -1;
    int endPos = -1;

    // Find all potential matches in the line
    for (int i = 0; i < line.length(); i++)
    {
        if (line[i] == '.')
        {
            // Check if there are digits before the dot
            if (i > 0 && isdigit(line[i - 1]))
            {
                // Find the start of the number (digits before the dot)
                startPos = i - 1;
                while (startPos > 0 && isdigit(line[startPos - 1]))
                {
                    startPos--;
                }

                // Check if there are 3-4 digits after the dot
                if (i + 4 <= line.length() && 
                    isdigit(line[i + 1]) && 
                    isdigit(line[i + 2]) && 
                    isdigit(line[i + 3]))
                {
                    endPos = i + 4;
                    
                    // Check if the 4th digit is actually part of the number or something else
                    // For robustness, allow 3-4 digits after decimal
                    if (i + 5 < line.length() && isdigit(line[i + 4]))
                    {
                        endPos = i + 5;
                    }

                    address = line.substring(startPos, endPos);
                    return true;
                }
                else if (i + 3 <= line.length() && 
                         isdigit(line[i + 1]) && 
                         isdigit(line[i + 2]) && 
                         isdigit(line[i + 3]))
                {
                    endPos = i + 4;
                    address = line.substring(startPos, endPos);
                    return true;
                }
            }
        }
    }

    return false;
}

// Try to find a timestamp inside the given line. Expected format: "DD/MM HH:MM" or "DD/MM HH:MM:SS"
bool findTimestampInLine(const String &line, String &outTs)
{
    for (int i = 0; i + 11 <= line.length(); i++)
    {
        if (isdigit(line[i]) && isdigit(line[i+1]) && line[i+2] == '/' && isdigit(line[i+3]) && isdigit(line[i+4]) && line[i+5] == ' ' && isdigit(line[i+6]) && isdigit(line[i+7]) && line[i+8] == ':' && isdigit(line[i+9]) && isdigit(line[i+10]))
        {
            int len = 11;
            if (i + 14 <= line.length() && line[i+11] == ':' && isdigit(line[i+12]) && isdigit(line[i+13]))
                len = 14;
            outTs = line.substring(i, i + len);
            outTs.trim();
            return true;
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
    String currentTimestamp = "";
    bool currentEventIsSmokeAlarm = false;
    String currentEvent = "";
    String currentSubject = "";
    unsigned long lastEventTime = 0;

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

    void onReceive(const uint8_t *data, const size_t length) override
    {
        for (size_t i = 0; i < length; i++)
        {
            uint8_t c = data[i];

            if (isValidChar(c))
            {
                if (c == '\r' || c == '\n')
                {
                    if (lineBuffer.length() > 0)
                    {
                        processLine(lineBuffer);
                        lineBuffer.clear();
                    }
                }
                else
                {
                    lineBuffer += (char)c;
                }
            }
            else
            {
                handleGarbage();
            }
        }
    }

    bool isTimestampLine(const String &line)
    {
        String ts;
        return findTimestampInLine(line, ts);
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

    void processLine(String line)
    {
        if (line.length() < 2)
        {
            Serial.printf("Skipping too short 0_line: '%s'\n", line.c_str());
            return;
        }

        if (!line.isEmpty() && !isValidLeadingChar(line[0]))
        {
            line.remove(0, 1);
        }
        //-----------------
        // Check for noise: a single uppercase letter followed by whitespace
        if (line.length() > 2 && isupper(line[0]) && isspace(line[1]))
        {
            int firstUsefulChar = 1;
            // Skip over the letter and any spaces after it
            while (firstUsefulChar < line.length() && isspace(line[firstUsefulChar]))
            {
                firstUsefulChar++;
            }
            line = line.substring(firstUsefulChar);
            Serial.printf("Filtered noise prefix, new line: '%s'\n", line.c_str());
        }

        if (line.length() < 2)
        {
            Serial.printf("Skipping too short 1_line: '%s'\n", line.c_str());
            return;
        }
        //----------------
        Serial.printf("[%lu] Received valid line: %s\n", millis(), line.c_str());

        //-----------------    
        while (line.endsWith("\r") || line.endsWith("\n"))
        {
            line.remove(line.length() - 1);
        }

        if (line.isEmpty())
            return;

        // Handle console commands
        String command = line;
        command.trim();
        command.toUpperCase();
        if (command == "LIST DETECTORS" || command == "DETECTORS" || command == "SHOW DETECTORS")
        {
            logInfo("DETECTOR LIST:");
            std::vector<String> detectors = readDetectorListFile();
            if (detectors.empty())
            {
                logError("No detectors found or failed to read detector list file.");
            }
            else
            {
                for (const auto &det : detectors)
                {
                    logInfo(det);
                }
            }
            return;
        }

        logInfo("USB RX: " + line);

        // Try to extract detector address from this line BEFORE checking if it's a timestamp
        String detectorAddr;
        if (currentEventIsSmokeAlarm && tryExtractDetectorAddress(line, detectorAddr))
        {
            String ts = currentTimestamp;
            String lineTs;
            if (findTimestampInLine(line, lineTs))
                ts = lineTs;

            bool existed = detectorExists(detectorAddr);

            if (addOrUpdateDetector(detectorAddr, ts))
            {
                if (!existed)
                {
                    logInfo("NEW DETECTOR: " + detectorAddr);
                    writeLog("Discovered detector: " + detectorAddr + " at " + ts);
                }
                else
                {
                    logInfo("UPDATED DETECTOR: " + detectorAddr + " @ " + ts);
                }
            }
        }

        // Check if this is a timestamp line (event header)
        if (isTimestampLine(line))
        {
            currentEventIsSmokeAlarm = false;
            if (line.indexOf("SUITSUHAIRE") >= 0)
            {
                currentEventIsSmokeAlarm = true;
            }

            String lineTs;
            if (findTimestampInLine(line, lineTs))
            {
                currentTimestamp = lineTs;
                currentTimestamp.trim();
            }

            // SEND PREVIOUS EVENT if it exists
            if (!currentEvent.isEmpty())
            {
                queueEmail(
                    currentSubject,
                    formatHtml(currentEvent)
                );
                logInfo("Event queued: " + currentSubject);
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

            logInfo("New event started: " + currentSubject);
        }
        else
        {
            // Append non-header lines to current event
            currentEvent += line + "\n";
        }

        lastEventTime = millis();
    }

    void handleGarbage()
    {
        static int garbageCount = 0;
        garbageCount++;

        if (garbageCount > 5)
        {
            logInfo("Too much garbage! Dropping current line.");
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

            logInfo("Event timeout queued: " + currentSubject);
            currentEvent.clear();
            currentSubject.clear();
        }
    }
};

SerialFTDI usbSerial;

void printDetectorListToSerial()
{
    logInfo("DETECTOR LIST:");
    std::vector<String> detectors = readDetectorListFile();
    if (detectors.empty())
    {
        logError("No detectors found or failed to read detector list file.");
        return;
    }

    for (const auto &det : detectors)
    {
        logInfo(det);
    }
}

void handleSerialConsoleLine(const String &line)
{
    String command = line;
    command.trim();
    command.toUpperCase();

    if (command == "LIST DETECTORS" || command == "DETECTORS" || command == "SHOW DETECTORS")
    {
        printDetectorListToSerial();
        return;
    }
}

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
