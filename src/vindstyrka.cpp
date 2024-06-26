#include <Arduino.h>
#include "credentials.h"
#include <SensirionI2CSen5x.h>
#include <Wire.h>
#include <ESP8266HTTPClient.h>
#include <ElegantOTA.h>

#define SERVER           "192.168.7.207:8086"  // address of influx database
#define WIFI_HOSTNAME    "vindstyrka"
#define RECORDING_PERIOD 5 * 60 * 1000  // record a sample measurement every five minutes, in milliseconds

class Blinker {
    const int           led = LED_BUILTIN;
    unsigned long       previousMillis;   // will store last time LED was updated
    const unsigned long interval = 1000;  // interval at which to blink (milliseconds)

  public:
    Blinker() : previousMillis (0) {
        pinMode (led, OUTPUT);
    }

    void blink() {
        digitalWrite (led, LOW);
        delay (100);
        digitalWrite (led, HIGH);
        delay (100);
    }

    void update() {
        unsigned long currentMillis = millis();
        if (currentMillis - previousMillis >= interval) {
            // Serial.println("blink");
            blink();
            previousMillis = currentMillis;
        }
    }
} Blinker;

SensirionI2CSen5x sen5x;

ELEGANTOTA_WEBSERVER server (80);

#define SERIAL_NUMBER_SIZE 32
char gSerialNumber[SERIAL_NUMBER_SIZE];

void setup() {
    // start serial port
    Serial.begin (115200);
    while (!Serial) delay (100);

    Serial.print ("Connecting WiFi");
    WiFi.setHostname (WIFI_HOSTNAME);
    WiFi.begin (WIFI_SSID, WIFI_PSK);  // defined in credentials.h
    WiFi.waitForConnectResult();       // so much neater than those stupid loops and dots
    Serial.println ("");
    Serial.print ("Connected to ");
    Serial.println (WIFI_SSID);
    Serial.print ("IP address: ");
    Serial.println (WiFi.localIP());

    Blinker.blink();

#if ELEGANTOTA_USE_ASYNC_WEBSERVER == 1
#define REQUEST_ARG AsyncWebServerRequest * request
#define DO_SEND     request->send
#else
#define REQUEST_ARG
#define DO_SEND server.send
#endif
    server.on ("/", [] (REQUEST_ARG) {
        String html = "";
        html += "<html>";
        html += WIFI_HOSTNAME;
        html += "<br/><a href='/reset'>Reset</a><br/><a href='/update'>Update</a></html>";
        DO_SEND (200, "text/html", html);
    });

    server.on ("/reset", [] (REQUEST_ARG) {
        DO_SEND (200, "text/html", "<html><head><meta http-equiv=\"Refresh\" content=\"0; url='/'\"/></head></html>");
        ESP.restart();
    });

    ElegantOTA.begin (&server);  // Start ElegantOTA
    server.begin();
    Serial.println ("HTTP server started");

    Wire.begin();

    sen5x.begin (Wire);

    uint16_t error;
    char     errorMessage[256];

    // get Serial Number
    error = sen5x.getSerialNumber ((unsigned char *)gSerialNumber, SERIAL_NUMBER_SIZE);
    if (error) {
        Serial.print ("Error trying to execute getSerialNumber(): ");
        errorToString (error, errorMessage, 256);
        Serial.println (errorMessage);
        sprintf (gSerialNumber, "unknown");
    } else {
        Serial.print ("SerialNumber:");
        Serial.println (gSerialNumber);
    }

    // Start Measurement
    error = sen5x.startMeasurement();
    if (error) {
        Serial.print ("Error trying to execute startMeasurement(): ");
        errorToString (error, errorMessage, 256);
        Serial.println (errorMessage);
    }
}

void record_to_database (char const * measurement, float value) {
    WiFiClient client;
    HTTPClient http;

    Serial.print ("[HTTP] begin...\n");
    http.begin (client, "http://" SERVER "/write?db=vindstyrka");
    http.addHeader ("Accept", "*/*");
    http.addHeader ("Content-Type", "application/json");

    Serial.print ("[HTTP] POST...\n");
    static char postval[256];
    sprintf (postval, "%s,serial_number=%s value=%f", measurement, gSerialNumber, value);
    Serial.println (postval);
    int httpCode = http.POST (postval);

    // httpCode will be negative on error
    if (httpCode > 0)
        Serial.printf ("[HTTP] POST... code: %d\n", httpCode);
    else
        Serial.printf ("[HTTP] POST... failed, error: %s\n", http.errorToString (httpCode).c_str());

    http.end();
}

static unsigned long previousMillis = 0UL;

void loop() {
    if (WiFi.status() != WL_CONNECTED)
        ESP.restart();

#if !(ELEGANTOTA_USE_ASYNC_WEBSERVER == 1)
    server.handleClient();
#endif
    ElegantOTA.loop();

    unsigned long currentMillis = millis();
    if (currentMillis - previousMillis < RECORDING_PERIOD)
        return;
    previousMillis = currentMillis;

    Blinker.blink();
    uint16_t error;
    char     errorMessage[256];

    // Read Measurement
    float massConcentrationPm1p0;
    float massConcentrationPm2p5;
    float massConcentrationPm4p0;
    float massConcentrationPm10p0;
    float ambientHumidity;
    float ambientTemperature;
    float vocIndex;
    float noxIndex;

    Serial.println ("\nReading sensors");
    error = sen5x.readMeasuredValues (massConcentrationPm1p0,
                                      massConcentrationPm2p5,
                                      massConcentrationPm4p0,
                                      massConcentrationPm10p0,
                                      ambientHumidity,
                                      ambientTemperature,
                                      vocIndex,
                                      noxIndex);

    if (error) {
        Serial.print ("Error trying to execute readMeasuredValues(): ");
        errorToString (error, errorMessage, 256);
        Serial.println (errorMessage);
    } else {
        Serial.print ("MassConcentrationPm1p0:");
        Serial.println (massConcentrationPm1p0);
        record_to_database ("mass_concentration_pm_1_0", massConcentrationPm1p0);

        Serial.print ("MassConcentrationPm2p5:");
        Serial.println (massConcentrationPm2p5);
        record_to_database ("mass_concentration_pm_2_5", massConcentrationPm2p5);

        Serial.print ("MassConcentrationPm4p0:");
        Serial.println (massConcentrationPm4p0);
        record_to_database ("mass_concentration_pm_4_0", massConcentrationPm4p0);

        Serial.print ("MassConcentrationPm10p0:");
        Serial.println (massConcentrationPm10p0);
        record_to_database ("mass_concentration_pm_10_0", massConcentrationPm10p0);

        Serial.print ("AmbientHumidity:");
        if (isnan (ambientHumidity))
            Serial.println ("n/a");
        else {
            Serial.println (ambientHumidity);
            record_to_database ("humidity", ambientHumidity);
        }

        Serial.print ("AmbientTemperature:");
        if (isnan (ambientTemperature))
            Serial.print ("n/a");
        else {
            Serial.println (ambientTemperature);
            record_to_database ("temperature", ambientTemperature);
        }

        Serial.print ("VocIndex:");
        if (isnan (vocIndex))
            Serial.print ("n/a");
        else {
            Serial.println (vocIndex);
            record_to_database ("voc_index", vocIndex);
        }

        Serial.print ("NoxIndex:");
        if (isnan (noxIndex))
            Serial.println ("n/a");
        else {
            Serial.println (noxIndex);
            record_to_database ("nox_index", noxIndex);
        }
    }
}
