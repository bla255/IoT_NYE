#include <Arduino.h>
#include "DHT.h"
#include <IRremote.hpp>
#include <LiquidCrystal.h>
#include <WiFi.h>
#include "time.h"

#define DHTPIN 4 //Hoszenzor a 4-es PIN-re illesztve.
#define DHTTYPE DHT11 //Olcso DHT11-es típus.
DHT dht(DHTPIN, DHTTYPE);
const unsigned long DHT_INTERVAL = 2000; //2000msecenkent tortenik a lekerdezes
unsigned long lastDhtMs = 0; //timer, utolso Dht lekerdezes ota eltelt ido
float lastH = NAN, lastT = NAN, lastHI = NAN; //Elokeszitjuk a valtozokat. Utolso Humidity, Temperature, HeatIndex (Para,Ho,Hoerzet)

#define IR_RECEIVER_PIN 34 //Taviranyito IR vevoje a 34-es PINre teve
#define DEBOUNCE_TIME 400 //400msec debounce timer
uint64_t lastCode = 0; //utolso osszeszedett hex kod IR felol
unsigned long lastIrMs = 0; //utolso IR vetel ido Msec
const int PIN_PIR = 26, PIN_LED_ACT = 27, PIN_LED_NACT = 14; //PIR szenzor a Pin 26-ra kotve, Activity (mozgas erzekelve) led a 27-es pinre, 14es pinre pedig ha nincs mozgas.
bool pirActive = false, pirPrev = LOW; //allapotvaltozok, alap setupkent nincs mozgas ugy veszem, tehat az elozo pir is low allapotba lesz, ne egjen a mozgas ledje.


//RS, EN, D4, D5, D6, D7
LiquidCrystal lcd(22, 21, 5, 18, 23, 19); //16X2 LCD  PIN parameterek, letrehozzuk az objektumot.

const char* WIFI_SSID = "WIFI_SSID"; //WIFI SSID
const char* WIFI_PASS = "WIFI_JELSZO"; //WIFI JELSZO
const char* TZ_EU_BUD = "CET-1CEST,M3.5.0/2,M10.5.0/3"; //NTP timezone POSIX idozona, Budapest UTC+1 es a nyari/teli idoszamitas parameterei.
const char* NTP1 = "pool.ntp.org"; //elso NTp szerver
const char* NTP2 = "time.nist.gov"; //backup NTP szerver

bool showPIRactive = false, debugMode = false, scrollingActive = false; //ujabb allapotvaltozok ahhoz, hogy a PIR aktiv-e, a debugmode Iranyitorol #-al bekapcsolva van-e, illetve az LCDn a scrolling aktiv-e.
int currentMenu = 1; //Default menunk az 1-es.
unsigned long lastScrollMs = 0; //Utolso scroll ota eltelt ido.
String scrollTextBuffer = ""; //Alapertelmezeskent az LCD scrolltextbufferje legyen ures.
int scrollOffset = 0; //Tehat az offset is 0 legyen, gorgetes/eltolas aktualis pozicioja.

void lcdPrint(String line1, String line2) { 
  String fullText = line1 + " " + line2;
  scrollTextTwoLines(fullText);
  Serial.println("--- " + line1 + " | " + line2 + " ---");
} 
//Ket sort adunk be a paramterekbe, ezeket osszefuzi es atadja a scrollTextTwoLines eljarasnak + Serialra is kiiratjuk.


void scrollTextTwoLines(String fullText) {
  const int width = 16;
  if (fullText.length() <= 32) {
    String padded = fullText;
    while (padded.length() < 32) padded += " ";
    lcd.clear();
    lcd.setCursor(0, 0); lcd.print(padded.substring(0, 16));
    lcd.setCursor(0, 1); lcd.print(padded.substring(16));
    return;
  }
  scrollingActive = true;
  scrollTextBuffer = fullText;
  scrollOffset = 0;
}
//Ha a szovegunk kifer a 2X16 karakterbe, akkor kiirja ket sorba. Ha hosszabb, akkor bekapcsoljuk a gorgetes offsetet es elkezdjuk bufferbe tolni.

void handleScroll() {
  if (!scrollingActive) return;
  unsigned long now = millis();
  if (now - lastScrollMs < 800) return;
  lastScrollMs = now;

  const int width = 16;
  String line1 = "", line2 = "";
  
  for (int i = 0; i < width; i++) {
    line1 += scrollTextBuffer[(scrollOffset + i) % scrollTextBuffer.length()];
    line2 += scrollTextBuffer[(scrollOffset + width + i) % scrollTextBuffer.length()];
  }
  lcd.setCursor(0, 0); lcd.print(line1);
  lcd.setCursor(0, 1); lcd.print(line2);
  scrollOffset = (scrollOffset + 1) % scrollTextBuffer.length();
}
//Ha aktiv a gorgetesunk, akkor 800msecenkent tolunk az LCD-n 1 karaktert mindket soron.

