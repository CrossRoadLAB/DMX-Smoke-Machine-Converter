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

// --- VARIABILI DI STATO PER EVITARE SPAM RADIO ---
bool fumoAttivo = false;
bool lightOffAttivo = false;
bool rossoAttivo = false;
bool verdeAttivo = false;
bool bluAttivo = false;
bool stroboAttivo = false;

// --- CODICI TELECOMANDO REALI --- 
const unsigned long CODICE_FUMO      = 1469186558;
const unsigned long CODICE_FUMO_OFF  = 1469187068;
const unsigned long CODICE_ROSSO     = 1469187323;
const unsigned long CODICE_VERDE     = 1469187578;
const unsigned long CODICE_BLU       = 1469187833;
const unsigned long CODICE_STROBO    = 1469190383;
const unsigned long CODICE_LIGHT_OFF = 1469195483; 

const int BIT_LENGTH = 32; 
const int PROTOCOLO  = 1;  

// --- FUNZIONE DI GENERAZIONE HTML ---
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
    
    // SEZIONE DMX
    html += "<p><strong>" + t_dmx_lbl + "</strong></p>";
    html += "<div class=\"val\">" + String(dmxBaseAddress) + "</div>";
    html += "<form action=\"/set-dmx\" method=\"GET\">";
    html += "<input type=\"number\" name=\"addr\" min=\"1\" max=\"507\" value=\"" + String(dmxBaseAddress) + "\" required>";
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
    // Inizializzazione Seriale a 115200 baud
    Serial.begin(115200);
    delay(500);
    Serial.println("\n\n====================================");
    Serial.println("  DMX to RF Converter Inizializzato ");
    Serial.println("====================================");

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
    language = preferences.getString("lang", "IT");

    Serial.print("Canale DMX Base: ");
    Serial.println(dmxBaseAddress);
    Serial.print("Rete Wi-Fi AP: ");
    Serial.println(wifiSSID);

    // --- SETUP DMX ---
    dmx_config_t dmx_config = DMX_CONFIG_DEFAULT;
    dmx_personality_t personalities[] = { {6, "Fogger RGB 6CH"} }; 
    dmx_driver_install(dmx_num, &dmx_config, personalities, 1);
    dmx_set_pin(dmx_num, -1, DMX_RX_PIN, -1); 

    // --- SETUP WI-FI E CAPTIVE PORTAL ---
    WiFi.mode(WIFI_AP);
    WiFi.softAP(wifiSSID.c_str(), wifiPASS.c_str());
    dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());

    // --- ROTTE WEB SERVER ---
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(200, "text/html", buildHTML());
    });

    server.on("/set-dmx", HTTP_GET, [](AsyncWebServerRequest *request){
        if (request->hasParam("addr")) {
            int newAddr = request->getParam("addr")->value().toInt();
            if (newAddr >= 1 && newAddr <= 507) {
                dmxBaseAddress = newAddr;
                preferences.putInt("dmx", dmxBaseAddress);
                Serial.print("! Nuovo Canale DMX Salvato: ");
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
    digitalWrite(LED_ONBOARD, HIGH); // Segnala fine boot (acceso)
    Serial.println("Server Web Avviato. In attesa di segnale DMX...");
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

            // Lettura 6 canali
            int valFumo     = dmx_data[dmxIndex];      
            int valLightOff = dmx_data[dmxIndex + 1]; 
            int valRosso    = dmx_data[dmxIndex + 2]; 
            int valVerde    = dmx_data[dmxIndex + 3]; 
            int valBlu      = dmx_data[dmxIndex + 4]; 
            int valStrobo   = dmx_data[dmxIndex + 5];

            // ==========================================
            // 1. CANALE 1: FUMO (Push to Hold)
            // ==========================================
            if (valFumo > 128) {
                if (!fumoAttivo) {
                    Serial.print("[DMX CH1] Fumo ON  -> Trasmetto RF: ");
                    Serial.println(CODICE_FUMO);
                    mySwitch.send(CODICE_FUMO, BIT_LENGTH);
                    fumoAttivo = true;
                    delay(50); 
                }
            } else {
                if (fumoAttivo) {
                    Serial.print("[DMX CH1] Fumo OFF -> Trasmetto RF: ");
                    Serial.println(CODICE_FUMO_OFF);
                    mySwitch.send(CODICE_FUMO_OFF, BIT_LENGTH);
                    fumoAttivo = false;
                    delay(50);
                }
            }

            // ==========================================
            // 2. CANALE 2: LUCI OFF (Flash per spegnere)
            // ==========================================
            if (valLightOff > 128) {
                if (!lightOffAttivo) {
                    Serial.print("[DMX CH2] Luci OFF richiesto -> Trasmetto RF: ");
                    Serial.println(CODICE_LIGHT_OFF);
                    mySwitch.send(CODICE_LIGHT_OFF, BIT_LENGTH);
                    lightOffAttivo = true;
                    delay(50);
                }
            } else {
                lightOffAttivo = false; // Reset trigger
            }

            // ==========================================
            // 3/4/5/6. CANALI COLORI E STROBO (Trigger)
            // ==========================================
            bool newR = (valRosso > 128);
            bool newG = (valVerde > 128);
            bool newB = (valBlu > 128);
            bool newS = (valStrobo > 128);

            // Verifica se ALMENO UN fader è stato abbassato rispetto al ciclo precedente
            bool anyLightTurnedOff = false;
            if (rossoAttivo && !newR) anyLightTurnedOff = true;
            if (verdeAttivo && !newG) anyLightTurnedOff = true;
            if (bluAttivo && !newB)   anyLightTurnedOff = true;
            if (stroboAttivo && !newS) anyLightTurnedOff = true;

            if (anyLightTurnedOff) {
                Serial.print("[DMX CH3-6] Rilevato spegnimento fader -> Trasmetto GLOBAL OFF: ");
                Serial.println(CODICE_LIGHT_OFF);
                mySwitch.send(CODICE_LIGHT_OFF, BIT_LENGTH);
                delay(50);
                
                // Ricontrolla e riaccende immediatamente le luci che hanno il fader ancora su
                if (newR) { 
                    Serial.print("   -> Riaccendo ROSSO: "); Serial.println(CODICE_ROSSO);
                    mySwitch.send(CODICE_ROSSO, BIT_LENGTH); delay(50); 
                }
                if (newG) { 
                    Serial.print("   -> Riaccendo VERDE: "); Serial.println(CODICE_VERDE);
                    mySwitch.send(CODICE_VERDE, BIT_LENGTH); delay(50); 
                }
                if (newB) { 
                    Serial.print("   -> Riaccendo BLU: "); Serial.println(CODICE_BLU);
                    mySwitch.send(CODICE_BLU, BIT_LENGTH); delay(50); 
                }
                if (newS) { 
                    Serial.print("   -> Riaccendo STROBO: "); Serial.println(CODICE_STROBO);
                    mySwitch.send(CODICE_STROBO, BIT_LENGTH); delay(50); 
                }
            } else {
                // Nessuna luce è stata spenta, controlla solo se ci sono nuove accensioni
                if (!rossoAttivo && newR) { 
                    Serial.print("[DMX CH3] Rosso ON -> Trasmetto RF: "); Serial.println(CODICE_ROSSO);
                    mySwitch.send(CODICE_ROSSO, BIT_LENGTH); delay(50); 
                }
                if (!verdeAttivo && newG) { 
                    Serial.print("[DMX CH4] Verde ON -> Trasmetto RF: "); Serial.println(CODICE_VERDE);
                    mySwitch.send(CODICE_VERDE, BIT_LENGTH); delay(50); 
                }
                if (!bluAttivo && newB)   { 
                    Serial.print("[DMX CH5] Blu ON -> Trasmetto RF: "); Serial.println(CODICE_BLU);
                    mySwitch.send(CODICE_BLU, BIT_LENGTH); delay(50); 
                }
                if (!stroboAttivo && newS) { 
                    Serial.print("[DMX CH6] Strobo ON -> Trasmetto RF: "); Serial.println(CODICE_STROBO);
                    mySwitch.send(CODICE_STROBO, BIT_LENGTH); delay(50); 
                }
            }

            // Aggiorna lo stato in memoria per il ciclo successivo
            rossoAttivo = newR;
            verdeAttivo = newG;
            bluAttivo = newB;
            stroboAttivo = newS;
        }
    }
    
    // ==========================================
    // WATCHDOG DI SICUREZZA
    // ==========================================
    if (millis() - lastDmxPacketTime > 1000) {
        digitalWrite(LED_ONBOARD, HIGH); // LED acceso fisso = Assenza DMX
        
        if (fumoAttivo) {
            Serial.println("!!! ALLARME: Segnale DMX Perso !!! -> Interrompo erogazione FUMO");
            Serial.print("Trasmetto RF: ");
            Serial.println(CODICE_FUMO_OFF);
            mySwitch.send(CODICE_FUMO_OFF, BIT_LENGTH);
            fumoAttivo = false;
            delay(50);
        }
    }
    
    delay(1); 
}
