#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>
#include <DNSServer.h>
#include <esp_dmx.h>
#include <RCSwitch.h>

// --- PINOUT ---
#define PIN_6N137_TRIGGER 3
#define LED_ONBOARD 8
#define DMX_RX_PIN 20

// --- DMX CONFIG ---
dmx_port_t dmx_num = DMX_NUM_1;
uint8_t dmx_data[DMX_PACKET_SIZE_MAX];
unsigned long lastDmxPacketTime = 0;

// --- NETWORK OBJECTS ---
AsyncWebServer server(80);
DNSServer dnsServer;
Preferences preferences;
RCSwitch mySwitch = RCSwitch();

const byte DNS_PORT = 53;

// --- STATE AND MEMORY VARIABLES ---
int dmxBaseAddress = 1;
String wifiSSID;
String wifiPASS;
String language; // "IT" or "EN"

// --- STATE VARIABLES TO PREVENT RF SPAM ---
bool fumoAttivo = false;
bool lightOffAttivo = false;
bool rossoAttivo = false;
bool verdeAttivo = false;
bool bluAttivo = false;
bool stroboAttivo = false;

// --- REAL REMOTE CONTROL CODES --- 
const unsigned long CODICE_FUMO      = 1469186558;
const unsigned long CODICE_FUMO_OFF  = 1469187068;
const unsigned long CODICE_ROSSO     = 1469187323;
const unsigned long CODICE_VERDE     = 1469187578;
const unsigned long CODICE_BLU       = 1469187833;
const unsigned long CODICE_STROBO    = 1469190383;
const unsigned long CODICE_LIGHT_OFF = 1469195483; 

const int BIT_LENGTH = 32; 
const int PROTOCOLO  = 1;  

// --- HTML GENERATION FUNCTION ---
String buildHTML() {
    bool isIT = (language == "IT");
    
    String t_title = isIT ? "IMPOSTAZIONI MACCHINA DEL FUMO" : "SMOKE MACHINE SETTINGS";
    String t_dmx_lbl = isIT ? "CANALE DMX ATTUALE:" : "CURRENT DMX CHANNEL:";
    String t_btn_save = isIT ? "SALVA DMX" : "SAVE DMX";
    String t_wifi_lbl = isIT ? "IMPOSTAZIONI RETE WI-FI" : "WI-FI NETWORK SETTINGS";
    String t_btn_wifi = isIT ? "AGGIORNA WI-FI E RIAVVIA" : "UPDATE WI-FI & REBOOT";
    String t_lang_lbl = isIT ? "LINGUA INTERFACCIA" : "INTERFACE LANGUAGE";
    
    String html = "<!DOCTYPE html><html><head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">";
    html += "<title>DMX Smoke Machine Converter</title>";
    html += "<style>body{font-family:monospace;background:#fff;color:#000;text-align:center;padding:10px;margin:0;}";
    html += ".container{border:4px solid #000;padding:20px;max-width:400px;margin:0 auto;box-shadow: 8px 8px 0px #000;}";
    html += "h1{font-size:24px;text-transform:uppercase;border-bottom:4px solid #000;padding-bottom:10px;}";
    html += "h2{font-size:18px;margin-top:20px;background:#000;color:#fff;padding:5px;}";
    html += ".val{font-size:48px;font-weight:bold;margin:10px 0;}";
    html += "input{font-family:monospace;font-size:18px;width:calc(100% - 20px);padding:8px;border:2px solid #000;margin-bottom:10px;box-sizing:border-box;}";
    html += "button{font-family:monospace;font-size:16px;font-weight:bold;width:100%;padding:12px;background:#000;color:#fff;border:none;cursor:pointer;text-transform:uppercase;}";
    html += "button:active{background:#444;}";
    html += ".lang-btn{width:48%;display:inline-block;}";
    html += "</style></head><body>";
    
    html += "<div class=\"container\">";
    html += "<h1>" + t_title + "</h1>";
    
    // DMX SECTION
    html += "<p><strong>" + t_dmx_lbl + "</strong></p>";
    html += "<div class=\"val\">" + String(dmxBaseAddress) + "</div>";
    html += "<form action=\"/set-dmx\" method=\"GET\">";
    html += "<input type=\"number\" name=\"addr\" min=\"1\" max=\"507\" value=\"" + String(dmxBaseAddress) + "\" required>";
    html += "<button type=\"submit\">" + t_btn_save + "</button>";
    html += "</form>";

    // WI-FI SECTION
    html += "<h2>" + t_wifi_lbl + "</h2>";
    html += "<form action=\"/set-wifi\" method=\"GET\">";
    html += "<input type=\"text\" name=\"ssid\" placeholder=\"SSID Name\" value=\"" + wifiSSID + "\" required>";
    html += "<input type=\"text\" name=\"pass\" placeholder=\"Password (min 8 char)\" value=\"" + wifiPASS + "\">";
    html += "<button type=\"submit\">" + t_btn_wifi + "</button>";
    html += "</form>";

    // LANGUAGE SECTION
    html += "<h2>" + t_lang_lbl + "</h2>";
    html += "<form action=\"/set-lang\" method=\"GET\">";
    html += "<button class=\"lang-btn\" type=\"submit\" name=\"l\" value=\"IT\" " + String(isIT ? "style='background:#ccc;color:#000;'" : "") + ">ITALIANO</button> ";
    html += "<button class=\"lang-btn\" type=\"submit\" name=\"l\" value=\"EN\" " + String(!isIT ? "style='background:#ccc;color:#000;'" : "") + ">ENGLISH</button>";
    html += "</form>";

    html += "</div></body></html>";
    return html;
}

