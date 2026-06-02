/******************************************************************
* @brief  Receptor LoRa com ESP32 em sleep
* @author Filipe Ihancis <@filipeihancist@gmail.com>
* @attention 
*/

// Bibliotecas Utilizadas ***************************************************************
#include <SPI.h>
#include <LoRa.h>
#include "esp_task_wdt.h"
#include "driver/rtc_io.h"

// Pinagem RFM95W ************************************************************************
#define SCK     18        // Serial clock
#define MISO    19        // Pino master in/slave out
#define MOSI    23        // Pino Master-out/Slave-in
#define SS      15        // Pino de chip select/slave select (acorda disp ou seleciona)
#define RST     2         // Pino de Reset do chip LoRa
#define DIO0    4         // Pino RxDone (usaremos como interrupção)
#define BAND    915E6     // Banda de frequência (homologada ANATEL)


// Registradores LoRa (checar Datasheet RFM95W) *******************************************
#define REG_DIO_MAPPING_1           0x40    // Pino RXDONE DIO0
#define REG_RX_NB_BYTES             0x13    // Número de bytes RX
#define REG_FIFO_RX_CURRENT_ADDR    0x10    //
#define REG_FIFO                    0x00    //
#define REG_PKT_RSSI_VALUE          0x1A    //
#define REG_FIFO_ADDR_PTR           0x0D    //


// Configurações Watchdog Timer ************************************************************
#define CONFIG_FREERTOS_NUMBER_OF_CORES 1
#define WDT_TIMEOUT 360000
esp_task_wdt_config_t twdt_config = {
    .timeout_ms = WDT_TIMEOUT,
    .idle_core_mask = (1 << CONFIG_FREERTOS_NUMBER_OF_CORES) - 1,
    .trigger_panic = true,
};

// Configuração fixa do SPI para ler o LoRa no Wakeup **************************************
SPISettings spiSettings(8E6, MSBFIRST, SPI_MODE0);

// Variáveis de controle ********************************************************************
RTC_DATA_ATTR bool sleepmode  = false;    // Config. Sleep Mode
bool pacoteRecebido           = false;    // Flag que indica se pacote pode ser recebido
int counter                   = 0;        // Contador para teste prático


/*******************************************************************************************/
void wdt_init()  {
  esp_task_wdt_deinit();
  esp_task_wdt_init(&twdt_config);
  esp_task_wdt_add(NULL);           // n tem task especifica de watchdog
}

/*******************************************************************************************/
uint8_t readRegRaw(uint8_t reg)
{
  SPI.beginTransaction(spiSettings);
  digitalWrite(SS, LOW);
  SPI.transfer(reg & 0x7F);
  uint8_t val = SPI.transfer(REG_FIFO);
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

/*****************************************************************************************/
void initSleep()
{
  // Coloca no modo de recepção
  LoRa.receive();

  Serial.println("[SLEEP] Dormindo...\n");
  Serial.flush();

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

/******************************************************************************************/
void IRAM_ATTR aoReceberPacote() {
  pacoteRecebido = true;
}

/*******************************************************************************************/
void configReceiver()
{
  attachInterrupt(digitalPinToInterrupt(DIO0), aoReceberPacote, RISING);
  Serial.println("[LORA] Colocando LoRa em modo de recepção contínua");
  LoRa.receive();
}

/********************************************************************************************/
void checkPacket()
{
  esp_task_wdt_reset();

  if (pacoteRecebido)
  {
    pacoteRecebido = false;     // Evitar duplicação

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
      counter++;

    } else {
      Serial.println("[LORA] Interrupção disparada, mas nenhum pacote válido extraído.");
    }
    LoRa.receive();   // coloca em modo de recepção contínua nos registradores
  }
}

/*******************************************************************************************/
void setup()
{
  // Configura os pinos como SAÍDA em nível ALTO
  // Esse procedimento evita que o registrador seja resetado/limpo devido a pulso RST/SS
  pinMode(SS, OUTPUT);
  digitalWrite(SS, HIGH);
  pinMode(RST, OUTPUT);
  digitalWrite(RST, HIGH);

  // Liberação de trava do sleep. Isso impede que o RST vá pra GND e apague o rádio.
  rtc_gpio_hold_dis((gpio_num_t)SS);
  rtc_gpio_hold_dis((gpio_num_t)RST);

  // Inicialização da Serial
  Serial.begin(115200);
  Serial.println("\n--- LORA RECEIVER WITH DEEP SLEEP ---");

  wdt_init();   // Configuração do watchdog timer (não é crítico, mas padrão aqui)

  SPI.begin(SCK, MISO, MOSI, SS);   // Inicia o SPI dedicado ao LoRa

  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();    

  // SE ACORDOU, EXTRAI O PACOTE INTACTO
  if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT0)
  {
    Serial.println("[WAKE] Acordado. Extraindo dados do buffer de hardware...");
    
    uint8_t irqFlags = readRegRaw(0x12);
    
    // Verifica o bit 0x40 (RxDone) bit a bit
    if (irqFlags & REG_DIO_MAPPING_1)
    { 
      int packetSize = readRegRaw(REG_RX_NB_BYTES); // numero de bytes disponiveis p leitura (rx)
      if (packetSize > 0) {
        uint8_t rxStartAddr = readRegRaw(REG_FIFO_RX_CURRENT_ADDR);
        writeRegRaw(REG_FIFO_ADDR_PTR, rxStartAddr);

        Serial.print("[LORA] Pacote extraído: '");
        for (int i = 0; i < packetSize; i++) {
          Serial.print((char)readRegRaw(REG_FIFO));
        }
        Serial.println();
        /*
        int rssi = readRegRaw(REG_PKT_RSSI_VALUE) - 157;
        Serial.printf("' | RSSI: %d dBm\n", rssi);
        */
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

  LoRa.setPins(SS, RST, DIO0);  // Pinagem LoRa (ind SPI)

  // Inicialização do rádio LoRa
  if (!LoRa.begin(BAND)) {
    Serial.println("[ERRO] Falha crítica ao inicializar o rádio.");
    while (1);
  } else {
    Serial.println("[LORA] Inicializado corretamente");
  }

  if(sleepmode) initSleep();
  else configReceiver();
}

/*******************************************************************************************/
void loop()
{
  esp_task_wdt_reset();
  checkPacket();
  
  // Teste de comutacao para verificar se comportamento será o mesmo (não irá perder pkt etc)
  if(counter > 9) {
    Serial.println("[SLEEP] Counter > 10, entrando em sleep.");
    sleepmode = true;
    initSleep();
  }
}