#include <ArduinoJson.h>
#define ENABLE_SMTP
#define ENABLE_DEBUG
#define READYMAIL_DEBUG_PORT Serial
#include <ReadyMail.h>
#include <ESP_SSLClient.h>

#include "email_queue.h"
#include "storage.h"
#include "logger.h"
#include "config.h"
#include "ethernet_manager.h"

extern EthernetClient ethClient;
ESP_SSLClient *global_ssl = nullptr;
static int lastCheckHour = -1;

static void printStackMark(const char *tag)
{
#if SMTP_VERBOSE_DIAGNOSTICS
    UBaseType_t words = uxTaskGetStackHighWaterMark(NULL);

    Serial.printf(
        "[STACK] %s highWater=%u words approxBytes=%u heap=%u\n",
        tag,
        (unsigned)words,
        (unsigned)(words * sizeof(StackType_t)),
        ESP.getFreeHeap()
    );
#endif
}

struct QueuedMail
{
    String path;
    String id;
    String hash;
    String subject;
    String body;
    uint8_t retry = 0;
    uint32_t nextDelayMs = 0;
    uint32_t lastAttemptMs = 0;
    bool inFlight = false;
};

static String queuePathForId(const String &id)
{
    return "/queue/" + id + ".json";
}

static String pendingHashPath(const String &hash)
{
    return "/hash/" + hash + ".pending";
}

static String sentHashPath(const String &hash)
{
    return "/hash/" + hash + ".ok";
}

static String inflightPathForId(const String &id)
{
    return "/queue/" + id + ".lock";
}

static uint32_t retryDelayMs(uint8_t retry)
{
    uint32_t delayMs = QUEUE_BASE_BACKOFF_MS;

    for (uint8_t i = 1; i < retry; i++)
    {
        if (delayMs >= QUEUE_MAX_BACKOFF_MS / 2)
            return QUEUE_MAX_BACKOFF_MS;

        delayMs *= 2;
    }

    if (delayMs > QUEUE_MAX_BACKOFF_MS)
        delayMs = QUEUE_MAX_BACKOFF_MS;

    return delayMs;
}

static bool isDue(const QueuedMail &mail)
{
    if (mail.retry == 0)
        return true;

    return millis() >= mail.nextDelayMs;
}

static void smtpCb(SMTPStatus status)
{
#if SMTP_VERBOSE_DIAGNOSTICS
    if (status.progress.available)
    {
        Serial.print("SMTP UPLOAD ");
        Serial.print(status.progress.value);
        Serial.println("%");
        return;
    }

    if (status.text.length())
        Serial.println("SMTP " + status.text);
#endif
}

static void startTLSCallback(bool &success)
{
    success = false;

    if (global_ssl != nullptr)
        success = global_ssl->connectSSL();
}

static bool writeQueuedMailLocked(const QueuedMail &mail)
{
    DynamicJsonDocument doc(2048);

    doc["id"] = mail.id;
    doc["hash"] = mail.hash;
    doc["subject"] = mail.subject;
    doc["body"] = mail.body;
    doc["retry"] = mail.retry;
    doc["nextDelayMs"] = mail.nextDelayMs;
    doc["lastAttemptMs"] = mail.lastAttemptMs;
    doc["inFlight"] = mail.inFlight;

    String tmpPath = mail.path + ".tmp";
    String bakPath = mail.path + ".bak";

    SD.remove(tmpPath.c_str());

    File tmp = SD.open(tmpPath.c_str(), FILE_WRITE);
    if (!tmp)
        return false;

    if (serializeJson(doc, tmp) == 0)
    {
        tmp.close();
        SD.remove(tmpPath.c_str());
        return false;
    }

    tmp.flush();
    tmp.close();

    SD.remove(bakPath.c_str());

    bool hadOriginal = SD.exists(mail.path.c_str());

    if (hadOriginal && !SD.rename(mail.path.c_str(), bakPath.c_str()))
    {
        SD.remove(tmpPath.c_str());
        return false;
    }

    if (!SD.rename(tmpPath.c_str(), mail.path.c_str()))
    {
        if (hadOriginal)
            SD.rename(bakPath.c_str(), mail.path.c_str());

        SD.remove(tmpPath.c_str());
        return false;
    }

    if (hadOriginal)
        SD.remove(bakPath.c_str());

    return true;
}

