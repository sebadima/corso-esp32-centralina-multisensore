// https://rntlab.com/question/how-to-know-and-or-set-the-wifi-channel-on-an-esp32/?utm_source=pocket_saves
// https://github.com/HarringayMakerSpace/ESP-Now/blob/master/espnow-sensor-minimal/espnow-sensor-minimal.ino
// https://randomnerdtutorials.com/esp32-esp-now-wi-fi-web-server/

#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h> 
#include "DHT.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

constexpr char WIFI_SSID[] = "rda0";

// Indirizzi MAC dei dispositivi di destinazione
// trovati con la utility apposita
constexpr uint8_t ESP_NOW_RECEIVER[] = { 0x08, 0xd1, 0xf9, 0x99, 0x2d, 0x84 };

// Set your Static IP address 
IPAddress local_IP(192, 168, 43, 129);
// Set your Gateway IP address 
IPAddress gateway(10, 15, 39, 160);
IPAddress subnet(255, 255, 0, 0);

// Struct per definire il formato dei dati
typedef struct struct_messaggio {
  char a[32];
  int   umidita;
  float temperatura;
  float gas_1;
  float gas_2;
  int contatore;
} struct_messaggio;

struct_messaggio Dati;
esp_now_peer_info_t peerInfo;

#define DHTPIN 13
#define DHTTYPE DHT11

DHT dht(DHTPIN, DHTTYPE);
float t, h, g_1, g_2;
int lost_packages;
int ix;
int Gas_1 = 33;
int Gas_2 = 35;

#define DELAY_RECONNECT 600 // intervallo in secondi per forzare il reboot
volatile int interruptCounter;
int totalInterruptCounter;
hw_timer_t * timer = NULL;
portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;



void IRAM_ATTR onTimer() 
{
  // https://github.com/espressif/arduino-esp32/blob/master/libraries/ESP32/examples/Timer/RepeatTimer/RepeatTimer.ino
  portENTER_CRITICAL_ISR(&timerMux);
  interruptCounter++;
  if (lost_packages >=15) {
    ESP.restart(); // Riesegui la connessione al nuovo canale WIFI
  }
  portEXIT_CRITICAL_ISR(&timerMux);
}


int32_t getWiFiChannel(const char *ssid) {

    if (int32_t n = WiFi.scanNetworks()) {
        for (uint8_t i=0; i<n; i++) {
            if (!strcmp(ssid, WiFi.SSID(i).c_str())) {
                return WiFi.channel(i);
            }
        }
    }

    return 0;
}


void initWiFi() {

    WiFi.mode(WIFI_MODE_STA);

    // acquisice il canale usato dalla WIFI
    int32_t channel = getWiFiChannel(WIFI_SSID);

    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_promiscuous(false);

    Serial.printf("SSID: %s\n", WIFI_SSID);
    Serial.printf("Channel: %u\n", WiFi.channel());
}


void initEspNow() {

    if (esp_now_init() != ESP_OK) {
        Serial.println("ESP NOW failed to initialize");
        while (1);
    }

    memcpy(peerInfo.peer_addr, ESP_NOW_RECEIVER, 6);
    peerInfo.ifidx   =  WIFI_IF_STA;
    peerInfo.encrypt = false;

    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
        Serial.println("ESP NOW pairing failure");
        while (1);
    }
}


void suInvioDati(const uint8_t *mac_addr, esp_now_send_status_t status) {
  Serial.print("\r\nStatus invio:\t");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Consegna positiva" : "Errore di consegna");
  if (status != ESP_NOW_SEND_SUCCESS) {
      lost_packages ++;
  }
  if (lost_packages >=15) {
    Serial.println("ESP restarting on lost packages");
    ESP.restart(); // Riesegui la connessione al nuovo canale WIFI
  }
}


void setup() {
  Serial.begin(115200);
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); //disable brownout detector

  initWiFi();
  initEspNow();

  timer = timerBegin(0, 80, true);
  timerAttachInterrupt(timer, &onTimer, true);
  timerAlarmWrite(timer, DELAY_RECONNECT * 1000000, true);
  timerAlarmEnable(timer);

  dht.begin();
  pinMode(Gas_1, INPUT);
  pinMode(Gas_2, INPUT);

  esp_now_register_send_cb(suInvioDati);
  ix = 1;
}


void loop() {
  
  h   = dht.readHumidity();
  t   = dht.readTemperature();
  g_1 = analogRead(Gas_1);
  g_2 = analogRead(Gas_2);

  if (isnan(g_1) )
  {
    Serial.println(F("Non riesco a leggere dal sensore di GAS 1!"));
    return;
  }

  if (isnan(g_2) )
  {
    Serial.println(F("Non riesco a leggere dal sensore di GAS 2!"));
    return;
  }
 
  if (isnan(t) ) 
  {
    Serial.println(F("Non riesco a leggere dal sensore DHT!"));
    return;
  }

  Serial.print("Temperatura: ");
  Serial.println(t);
  Serial.print("Umidità: ");
  Serial.println(h);
  Serial.print("Gas_1: ");
  Serial.println(g_1);
  Serial.print("Gas_2: ");
  Serial.println(g_2);

  strcpy(Dati.a, "Rilevazioni DHT11");
  Dati.umidita     = (int) h;
  Dati.temperatura = t;
  Dati.gas_1       = g_1;
  Dati.gas_2       = g_2;
  Dati.contatore   = ix;

  // invio del messaggio a ESP1
  esp_err_t result = esp_now_send(0, (uint8_t *) &Dati, sizeof(Dati));
   
  if (result == ESP_OK) {
    Serial.println("Messaggio inviato con successo");
  }
  else {
    Serial.println("Errore di invio");
  }

  ix = ix + 1;
  delay(2000);
}