void setup() {
    // Initialize Serial at 115200 baud
    Serial.begin(115200);
    delay(500);
    Serial.println("\n\n====================================");
    Serial.println("  DMX to RF Converter Initialized   ");
    Serial.println("====================================");

    pinMode(LED_ONBOARD, OUTPUT);
    digitalWrite(LED_ONBOARD, LOW); 
    pinMode(PIN_6N137_TRIGGER, OUTPUT);
    digitalWrite(PIN_6N137_TRIGGER, HIGH); 

    mySwitch.enableTransmit(PIN_6N137_TRIGGER);
    mySwitch.setProtocol(PROTOCOLO);

    // --- MEMORY LOADING ---
    preferences.begin("fogger-cfg", false);
    dmxBaseAddress = preferences.getInt("dmx", 1);
    wifiSSID = preferences.getString("ssid", "SMOKE_MACHINE");
    wifiPASS = preferences.getString("pass", "smoke123");
    language = preferences.getString("lang", "IT");

    Serial.print("Base DMX Channel: ");
    Serial.println(dmxBaseAddress);
    Serial.print("Wi-Fi AP Network: ");
    Serial.println(wifiSSID);

    // --- DMX SETUP ---
    dmx_config_t dmx_config = DMX_CONFIG_DEFAULT;
    dmx_personality_t personalities[] = { {6, "Fogger RGB 6CH"} }; 
    dmx_driver_install(dmx_num, &dmx_config, personalities, 1);
    dmx_set_pin(dmx_num, -1, DMX_RX_PIN, -1); 

    // --- WI-FI AND CAPTIVE PORTAL SETUP ---
    WiFi.mode(WIFI_AP);
    WiFi.softAP(wifiSSID.c_str(), wifiPASS.c_str());
    dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());

    // --- WEB SERVER ROUTES ---
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(200, "text/html", buildHTML());
    });

    server.on("/set-dmx", HTTP_GET, [](AsyncWebServerRequest *request){
        if (request->hasParam("addr")) {
            int newAddr = request->getParam("addr")->value().toInt();
            if (newAddr >= 1 && newAddr <= 507) {
                dmxBaseAddress = newAddr;
                preferences.putInt("dmx", dmxBaseAddress);
                Serial.print("! New DMX Channel Saved: ");
                Serial.println(dmxBaseAddress);
            }
        }
        request->redirect("/");
    });

    server.on("/set-wifi", HTTP_GET, [](AsyncWebServerRequest *request){
        if (request->hasParam("ssid")) {
            wifiSSID = request->getParam("ssid")->value();
            preferences.putString("ssid", wifiSSID);
        }
        if (request->hasParam("pass")) {
            String tempPass = request->getParam("pass")->value();
            if(tempPass.length() >= 8 || tempPass.length() == 0) {
                wifiPASS = tempPass;
                preferences.putString("pass", wifiPASS);
            }
        }
        request->send(200, "text/html", "<h1 style='font-family:monospace;text-align:center;'>Wi-Fi Updated. Rebooting...</h1>");
        delay(1000);
        ESP.restart();
    });

    server.on("/set-lang", HTTP_GET, [](AsyncWebServerRequest *request){
        if (request->hasParam("l")) {
            language = request->getParam("l")->value();
            preferences.putString("lang", language);
        }
        request->redirect("/");
    });

    server.onNotFound([](AsyncWebServerRequest *request){
        String captiveUrl = "http://" + WiFi.softAPIP().toString() + "/";
        request->redirect(captiveUrl);
    });

    server.begin();
    digitalWrite(LED_ONBOARD, HIGH); // Signal end of boot (ON)
    Serial.println("Web Server Started. Waiting for DMX signal...");
}