static bool readQueuedMailLocked(const String &path, QueuedMail &mail)
{
    File f = SD.open(path.c_str(), FILE_READ);

    if (!f)
        return false;

    DynamicJsonDocument doc(2048);
    DeserializationError err = deserializeJson(doc, f);
    f.close();

    if (err)
        return false;

    mail.path = path;
    mail.id = doc["id"].as<String>();
    mail.hash = doc["hash"].as<String>();
    mail.subject = doc["subject"].as<String>();
    mail.body = doc["body"].as<String>();
    mail.retry = doc["retry"] | 0;
    mail.nextDelayMs = doc["nextDelayMs"] | 0;
    mail.lastAttemptMs = doc["lastAttemptMs"] | 0;
    mail.inFlight = doc["inFlight"] | false;

    return mail.id.length() > 0 && mail.hash.length() > 0;
}

static bool readQueuedMail(const String &path, QueuedMail &mail)
{
    if (!lockSD(pdMS_TO_TICKS(5000)))
        return false;

    bool ok = readQueuedMailLocked(path, mail);
    unlockSD();
    return ok;
}

static bool updateQueuedMail(QueuedMail &mail)
{
    if (!lockSD(pdMS_TO_TICKS(5000)))
        return false;

    bool ok = writeQueuedMailLocked(mail);
    unlockSD();
    return ok;
}

static void markPermanentFailure(const QueuedMail &mail, const String &reason)
{
    if (!lockSD(pdMS_TO_TICKS(5000)))
        return;

    String failedPath = "/failed/" + mail.id + ".json";
    String pendingPath = pendingHashPath(mail.hash);
    String lockPath = inflightPathForId(mail.id);

    SD.remove(failedPath.c_str());

    if (!SD.rename(mail.path.c_str(), failedPath.c_str()))
        SD.remove(mail.path.c_str());

    SD.remove(pendingPath.c_str());
    SD.remove(lockPath.c_str());

    unlockSD();

    writeLog("EMAIL FAILED PERMANENTLY " + mail.id + " " + reason);
}

static void scheduleRetry(QueuedMail &mail, const String &reason)
{
    mail.retry++;
    mail.inFlight = false;
    mail.lastAttemptMs = millis();
    mail.nextDelayMs = millis() + retryDelayMs(mail.retry);

    if (mail.retry >= MAX_RETRY_COUNT)
    {
        markPermanentFailure(mail, reason);
        return;
    }

    if (!updateQueuedMail(mail))
        writeLog("EMAIL RETRY WRITE FAIL " + mail.id);

    writeLog(
        "EMAIL RETRY " +
        mail.id +
        " retry=" +
        String(mail.retry) +
        " delayMs=" +
        String(retryDelayMs(mail.retry)) +
        " reason=" +
        reason
    );
}

static bool claimForSend(QueuedMail &mail)
{
    mail.inFlight = true;
    mail.lastAttemptMs = millis();

    if (!lockSD(pdMS_TO_TICKS(5000)))
        return false;

    String lockPath = inflightPathForId(mail.id);
    File lockFile = SD.open(lockPath.c_str(), FILE_WRITE);

    if (lockFile)
    {
        lockFile.print(mail.hash);
        lockFile.close();
    }

    bool ok = writeQueuedMailLocked(mail);
    unlockSD();

    return ok;
}

