/******************************************************************
*   @brief
*   @author
******************************************************************/

// Bibliotecas inclusas
#include <SPI.h>
#include <LoRa.h>
#include "esp_task_wdt.h"

// Definição dos pinos de conexão entre ESP32 e RFM95W
#define SCK     18
#define MISO    19
#define MOSI    23
#define SS      15
#define RST     2
#define DIO0    5         // Pino de interrupção (Gatilho de Wakeup)

#define BAND    915E6   // Frequência de operação

volatile bool pacoteRecebido = false;

// Configurações Watchdog Timer ******************************************************
#define CONFIG_FREERTOS_NUMBER_OF_CORES 1
#define WDT_TIMEOUT 360000
esp_task_wdt_config_t twdt_config = {
    .timeout_ms = WDT_TIMEOUT,
    .idle_core_mask = (1 << CONFIG_FREERTOS_NUMBER_OF_CORES) - 1,    // Bitmask of all cores
    .trigger_panic = true,
};


/****************************************************************************************/
void IRAM_ATTR aoReceberPacote() {
  pacoteRecebido = true;
}

/***************************************************************************************/
void wdt_init()  {
  esp_task_wdt_deinit();              // Wdt is enabled by default, so we need to deinit it first
  esp_task_wdt_init(&twdt_config);    // Enable panic so ESP32 restarts
  esp_task_wdt_add(NULL);             // Add current thread to WDT watch

  Serial.println("\n[Lora Gateway] Watchdog configured successful");
}

/****************************************************************************************/
void setup()
{
  // Inicialização Serial
  Serial.begin(115200);
  while (!Serial);
  Serial.println("\n--- LORA RECEIVER ---");

  // Configura o pino do ESP32 como entrada
  pinMode(DIO0, INPUT_PULLDOWN);

  // Inicializa o barramento SPI com os pinos corretos
  SPI.begin(SCK, MISO, MOSI, SS);
  LoRa.setPins(SS, RST, DIO0);

  // Inicializa o rádio LoRa
  if (!LoRa.begin(BAND)) {
    Serial.println("[LORA] FALHA/ERRO ao iniciar o LoRa.");
    //while (1);
  } else {
    Serial.println("[LORA] Inicializado corretamente.");
  }
  
  attachInterrupt(digitalPinToInterrupt(DIO0), aoReceberPacote, RISING);

  Serial.println("[LORA] Colocando LoRa em modo de recepção contínua");
  LoRa.receive();
  wdt_init();
}

/***************************************************************************************/
void checkPacket(bool packetRead = false)
{
  esp_task_wdt_reset();

  if (pacoteRecebido || packetRead)
  {
    pacoteRecebido = false; 

    int packetSize = LoRa.parsePacket();

    if (packetSize) 
    {
      Serial.print("Received packet '");
      // read packet
      while (LoRa.available()) {
        Serial.print((char)LoRa.read());
      }
      // print RSSI of packet
      Serial.print("' with RSSI ");
      Serial.println(LoRa.packetRssi());
    } else {
      Serial.println("Interrupção disparada, mas nenhum pacote válido extraído.");
    }
    LoRa.receive(); 
  }
}

/****************************************************************************************/
void loop()
{
  esp_task_wdt_reset();
  checkPacket();
  //nada
}