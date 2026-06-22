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

// --- OGGETTI DI RETE ---
AsyncWebServer server(80);
DNSServer dnsServer;
Preferences preferences;
RCSwitch mySwitch = RCSwitch();

const byte DNS_PORT = 53;

// --- VARIABILI DI STATO E MEMORIA ---
int dmxBaseAddress = 1;
String wifiSSID;
String wifiPASS;
String language; // "IT" o "EN"

// --- CODICI TELECOMANDO ---
const unsigned long CODICE_FUMO   = 1234567; 
const unsigned long CODICE_ROSSO  = 1234568;
const unsigned long CODICE_VERDE  = 1234569;
const unsigned long CODICE_BLU    = 1234570;
const int BIT_LENGTH = 24; 
const int PROTOCOLO  = 1;  

// --- FUNZIONE DI GENERAZIONE HTML (Alto Contrasto / B&W) ---
String buildHTML() {
    bool isIT = (language == "IT");
    
    // Testi localizzati
    String t_title = isIT ? "IMPOSTAZIONI MACCHINA DEL FUMO" : "SMOKE MACHINE SETTINGS";
    String t_dmx_lbl = isIT ? "CANALE DMX ATTUALE:" : "CURRENT DMX CHANNEL:";
    String t_btn_save = isIT ? "SALVA DMX" : "SAVE DMX";
    String t_wifi_lbl = isIT ? "IMPOSTAZIONI RETE WI-FI" : "WI-FI NETWORK SETTINGS";
    String t_btn_wifi = isIT ? "AGGIORNA WI-FI E RIAVVIA" : "UPDATE WI-FI & REBOOT";
    String t_lang_lbl = isIT ? "LINGUA INTERFACCIA" : "INTERFACE LANGUAGE";
    
    String html = "<!DOCTYPE html><html><head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">";
    html += "<title>DMX Smoke Machine Converter</title>";
    // Stile ad alto contrasto, font monospace, bordi spessi
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
    
    // SEZIONE DMX
    html += "<p><strong>" + t_dmx_lbl + "</strong></p>";
    html += "<div class=\"val\">" + String(dmxBaseAddress) + "</div>";
    html += "<form action=\"/set-dmx\" method=\"GET\">";
    html += "<input type=\"number\" name=\"addr\" min=\"1\" max=\"509\" value=\"" + String(dmxBaseAddress) + "\" required>";
    html += "<button type=\"submit\">" + t_btn_save + "</button>";
    html += "</form>";

    // SEZIONE WI-FI
    html += "<h2>" + t_wifi_lbl + "</h2>";
    html += "<form action=\"/set-wifi\" method=\"GET\">";
    html += "<input type=\"text\" name=\"ssid\" placeholder=\"SSID Name\" value=\"" + wifiSSID + "\" required>";
    html += "<input type=\"text\" name=\"pass\" placeholder=\"Password (min 8 char)\" value=\"" + wifiPASS + "\">";
    html += "<button type=\"submit\">" + t_btn_wifi + "</button>";
    html += "</form>";

    // SEZIONE LINGUA
    html += "<h2>" + t_lang_lbl + "</h2>";
    html += "<form action=\"/set-lang\" method=\"GET\">";
    html += "<button class=\"lang-btn\" type=\"submit\" name=\"l\" value=\"IT\" " + String(isIT ? "style='background:#ccc;color:#000;'" : "") + ">ITALIANO</button> ";
    html += "<button class=\"lang-btn\" type=\"submit\" name=\"l\" value=\"EN\" " + String(!isIT ? "style='background:#ccc;color:#000;'" : "") + ">ENGLISH</button>";
    html += "</form>";

    html += "</div></body></html>";
    return html;
}