void showHumTemp() {
  if (isnan(lastH) || isnan(lastT) || isnan(lastHI)) {
    lcdPrint("Nincs adat", "DHT ujraproba");
    return;
  }
  char l1[17], l2[17];
  snprintf(l1, sizeof(l1), "Para:%2.0f%% Ho:%4.1fC", lastH, lastT);
  snprintf(l2, sizeof(l2), "Hoerzet:%4.1fC", lastHI);
  lcdPrint(l1, l2);
}
//A hoszenzor ertekeit ontjuk formaba. Ha nincs adat akkor -> LCD sor1 Nincs adat, LCD sor2 -> DHT ujraproba.

void showPIR() {
  lcdPrint("PIR: " + String(pirActive ? "Mozgas" : "Nincs"), pirActive ? "Mozgas" : "erzekelve");
}
//Pir allapotjelzo, mozog vagy sem.

void showDateTime() {
  struct tm ti;
  if (getLocalTime(&ti)) {
    char l1[17], l2[17];
    strftime(l1, sizeof(l1), "%Y-%m-%d", &ti);
    strftime(l2, sizeof(l2), "%H:%M", &ti);
    lcdPrint(l1, l2);
  } else {
    lcdPrint("Nincs ido", "Ellenorizd WIFI");
  }
}
//Datum - ido kiiratas.

void showMenu() {
  String menuText = " 1:Homerseklet 2:PIR 3:Datum 4:Menu 5:WiFi 6:Sys";
  scrollTextTwoLines(menuText);
  Serial.println("--------------------");
  Serial.println("FUNKCIOK LISTAJA (4)");
  Serial.println("--------------------");
  Serial.println("  1: Ho   (Homerseklet)");
  Serial.println("  2: PIR  (Mozgas erzekelo)");
  Serial.println("  3: Datum/Ido (NTP)");
  Serial.println("  4: Funkciok listaja");
  Serial.println("  5: WiFi SSID + Jelero");
  Serial.println("  6: Rendszer info");
  Serial.println("--------------------");
}
//menu kiiratas Serialra es LCD-re.

void showSystemInfo() {
  uint32_t usedHeap = (ESP.getHeapSize() - ESP.getFreeHeap()) / 1024;
  uint32_t totalHeap = ESP.getHeapSize() / 1024;
  uint32_t cpuFreq = ESP.getCpuFreqMHz();
  String sysText = "Mem:" + String(usedHeap) + "/" + String(totalHeap) + "KB CPU:" + String(cpuFreq) + "MHz";
  scrollTextTwoLines(sysText);
  Serial.printf("Mem:%lu/%luKB CPU:%luMHz\n", usedHeap, totalHeap, cpuFreq);
}
//LCD+Serial hardverinfo.

void showWiFiInfo() {
  String ssid = WiFi.SSID();
  int quality = map(WiFi.RSSI(), -100, -50, 0, 100);
  quality = constrain(quality, 0, 100);
  String wifiText = "SSID:" + ssid + " Jelero:" + String(quality) + "%";
  scrollTextTwoLines(wifiText);
  Serial.println("WiFi: " + ssid + " (" + String(quality) + "%)");
}
//Wifi allapot Serial+LCD.

void showDebugCode(uint64_t code) {
  char buf[17];
  snprintf(buf, sizeof(buf), "%08X", (unsigned long)(code & 0xFFFFFFFF));
  lcdPrint("IR kod (HEX):", buf);
  Serial.println("IR: 0x" + String(buf, HEX));
}
//Ha aktiv, akkor serial+lcd IR-rol bekapott hex kod.

void handleDHT() {
  unsigned long now = millis();
  if (now - lastDhtMs < DHT_INTERVAL) return;
  lastDhtMs = now;
  float h = dht.readHumidity();
  float t = dht.readTemperature();
  if (!isnan(h) && !isnan(t)) {
    lastH = h; lastT = t;
    lastHI = dht.computeHeatIndex(t, h, false);
  }
}
//Loopba futo DHT lekerdezo algo, 2000msecenkent hivjuk be.