void loop() {
    dnsServer.processNextRequest();

    dmx_packet_t packet;
    if (dmx_receive(dmx_num, &packet, 0)) {
        if (packet.err == DMX_OK) {
            lastDmxPacketTime = millis(); 
            digitalWrite(LED_ONBOARD, LOW); 
            
            dmx_read(dmx_num, dmx_data, packet.size);
            
            int dmxIndex = dmxBaseAddress;

            // Reading 6 channels
            int valFumo     = dmx_data[dmxIndex];      
            int valLightOff = dmx_data[dmxIndex + 1]; 
            int valRosso    = dmx_data[dmxIndex + 2]; 
            int valVerde    = dmx_data[dmxIndex + 3]; 
            int valBlu      = dmx_data[dmxIndex + 4]; 
            int valStrobo   = dmx_data[dmxIndex + 5];

            // ==========================================
            // 1. CHANNEL 1: SMOKE (Push to Hold)
            // ==========================================
            if (valFumo > 128) {
                if (!fumoAttivo) {
                    Serial.print("[DMX CH1] Smoke ON  -> Transmitting RF: ");
                    Serial.println(CODICE_FUMO);
                    mySwitch.send(CODICE_FUMO, BIT_LENGTH);
                    fumoAttivo = true;
                    delay(50); 
                }
            } else {
                if (fumoAttivo) {
                    Serial.print("[DMX CH1] Smoke OFF -> Transmitting RF: ");
                    Serial.println(CODICE_FUMO_OFF);
                    mySwitch.send(CODICE_FUMO_OFF, BIT_LENGTH);
                    fumoAttivo = false;
                    delay(50);
                }
            }

            // ==========================================
            // 2. CHANNEL 2: LIGHTS OFF (Flash to turn off)
            // ==========================================
            if (valLightOff > 128) {
                if (!lightOffAttivo) {
                    Serial.print("[DMX CH2] Lights OFF requested -> Transmitting RF: ");
                    Serial.println(CODICE_LIGHT_OFF);
                    mySwitch.send(CODICE_LIGHT_OFF, BIT_LENGTH);
                    lightOffAttivo = true;
                    delay(50);
                }
            } else {
                lightOffAttivo = false; // Reset trigger
            }

            // ==========================================
            // 3/4/5/6. COLOR AND STROBE CHANNELS (Trigger)
            // ==========================================
            bool newR = (valRosso > 128);
            bool newG = (valVerde > 128);
            bool newB = (valBlu > 128);
            bool newS = (valStrobo > 128);

            // Check if AT LEAST ONE fader was lowered compared to the previous cycle
            bool anyLightTurnedOff = false;
            if (rossoAttivo && !newR) anyLightTurnedOff = true;
            if (verdeAttivo && !newG) anyLightTurnedOff = true;
            if (bluAttivo && !newB)   anyLightTurnedOff = true;
            if (stroboAttivo && !newS) anyLightTurnedOff = true;

            if (anyLightTurnedOff) {
                Serial.print("[DMX CH3-6] Fader turn off detected -> Transmitting GLOBAL OFF: ");
                Serial.println(CODICE_LIGHT_OFF);
                mySwitch.send(CODICE_LIGHT_OFF, BIT_LENGTH);
                delay(50);
                
                // Re-check and immediately turn back on the lights that still have the fader up
                if (newR) { 
                    Serial.print("   -> Turning RED back on: "); Serial.println(CODICE_ROSSO);
                    mySwitch.send(CODICE_ROSSO, BIT_LENGTH); delay(50); 
                }
                if (newG) { 
                    Serial.print("   -> Turning GREEN back on: "); Serial.println(CODICE_VERDE);
                    mySwitch.send(CODICE_VERDE, BIT_LENGTH); delay(50); 
                }
                if (newB) { 
                    Serial.print("   -> Turning BLUE back on: "); Serial.println(CODICE_BLU);
                    mySwitch.send(CODICE_BLU, BIT_LENGTH); delay(50); 
                }
                if (newS) { 
                    Serial.print("   -> Turning STROBE back on: "); Serial.println(CODICE_STROBO);
                    mySwitch.send(CODICE_STROBO, BIT_LENGTH); delay(50); 
                }
            } else {
                // No light was turned off, only check if there are new turn-ons
                if (!rossoAttivo && newR) { 
                    Serial.print("[DMX CH3] Red ON -> Transmitting RF: "); Serial.println(CODICE_ROSSO);
                    mySwitch.send(CODICE_ROSSO, BIT_LENGTH); delay(50); 
                }
                if (!verdeAttivo && newG) { 
                    Serial.print("[DMX CH4] Green ON -> Transmitting RF: "); Serial.println(CODICE_VERDE);
                    mySwitch.send(CODICE_VERDE, BIT_LENGTH); delay(50); 
                }
                if (!bluAttivo && newB)   { 
                    Serial.print("[DMX CH5] Blue ON -> Transmitting RF: "); Serial.println(CODICE_BLU);
                    mySwitch.send(CODICE_BLU, BIT_LENGTH); delay(50); 
                }
                if (!stroboAttivo && newS) { 
                    Serial.print("[DMX CH6] Strobe ON -> Transmitting RF: "); Serial.println(CODICE_STROBO);
                    mySwitch.send(CODICE_STROBO, BIT_LENGTH); delay(50); 
                }
            }

            // Update the state in memory for the next cycle
            rossoAttivo = newR;
            verdeAttivo = newG;
            bluAttivo = newB;
            stroboAttivo = newS;
        }
    }
    
    // ==========================================
    // SAFETY WATCHDOG
    // ==========================================
    if (millis() - lastDmxPacketTime > 1000) {
        digitalWrite(LED_ONBOARD, HIGH); // Solid LED ON = DMX missing
        
        if (fumoAttivo) {
            Serial.println("!!! ALARM: DMX Signal Lost !!! -> Stopping SMOKE output");
            Serial.print("Transmitting RF: ");
            Serial.println(CODICE_FUMO_OFF);
            mySwitch.send(CODICE_FUMO_OFF, BIT_LENGTH);
            fumoAttivo = false;
            delay(50);
        }
    }
    
    delay(1); 
}
