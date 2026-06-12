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
    int L = line.length();

    for (int i = 0; i < L; i++)
    {
        // Need at least DD/MM -> positions i..i+4
        if (i + 4 >= L)
            break;

        if (!(isdigit(line[i]) && isdigit(line[i + 1]) && line[i + 2] == '/' && isdigit(line[i + 3]) && isdigit(line[i + 4])))
            continue;

        int pos = i + 5; // position after DD/MM

        // Optional year: /YYYY
        if (pos < L && line[pos] == '/' && pos + 4 < L && isdigit(line[pos + 1]) && isdigit(line[pos + 2]) && isdigit(line[pos + 3]) && isdigit(line[pos + 4]))
        {
            pos += 5; // skip '/YYYY'
        }

        // Skip whitespace
        int j = pos;
        while (j < L && isspace(line[j]))
            j++;

        // Optional separator like '-' or longer dashes
        if (j < L && (line[j] == '-' || line.startsWith("\u2013", j) || line.startsWith("\u2014", j)))
        {
            if (line.startsWith("\u2013", j) || line.startsWith("\u2014", j))
            {
                // UTF-8 dash sequences are 3 bytes long
                j += 3;
            }
            else
            {
                j++;
            }
            while (j < L && isspace(line[j]))
                j++;
        }

        // Now expect time HH:MM (optionally :SS)
        if (j + 4 < L && isdigit(line[j]) && isdigit(line[j + 1]) && line[j + 2] == ':' && isdigit(line[j + 3]) && isdigit(line[j + 4]))
        {
            int end = j + 5; // exclusive
            // optional :SS
            if (end + 2 < L && line[end] == ':' && isdigit(line[end + 1]) && isdigit(line[end + 2]))
                end += 3;

            outTs = line.substring(i, end);
            outTs.trim();
            return true;
        }
    }

    return false;
}

// Find lines that look like they *should* have timestamps (for diagnostics)
String collectSuspectedTimestampLines(const String &line)
{
    String suspected = "";
    
    // Pattern 1: DD/MM but invalid time part
    for (int i = 0; i + 5 <= line.length(); i++)
    {
        if (isdigit(line[i]) && isdigit(line[i+1]) && line[i+2] == '/' && isdigit(line[i+3]) && isdigit(line[i+4]))
        {
            // Found a date part - check if rest looks corrupted
            if (i + 6 < line.length() && (line[i+5] == ' ' || isspace(line[i+5])))
            {
                // Likely corrupted timestamp - extract context
                int start = (i > 0) ? i - 2 : 0;
                int end = (i + 10 < line.length()) ? i + 10 : line.length();
                suspected = line.substring(start, end);
                return suspected;
            }
        }
    }

    // Pattern 2: Looks like time format HH:MM but not in proper context
    for (int i = 0; i + 5 <= line.length(); i++)
    {
        if (isdigit(line[i]) && isdigit(line[i+1]) && line[i+2] == ':' && isdigit(line[i+3]) && isdigit(line[i+4]))
        {
            // Found time pattern - check context
            if ((i < 3 || !isdigit(line[i-1])) && 
                (i > 0 && (isspace(line[i-1]) || line[i-1] == '/')))
            {
                int start = (i > 3) ? i - 3 : 0;
                int end = (i + 8 < line.length()) ? i + 8 : line.length();
                suspected = line.substring(start, end);
                return suspected;
            }
        }
    }

    return suspected;
}

class SerialFTDI : public EspUsbHostSerial_FTDI
{
private:
    String lineBuffer;
    std::vector<String> activeBuffer;
    std::vector<String> garbageLineBuffer;  // Collect suspected timestamp lines

    unsigned long groupStartTime = 0;
    String currentTimestamp = "";
    String lastGoodTimestamp = "00/00 00:00";  // Fallback timestamp
    bool currentEventIsSmokeAlarm = false;
    String currentEvent = "";
    String currentSubject = "";
    unsigned long lastEventTime = 0;
    unsigned long garbageReportTime = 0;
    static const unsigned long GARBAGE_REPORT_INTERVAL = 60000;  // Report every 60 seconds

