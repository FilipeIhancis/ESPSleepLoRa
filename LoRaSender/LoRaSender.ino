#include <SPI.h>
#include <LoRa.h>

// Definição dos pinos de conexão entre ESP32 e RFM95W
#define SCK     18
#define MISO    19
#define MOSI    23
#define SS      15
#define RST     2
#define DIO0    5  // Pino de interrupção (Gatilho de Wakeup)

// Definição da frequência do seu módulo (ajuste para a sua região)
// 433E6 para 433 MHz, 868E6 para 868 MHz, 915E6 para 915 MHz
#define BAND    915E6 

int counter = 0;

void setup() 
{
  Serial.begin(115200);
  while (!Serial);
  Serial.println("\n--- LORA SENDER ---");

  // Inicializa o barramento SPI com os pinos corretos
  SPI.begin(SCK, MISO, MOSI, SS);
  LoRa.setPins(SS, RST, DIO0);

  // Inicializa o rádio LoRa
  if (!LoRa.begin(BAND)) {
    Serial.println("Falha ao iniciar o LoRa. Verifique as conexões.");
    while (1);
  }
}

void loop() 
{
  Serial.print("Sending packet: ");
  Serial.println(counter);

  // send packet
  LoRa.beginPacket();
  LoRa.print("hello ");
  LoRa.print(counter);
  LoRa.endPacket();

  counter++;

  delay(3000);
}