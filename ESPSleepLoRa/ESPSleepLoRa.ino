/******************************************************************
* @brief  Receptor LoRa com ESP32 em sleep
* @author Filipe Ihancis <@filipeihancist@gmail.com>
******************************************************************/

// Bibliotecas Utilizadas
#include <SPI.h>
#include <LoRa.h>
#include "esp_task_wdt.h"
#include "driver/rtc_io.h"

// Pinagem RFM95W
#define SCK     18
#define MISO    19
#define MOSI    23
#define SS      15
#define RST     2   
#define DIO0    4   
#define BAND    915E6 

// Configurações Watchdog Timer --------------------------------
#define CONFIG_FREERTOS_NUMBER_OF_CORES 1
#define WDT_TIMEOUT 360000
esp_task_wdt_config_t twdt_config = {
    .timeout_ms = WDT_TIMEOUT,
    .idle_core_mask = (1 << CONFIG_FREERTOS_NUMBER_OF_CORES) - 1,
    .trigger_panic = true,
};

// Configuração fixa do SPI para ler o LoRa no Wakeup
SPISettings spiSettings(8E6, MSBFIRST, SPI_MODE0);

/*******************************************************************************************/
void wdt_init()  {
  esp_task_wdt_deinit();
  esp_task_wdt_init(&twdt_config);
  esp_task_wdt_add(NULL);
}

/*******************************************************************************************/
uint8_t readRegRaw(uint8_t reg) {
  SPI.beginTransaction(spiSettings);
  digitalWrite(SS, LOW);
  SPI.transfer(reg & 0x7F);
  uint8_t val = SPI.transfer(0x00);
  digitalWrite(SS, HIGH);
  SPI.endTransaction();
  return val;
}

/*******************************************************************************************/
void writeRegRaw(uint8_t reg, uint8_t value) {
  SPI.beginTransaction(spiSettings);
  digitalWrite(SS, LOW);
  SPI.transfer(reg | 0x80);
  SPI.transfer(value);
  digitalWrite(SS, HIGH);
  SPI.endTransaction();
}

/*******************************************************************************************/
void setup()
{
  // --- A CORREÇÃO DO GLITCH ELÉTRICO ---
  // 1. Configura os pinos como SAÍDA em nível ALTO (Seguro) PRIMEIRO
  pinMode(SS, OUTPUT);
  digitalWrite(SS, HIGH);
  pinMode(RST, OUTPUT);
  digitalWrite(RST, HIGH);

  // 2. SÓ DEPOIS libera a trava. Isso impede que o RST "caia" pra GND e apague o rádio.
  rtc_gpio_hold_dis((gpio_num_t)SS);
  rtc_gpio_hold_dis((gpio_num_t)RST);

  Serial.begin(115200);
  Serial.println("\n--- LORA RECEIVER WITH DEEP SLEEP ---");

  wdt_init();

  // Inicia o SPI do hardware
  SPI.begin(SCK, MISO, MOSI, SS);

  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();

  // 3. SE ACORDOU, EXTRAI O PACOTE INTACTO
  if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT0) {
    Serial.println("[WAKE] Acordado. Extraindo dados do buffer de hardware...");
    
    uint8_t irqFlags = readRegRaw(0x12);
    
    if (irqFlags & 0x40) { // Verifica o bit 0x40 (RxDone)
      int packetSize = readRegRaw(0x13);
      if (packetSize > 0) {
        uint8_t rxStartAddr = readRegRaw(0x10);
        writeRegRaw(0x0D, rxStartAddr);

        Serial.print("[LORA] Pacote extraído: '");
        for (int i = 0; i < packetSize; i++) {
          Serial.print((char)readRegRaw(0x00));
        }
        int rssi = readRegRaw(0x1A) - 157;
        Serial.printf("' | RSSI: %d dBm\n", rssi);
      }
    } else {
      Serial.printf("[AVISO] Acordou, mas sem RxDone. Flags: 0x%02X\n", irqFlags);
    }
  } else {
    Serial.println("[BOOT] Inicialização fria (Primeiro Boot).");
  }

  // 4. LIMPEZA E REINICIALIZAÇÃO (Hard Reset)
  digitalWrite(RST, LOW);
  delay(1);
  digitalWrite(RST, HIGH);
  delay(1);

  LoRa.setPins(SS, RST, DIO0);

  if (!LoRa.begin(BAND)) {
    Serial.println("[ERRO] Falha crítica ao inicializar o rádio.");
    while (1);
  } else {
    Serial.println("[LORA] Inicializado corretamente");
  }

  // Coloca no modo de recepção
  LoRa.receive();

  Serial.println("[SLEEP] Dormindo...\n");
  Serial.flush();

  // 5. PREPARAÇÃO PARA O SONO
  // Trava a energia do barramento para o rádio se manter vivo
  digitalWrite(SS, HIGH);
  digitalWrite(RST, HIGH);
  rtc_gpio_hold_en((gpio_num_t)SS);
  rtc_gpio_hold_en((gpio_num_t)RST);

  // Ativa o resistor interno do RTC no pino de interrupção para ancorar ruídos em 0V
  rtc_gpio_pulldown_en((gpio_num_t)DIO0);

  // Configura para acordar na borda de subida (quando for pra 3.3V)
  esp_sleep_enable_ext0_wakeup((gpio_num_t)DIO0, 1);
  
  delay(50);
  esp_deep_sleep_start();
}

/*******************************************************************************************/
void loop() {
  // Loop vazio - Tudo ocorre no ciclo de setup/sleep
}