static bool markSent(const QueuedMail &mail)
{
    if (!lockSD(portMAX_DELAY))
        return false;

    String sentMarkerPath = "/sent/" + mail.id + ".ok";
    String hashPath = sentHashPath(mail.hash);
    String pendingPath = pendingHashPath(mail.hash);
    String lockPath = inflightPathForId(mail.id);

    File sent = SD.open(sentMarkerPath.c_str(), FILE_WRITE);
    if (sent)
    {
        sent.print("sent");
        sent.close();
    }

    File hashFile = SD.open(hashPath.c_str(), FILE_WRITE);
    if (hashFile)
    {
        hashFile.print(mail.id);
        hashFile.close();
    }

    SD.remove(pendingPath.c_str());
    SD.remove(lockPath.c_str());
    SD.remove(mail.path.c_str());

    unlockSD();

    return true;
}

static String normalizeQueuePath(const String &name)
{
    if (name.startsWith("/queue/"))
        return name;

    if (name.startsWith("/"))
        return name;

    return "/queue/" + name;
}

static bool getNextQueuePath(String &path)
{
    if (!lockSD(pdMS_TO_TICKS(5000)))
        return false;

    File dir = SD.open("/queue");

    if (!dir)
    {
        unlockSD();
        return false;
    }

    bool found = false;
    File file = dir.openNextFile();

    while (file)
    {
        String candidate = normalizeQueuePath(file.name());
        file.close();

        if (candidate.endsWith(".json"))
        {
            QueuedMail mail;

            if (readQueuedMailLocked(candidate, mail))
            {
                String sentPath = sentHashPath(mail.hash);

                if (SD.exists(sentPath.c_str()))
                {
                    SD.remove(candidate.c_str());
                    SD.remove(inflightPathForId(mail.id).c_str());
                }
                else if (!mail.inFlight && isDue(mail))
                {
                    path = candidate;
                    found = true;
                    break;
                }
                else if (mail.inFlight && millis() - mail.lastAttemptMs > QUEUE_STALE_INFLIGHT_MS)
                {
                    mail.inFlight = false;
                    mail.retry++;
                    mail.nextDelayMs = millis() + retryDelayMs(mail.retry);
                    writeQueuedMailLocked(mail);
                    SD.remove(inflightPathForId(mail.id).c_str());
                }
            }
            else
            {
                String failedPath = "/failed/corrupt_" + String(millis()) + ".json";
                SD.rename(candidate.c_str(), failedPath.c_str());
            }
        }

        file = dir.openNextFile();
    }

    dir.close();
    unlockSD();

    return found;
}

void recoverEmailQueue()
{
    if (!lockSD(pdMS_TO_TICKS(10000)))
    {
        Serial.println("Queue recovery: lock failed");
        return;
    }

    File dir = SD.open("/queue");

    if (!dir)
    {
        unlockSD();
        writeLog("Queue recovery: open failed");
        return;
    }

    uint16_t recovered = 0;
    File file = dir.openNextFile();

    while (file)
    {
        String path = normalizeQueuePath(file.name());
        file.close();

        if (path.endsWith(".tmp") || path.endsWith(".bak") || path.endsWith(".lock"))
        {
            SD.remove(path.c_str());
        }
        else if (path.endsWith(".json"))
        {
            QueuedMail mail;

            if (readQueuedMailLocked(path, mail))
            {
                String sentPath = sentHashPath(mail.hash);
                String pendingPath = pendingHashPath(mail.hash);

                if (SD.exists(sentPath.c_str()))
                {
                    SD.remove(path.c_str());
                    SD.remove(inflightPathForId(mail.id).c_str());
                }
                else
                {
                    if (!SD.exists(pendingPath.c_str()))
                    {
                        File pending = SD.open(pendingPath.c_str(), FILE_WRITE);
                        if (pending)
                        {
                            pending.print(mail.id);
                            pending.close();
                        }
                    }

                    if (mail.inFlight)
                    {
                        mail.inFlight = false;
                        mail.retry++;
                        mail.nextDelayMs = millis() + retryDelayMs(mail.retry);
                        writeQueuedMailLocked(mail);
                        SD.remove(inflightPathForId(mail.id).c_str());
                        recovered++;
                    }
                }
            }
            else
            {
                String failedPath = "/failed/corrupt_boot_" + String(millis()) + ".json";
                SD.rename(path.c_str(), failedPath.c_str());
            }
        }

        file = dir.openNextFile();
    }

    dir.close();
    unlockSD();

    if (recovered > 0)
        writeLog("Queue recovery: recovered=" + String(recovered));
}