void handlePIR() {
  int cur = digitalRead(PIN_PIR);
  if (cur == HIGH) {
    digitalWrite(PIN_LED_ACT, HIGH);
    digitalWrite(PIN_LED_NACT, LOW);
    pirActive = true;
  } else {
    digitalWrite(PIN_LED_ACT, LOW);
    digitalWrite(PIN_LED_NACT, HIGH);
    pirActive = false;
  }
  if (showPIRactive && cur != pirPrev) showPIR();
  pirPrev = cur;
}
//PIR erzekelo, Loopba folyamat megy. Ha a shiwPIRactive fel van kapcsolva, akkor csak allapotvaltasra frissul az lcd.

void handleIR() {
  if (!IrReceiver.decode()) return;
  uint64_t code = IrReceiver.decodedIRData.decodedRawData;
  unsigned long now = millis();
  if (IrReceiver.decodedIRData.flags & IRDATA_FLAGS_IS_REPEAT || 
      code == 0xFFFFFFFFULL || code == 0x0ULL) {
    IrReceiver.resume(); return;
  }
  
  if (code != lastCode || (now - lastIrMs) > DEBOUNCE_TIME) {
    lastCode = code; lastIrMs = now;
    scrollingActive = false; // Scroll leállítás IR-nél
    if (code == 0xBD42FF00ULL) { // *
      debugMode = !debugMode;
      lcdPrint("DEBUG MOD:", debugMode ? "AKTIV" : "KI");
      Serial.println("DEBUG: " + String(debugMode ? "ON" : "OFF"));
    }
    else if (debugMode) showDebugCode(code);
    else if (code == 0xE916FF00ULL) { currentMenu = 1; showHumTemp(); }
    else if (code == 0xE619FF00ULL) { currentMenu = 2; showPIRactive = true; showPIR(); }
    else if (code == 0xF20DFF00ULL) { currentMenu = 3; showDateTime(); }
    else if (code == 0xF30CFF00ULL) { currentMenu = 4; showMenu(); }
    else if (code == 0xE718FF00ULL) { currentMenu = 5; showWiFiInfo(); }
    else if (code == 0xA15EFF00ULL) { currentMenu = 6; showSystemInfo(); }
    else if (code == 0xB946FF00ULL) { // Fel
      currentMenu = (currentMenu % 6) + 1;
      showMenuFunction(currentMenu);
    }
    else if (code == 0xEA15FF00ULL) { // Le
      currentMenu = currentMenu == 1 ? 6 : currentMenu - 1;
      showMenuFunction(currentMenu);
    }
  }
  IrReceiver.resume();
}
//iranyito vezerlo. # = DEBUG mod, 1-6 menu, FEL/Le valtogatjuk a menut.

void connectWiFiAndTime() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  unsigned long start = millis();
  Serial.print("WiFi...");
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(1000);
    attempts++;
    Serial.print(" (" + String(attempts) + "/15)");
    }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(" OK");
    configTzTime(TZ_EU_BUD, NTP1, NTP2);
    delay(1000);
  } else {
    Serial.println(" SIKERTELEN");
    WiFi.disconnect();
  }
}
//Wifihez csatlakozik es NTP lekerdez. Wifi timedoutol, ha 15masodperc alatt nem tud csatlakozni es serialra annak fuggvenyebe emgy a WIFI... OK/SIKERTELEN.

void showMenuFunction(int menu) {
  showPIRactive = false;
  switch (menu) {
    case 1: showHumTemp(); break;
    case 2: showPIRactive = true; showPIR(); break;
    case 3: showDateTime(); break;
    case 4: showMenu(); break;
    case 5: showWiFiInfo(); break;
    case 6: showSystemInfo(); break;
  }
} //currentMenu allapota alapja meghiva az adott fuggvenyt, illetve case 2-nel PIR allapotmenuben a PIRactivet truera rakja, hogy a ledek dolgozzanak es a kiiras folyamat loopban.

void setup() {
  Serial.begin(9600);
  lcd.begin(16, 2);
  lcdPrint("Inicializalas", "");
  dht.begin();
  IrReceiver.begin(IR_RECEIVER_PIN, ENABLE_LED_FEEDBACK);
  pinMode(PIN_PIR, INPUT);
  pinMode(PIN_LED_ACT, OUTPUT); pinMode(PIN_LED_NACT, OUTPUT);
  digitalWrite(PIN_LED_ACT, LOW); digitalWrite(PIN_LED_NACT, HIGH);
  connectWiFiAndTime();
  showMenu();
}
//inicializalas.


void loop() {
  handleIR();
  handleScroll();
  handleDHT();
  handlePIR();
}
//folyamatosan fut az esp cpun a loop.