    void onNew() override
    {
        writeLog("USB FTDI connected");
        writeLog(("Manufacturer: " + getManufacturer()).c_str());
        writeLog(("Product: " + getProduct()).c_str());
        writeLog("Baud rate: " + BAUD_RATE);
        lineBuffer.clear();
        activeBuffer.clear();
        garbageLineBuffer.clear();
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
                handleGarbage(String((char)c));
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
        //Serial.printf("[%lu] Received valid line: %s\n", millis(), line.c_str());

        //-----------------    
        while (line.endsWith("\r") || line.endsWith("\n"))
        {
            line.remove(line.length() - 1);
        }

        if (line.isEmpty())
            return;

        logInfo("USB RX: " + line);

        // Try to extract detector address from this line BEFORE checking if it's a timestamp
        String detectorAddr;
        if (currentEventIsSmokeAlarm && tryExtractDetectorAddress(line, detectorAddr))
        {
            String ts = currentTimestamp.isEmpty() ? lastGoodTimestamp : currentTimestamp;
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
                lastGoodTimestamp = currentTimestamp;  // Remember this timestamp
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
                currentSubject = "VAO22 EVENT";
            }

            logInfo("New event started: " + currentSubject);
        }
        else
        {
            // Append non-header lines to current event
            currentEvent += line + "\n";
            
            // If we haven't found a timestamp yet but this line looks like it should have one, log it
            if (currentTimestamp.isEmpty() && line.indexOf("/") >= 0 && line.indexOf(":") >= 0)
            {
                logInfo("POSSIBLE CORRUPT TIMESTAMP in line: " + line);
            }
        }

        lastEventTime = millis();
    }

    void handleGarbage(String garbageChar)
    {
        static int garbageCount = 0;
        garbageCount++;

        // When we hit garbage, try to detect if the line contains suspected timestamp patterns
        String suspected = collectSuspectedTimestampLines(lineBuffer);
        if (suspected.length() > 0 && garbageLineBuffer.size() < 50)  // Keep buffer manageable
        {
            garbageLineBuffer.push_back(suspected);
            logInfo("SUSPECTED TIMESTAMP LINE (garbage detected): " + suspected);
        }

        if (garbageCount > 5)
        {
            logInfo("Too much garbage! Dropping current line: " + lineBuffer);
            lineBuffer.clear();
            garbageCount = 0;
        }
    }

    void sendDiagnosticReport()
    {
        if (garbageLineBuffer.empty())
            return;

        String diagnosticBody = "DIAGNOSTIC REPORT: Suspected Timestamp Lines (for garbage filtering analysis)<br><br>";
        diagnosticBody += "Total suspected lines collected: " + String(garbageLineBuffer.size()) + "<br><br>";
        diagnosticBody += "<strong>Suspected Timestamp Patterns:</strong><br>";

        for (size_t i = 0; i < garbageLineBuffer.size() && i < 100; i++)  // Limit to 100 for email size
        {
            diagnosticBody += "[" + String(i+1) + "] " + garbageLineBuffer[i] + "<br>";
        }

        diagnosticBody += "<br><strong>Last Good Timestamp:</strong> " + lastGoodTimestamp + "<br>";

        queueEmail(
            "VAO22 DIAGNOSTIC: Timestamp/Garbage Analysis",
            formatHtml(diagnosticBody)
        );

        logInfo("Diagnostic report queued with " + String(garbageLineBuffer.size()) + " suspected lines");
    }

public:
    void checkTimeout()
    {
        // Check for garbage report interval
        if (millis() - garbageReportTime > GARBAGE_REPORT_INTERVAL && garbageLineBuffer.size() >= 10)
        {
            sendDiagnosticReport();
            garbageLineBuffer.clear();
            garbageReportTime = millis();
        }

        // Check for event timeout
        if (currentEvent.isEmpty())
            return;

        if (millis() - lastEventTime > 5000)
        {
            // Use last good timestamp if current timestamp is missing
            String ts = currentTimestamp.isEmpty() ? lastGoodTimestamp : currentTimestamp;
            String eventWithTs = "TIMESTAMP: " + ts + "\n" + currentEvent;

            queueEmail(
                currentSubject,
                formatHtml(eventWithTs)
            );

            logInfo("Event timeout queued: " + currentSubject + " [TS: " + ts + "]");
            currentEvent.clear();
            currentSubject.clear();
        }
    }
};

SerialFTDI usbSerial;

void handleSerialConsoleLine(const String &line)
{
    String command = line;
    command.trim();
    command.toUpperCase();

    if (command == "LIST DETECTORS" || command == "DETECTORS" || command == "SHOW DETECTORS")
    {
        sendDetectorListEmail();
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