void queueEmail(String subject, String body)
{
    String hash = sha1String(subject + "\n" + body);

    if (!lockSD(pdMS_TO_TICKS(5000)))
    {
        writeLog("Queue: SD lock failed");
        return;
    }

    String sentPath = sentHashPath(hash);
    String pendingPath = pendingHashPath(hash);
    bool duplicate = SD.exists(sentPath.c_str()) || SD.exists(pendingPath.c_str());

    if (duplicate)
    {
        unlockSD();
        Serial.println("Queue: duplicate blocked");
        return;
    }

    QueuedMail mail;
    mail.id = generateMessageID();
    mail.hash = hash;
    mail.subject = subject;
    mail.body = body;
    mail.path = queuePathForId(mail.id);

    bool saved = writeQueuedMailLocked(mail);

    if (saved)
    {
        File pending = SD.open(pendingPath.c_str(), FILE_WRITE);
        if (pending)
        {
            pending.print(mail.id);
            pending.close();
        }
        else
        {
            SD.remove(mail.path.c_str());
            saved = false;
        }
    }

    unlockSD();

    if (saved)
        Serial.println("Queue: email queued " + mail.id);
    else
        writeLog("Queue: write failed " + mail.id);
}

static bool sendEmailMessage(
    const char *subject,
    const char *body,
    const char *messageId,
    const char *hash,
    String &error
)
{
    error = "";

    if (!ethernetOK())
    {
        Serial.println("Ethernet DOWN");
        error = "Ethernet DOWN";
        return false;
    }

    if (!lockEthernetBus(pdMS_TO_TICKS(120000)))
    {
        error = "ETH LOCK FAIL";
        return false;
    }

    printStackMark("SMTP start");

    Serial.println();
    Serial.println("Preparing SMTP...");

    Serial.printf(
        "Free heap before send: %u\n",
        ESP.getFreeHeap()
    );

    yield();

    bool ok = false;
    ESP_SSLClient ssl;

    ssl.setClient(&ethClient);
    ssl.setInsecure();
    ssl.enableSSL(false);

    global_ssl = &ssl;

    SMTPClient smtp(ssl);

    smtp.setStartTLS(
        startTLSCallback,
        true
    );

    Serial.println("SMTP: connecting");
    printStackMark("BEFORE CONNECT");

    ok = smtp.connect(
        SMTP_HOST,
        SMTP_PORT,
        "",
        smtpCb,
        true
    );

    printStackMark("AFTER CONNECT");

    if (!ok)
    {
        SMTPStatus status = smtp.status();
        error = "SMTP CONNECT FAILED status=" + String(status.statusCode) + " error=" + String(status.errorCode) + " " + status.text;
        Serial.println("SMTP: connect failed");

        ethClient.stop();
        delay(200);
        global_ssl = nullptr;
        unlockEthernetBus();

        Serial.printf(
            "Free heap after send: %u\n",
            ESP.getFreeHeap()
        );

        return false;
    }

    Serial.println("SMTP: connected");
    Serial.println("SMTP: authenticating");
    printStackMark("BEFORE AUTH");

    yield();
    delay(1);

    ok = smtp.authenticate(
        SMTP_LOGIN,
        SMTP_PASSWORD,
        readymail_auth_password
    );

    printStackMark("AFTER AUTH");

    if (!ok)
    {
        SMTPStatus status = smtp.status();
        error = "SMTP AUTH FAILED status=" + String(status.statusCode) + " error=" + String(status.errorCode) + " " + status.text;
        Serial.println("SMTP: auth failed");

        ethClient.stop();
        delay(200);
        global_ssl = nullptr;
        unlockEthernetBus();

        Serial.printf(
            "Free heap after send: %u\n",
            ESP.getFreeHeap()
        );

        return false;
    }

    Serial.println("SMTP: auth ok");

    SMTPMessage msg;

    msg.headers.add(
        rfc822_subject,
        subject
    );

    msg.headers.add(
        rfc822_from,
        String("ESP32 <") + AUTHOR_EMAIL + ">"
    );
//---------------------
    msg.headers.add(rfc822_to, "My_ESP <" + String(RECIPIENT_EMAIL) + ">");
    #ifdef RECIPIENT_EMAIL2
    msg.headers.add(rfc822_to, "Erik <" + String(RECIPIENT_EMAIL2) + ">");
    #endif
//----------------------
    if (messageId != nullptr && strlen(messageId) > 0)
    {
        msg.headers.add(
            rfc822_message_id,
            String("<") + messageId + "@" + DEVICE_NAME + ">"
        );
    }

    // msg.headers.addCustom( #SUUR TÄHTSUS#
    //     "X-Priority",
    //     "1"
    // );

    if (hash != nullptr && strlen(hash) > 0)
    {
        msg.headers.addCustom(
            "X-VAO21-Hash",
            hash
        );
    }

    msg.text.body(body);

    msg.timestamp = time(nullptr);

    Serial.println("SMTP: sending");
    printStackMark("BEFORE SEND");

    ok = smtp.send(msg);

    printStackMark("AFTER SEND");

    if (ok)
    {
        Serial.println("SMTP: sent");
    }
    else
    {
        SMTPStatus status = smtp.status();
        error = "EMAIL FAILED status=" + String(status.statusCode) + " error=" + String(status.errorCode) + " " + status.text;
        Serial.println("SMTP: send failed");
    }

    delay(200);

    ethClient.stop();

    delay(200);

    global_ssl = nullptr;

    printStackMark("SMTP end");

    Serial.printf(
        "Free heap after send: %u\n",
        ESP.getFreeHeap()
    );

    unlockEthernetBus();

    return ok;
}