void setup() {
    pinMode(LED_ONBOARD, OUTPUT);
    digitalWrite(LED_ONBOARD, LOW); 
    pinMode(PIN_6N137_TRIGGER, OUTPUT);
    digitalWrite(PIN_6N137_TRIGGER, HIGH); 

    mySwitch.enableTransmit(PIN_6N137_TRIGGER);
    mySwitch.setProtocol(PROTOCOLO);

    // --- CARICAMENTO MEMORIA ---
    preferences.begin("fogger-cfg", false);
    dmxBaseAddress = preferences.getInt("dmx", 1);
    wifiSSID = preferences.getString("ssid", "SMOKE_MACHINE");
    wifiPASS = preferences.getString("pass", "smoke123");
    language = preferences.getString("lang", "IT"); // IT o EN

    // --- SETUP DMX ---
    dmx_config_t dmx_config = DMX_CONFIG_DEFAULT;
    dmx_personality_t personalities[] = { {4, "Fogger RGB"} };
    dmx_driver_install(dmx_num, &dmx_config, personalities, 1);
    dmx_set_pin(dmx_num, -1, DMX_RX_PIN, -1); 

    // --- SETUP WI-FI ---
    WiFi.mode(WIFI_AP);
    WiFi.softAP(wifiSSID.c_str(), wifiPASS.c_str());

    // --- SETUP CAPTIVE PORTAL (DNS) ---
    // Dirotta TUTTE le richieste DNS (*) verso l'IP dell'ESP32
    dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());

    // --- ROTTE WEB SERVER ---
    
    // Pagina principale
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(200, "text/html", buildHTML());
    });

    // Endpoint cambio DMX
    server.on("/set-dmx", HTTP_GET, [](AsyncWebServerRequest *request){
        if (request->hasParam("addr")) {
            int newAddr = request->getParam("addr")->value().toInt();
            if (newAddr >= 1 && newAddr <= 509) {
                dmxBaseAddress = newAddr;
                preferences.putInt("dmx", dmxBaseAddress);
            }
        }
        request->redirect("/");
    });

    // Endpoint cambio Wi-Fi
    server.on("/set-wifi", HTTP_GET, [](AsyncWebServerRequest *request){
        if (request->hasParam("ssid")) {
            wifiSSID = request->getParam("ssid")->value();
            preferences.putString("ssid", wifiSSID);
        }
        if (request->hasParam("pass")) {
            String tempPass = request->getParam("pass")->value();
            if(tempPass.length() >= 8 || tempPass.length() == 0) { // La pass WPA2 deve essere min 8 o vuota
                wifiPASS = tempPass;
                preferences.putString("pass", wifiPASS);
            }
        }
        request->send(200, "text/html", "<h1 style='font-family:monospace;text-align:center;'>Wi-Fi Updated. Rebooting Fogger...</h1>");
        delay(1000);
        ESP.restart(); // Riavvia la scheda per applicare il nuovo Wi-Fi
    });

    // Endpoint cambio Lingua
    server.on("/set-lang", HTTP_GET, [](AsyncWebServerRequest *request){
        if (request->hasParam("l")) {
            language = request->getParam("l")->value();
            preferences.putString("lang", language);
        }
        request->redirect("/");
    });

    // GESTIONE CAPTIVE PORTAL: Se il dispositivo cerca di visitare google.com, rimandalo alla root
    server.onNotFound([](AsyncWebServerRequest *request){
        request->redirect("/");
    });

    server.begin();
    digitalWrite(LED_ONBOARD, HIGH); // Spegni LED fine boot
}

void loop() {
    // IMPORTANTE: Mantiene vivo il finto server DNS per far comparire il popup
    dnsServer.processNextRequest();

    dmx_packet_t packet;
    if (dmx_receive(dmx_num, &packet, 0)) {
        if (packet.err == DMX_OK) {
            lastDmxPacketTime = millis(); 
            digitalWrite(LED_ONBOARD, LOW); // LED Segnale OK
            
            dmx_read(dmx_num, dmx_data, packet.size);
            
            int valFumo  = dmx_data[dmxBaseAddress];     
            int valRosso = dmx_data[dmxBaseAddress + 1]; 
            int valVerde = dmx_data[dmxBaseAddress + 2]; 
            int valBlu   = dmx_data[dmxBaseAddress + 3]; 

            if (valFumo > 128) {
                mySwitch.send(CODICE_FUMO, BIT_LENGTH);
                delay(20); 
            }
            if (valRosso > 128) {
                mySwitch.send(CODICE_ROSSO, BIT_LENGTH);
                delay(20);
            } else if (valVerde > 128) {
                mySwitch.send(CODICE_VERDE, BIT_LENGTH);
                delay(20);
            } else if (valBlu > 128) {
                mySwitch.send(CODICE_BLU, BIT_LENGTH);
                delay(20);
            }
        }
    }
    
    if (millis() - lastDmxPacketTime > 1000) {
        digitalWrite(LED_ONBOARD, HIGH); // Nessun segnale
    }
    
    delay(1); 
}