bool sendEmail(
    const char *subject,
    const char *body
)
{
    String error;
    return sendEmailMessage(subject, body, nullptr, nullptr, error);
}

static bool sendQueuedMail(const QueuedMail &mail, String &error)
{
    return sendEmailMessage(
        mail.subject.c_str(),
        mail.body.c_str(),
        mail.id.c_str(),
        mail.hash.c_str(),
        error
    );
}

void processQueue()
{
    String path;

    if (!getNextQueuePath(path))
        return;

    QueuedMail mail;

    if (!readQueuedMail(path, mail))
    {
        writeLog("Queue: read failed " + path);
        return;
    }

    if (!claimForSend(mail))
    {
        writeLog("Queue: claim failed " + mail.id);
        return;
    }

    String error;

    if (sendQueuedMail(mail, error))
    {
        markSent(mail);
        writeLog("Email sent " + mail.id + " retry=" + String(mail.retry));
        return;
    }

    scheduleRetry(mail, error);
}

void emailTask(void *pv)
{
    Serial.printf("Email task started core=%d\n", xPortGetCoreID());
    printStackMark("emailTask boot");

    while (true)
    {
        printStackMark("emailTask loop");

        if (ethernetOK())
        {
            if (xSemaphoreTake(emailMutex, pdMS_TO_TICKS(5000)))
            {
                processQueue();
                handleHourlyCheck();
                xSemaphoreGive(emailMutex);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(EMAIL_TASK_INTERVAL_MS));
    }
}
void handleHourlyCheck()
{
    if (!CONTROL_EMAIL_ENABLED)
        return;

    if (!timeInitialized)
        return;

    struct tm timeinfo;

    getTallinnTimeNow(timeinfo);

    if (
        timeinfo.tm_min == 0 &&
        timeinfo.tm_hour != lastCheckHour
    )
    {
        lastCheckHour = timeinfo.tm_hour;

        String body =
            "check<br>" +
            getTimestamp();

        queueEmail(
            "check",
            body
        );

        writeLog("Hourly check queued");
    }
}
void initEmailSystem()
{
    ethClient.stop();
    global_ssl = nullptr;
}
