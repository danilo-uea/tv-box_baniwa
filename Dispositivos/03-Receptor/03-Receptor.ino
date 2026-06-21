/*
  03-Receptor.ino
  Dispositivo: ESP32 LoRa v2 + TV-Box via Serial

  Função na rede:
  - Finaliza o fluxo.
  - Recebe dados do transmissor ou de intermediários.
  - Registra os dados na TV-Box do receptor.
  - Se o dado for novo, envia ACK ao nó anterior.
  - Se o dado já existir na TV-Box, descarta e NÃO envia ACK, conforme solicitado.
*/

/* Bibliotecas para o Display OLED */
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

/* Bibliotecas para comunicação LoRa */
#include <LoRa.h>
#include <SPI.h>

/* Pinagem para o Display OLED */
#define OLED_SDA 4
#define OLED_SCL 15
#define OLED_RST 16
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

/* Pinagem para comunicação com rádio LoRa */
#define SCK_LORA           5
#define MISO_LORA          19
#define MOSI_LORA          27
#define RESET_PIN_LORA     14
#define SS_PIN_LORA        18
#define HIGH_GAIN_LORA     20
#define BAND               915E6

/* Configuração deste nó */
#define ID_DISPOSITIVO     3
#define TIPO_DISPOSITIVO   TIPO_RECEPTOR

/* Tipos de dispositivo */
#define TIPO_TRANSMISSOR   1
#define TIPO_INTERMEDIARIO 2
#define TIPO_RECEPTOR      3

/* Tipos de mensagem LoRa */
#define MSG_DADO           1
#define MSG_ACK            2
#define MSG_NACK           3
#define MSG_RESPOSTA_TVBOX 4

/* Comandos usados somente entre ESP32 e TV-Box via Serial */
#define CMD_NENHUM             0
#define CMD_ARMAZENAR_PENDENTE 1
#define CMD_REMOVER_PENDENTE   2
#define CMD_REGISTRAR_RECEBIDO 3
#define CMD_RESPOSTA_TVBOX     4

/* Status usado nas respostas da TV-Box e ACK/NACK */
#define STATUS_ERRO       0
#define STATUS_OK         1
#define STATUS_DUPLICADO  2
#define STATUS_VAZIO      3

/* Configurações do protocolo */
#define VERSAO_PROTOCOLO  1
#define TAM_PACOTE        sizeof(TPacoteRede)

const unsigned long TIMEOUT_TVBOX_MS = 700;

/*
  Formato equivalente no Python:
  '<BBBBHHHIBBffffI'
*/
typedef struct __attribute__((__packed__))
{
  uint8_t  versao;
  uint8_t  tipo_dispositivo;
  uint8_t  tipo_mensagem;
  uint8_t  comando_serial;
  uint16_t origem_id;
  uint16_t remetente_id;
  uint16_t destino_id;
  uint32_t sequencia;
  uint8_t  saltos;
  uint8_t  status;
  float    temperatura;
  float    umidade;
  float    latitude;
  float    longitude;
  uint32_t timestamp;
} TPacoteRede;

/* Objetos globais */
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RST);

uint32_t fatorE = 7;

/* -------------------- Funções utilitárias -------------------- */

bool mesmaChavePacote(TPacoteRede a, TPacoteRede b)
{
  return (a.origem_id == b.origem_id && a.sequencia == b.sequencia);
}

void mostrarDisplay(String linha1, String linha2, TPacoteRede pacote)
{
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("RECEPTOR");

  display.setCursor(0, 10);
  display.println(linha1);

  display.setCursor(0, 20);
  display.println(linha2);

  display.setCursor(0, 30);
  display.print("Orig: ");
  display.print(pacote.origem_id);
  display.print(" Seq: ");
  display.println(pacote.sequencia);

  display.setCursor(0, 40);
  display.print("T:");
  display.print(pacote.temperatura, 1);
  display.print(" U:");
  display.print(pacote.umidade, 1);

  display.setCursor(0, 50);
  display.print("Rem: ");
  display.print(pacote.remetente_id);
  display.print(" S:");
  display.print(pacote.saltos);

  display.display();
}

void mostrarAguardando()
{
  TPacoteRede vazio = {};
  mostrarDisplay("Aguardando...", "Rede LoRa", vazio);
}

bool initComunicacaoLoRa()
{
  SPI.begin(SCK_LORA, MISO_LORA, MOSI_LORA, SS_PIN_LORA);
  LoRa.setPins(SS_PIN_LORA, RESET_PIN_LORA, LORA_DEFAULT_DIO0_PIN);

  if (!LoRa.begin(BAND))
  {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("Radio LoRa");
    display.setCursor(0, 10);
    display.println("Conectando...");
    display.display();
    delay(1000);
    return false;
  }

  LoRa.setSpreadingFactor(fatorE);
  LoRa.setTxPower(HIGH_GAIN_LORA);
  LoRa.setSignalBandwidth(125E3);
  LoRa.setCodingRate4(5);
  LoRa.setSyncWord(0x55);

  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Radio LoRa OK");
  display.display();
  delay(1000);
  return true;
}

void enviarPacoteLoRa(TPacoteRede pacote)
{
  LoRa.beginPacket();
  LoRa.write((uint8_t *)&pacote, sizeof(TPacoteRede));
  LoRa.endPacket();
}

bool lerPacoteSerial(TPacoteRede *pacote)
{
  if (Serial.available() >= (int)sizeof(TPacoteRede))
  {
    Serial.readBytes((char *)pacote, sizeof(TPacoteRede));
    return true;
  }
  return false;
}

bool lerPacoteLoRa(TPacoteRede *pacote)
{
  int tamPacote = LoRa.parsePacket();

  if (tamPacote == sizeof(TPacoteRede))
  {
    uint8_t *ptr = (uint8_t *)pacote;
    int i = 0;

    while (LoRa.available() && i < (int)sizeof(TPacoteRede))
    {
      ptr[i] = (uint8_t)LoRa.read();
      i++;
    }

    return (i == (int)sizeof(TPacoteRede));
  }

  return false;
}

uint8_t enviarComandoTvBox(uint8_t comando, TPacoteRede pacote)
{
  pacote.comando_serial = comando;
  Serial.write((uint8_t *)&pacote, sizeof(TPacoteRede));

  unsigned long inicio = millis();

  while (millis() - inicio < TIMEOUT_TVBOX_MS)
  {
    TPacoteRede resposta;

    if (lerPacoteSerial(&resposta))
    {
      if (resposta.comando_serial == CMD_RESPOSTA_TVBOX && mesmaChavePacote(resposta, pacote))
      {
        return resposta.status;
      }
    }
  }

  return STATUS_ERRO;
}

bool registrarRecebidoNaTvBox(TPacoteRede pacote)
{
  uint8_t status = enviarComandoTvBox(CMD_REGISTRAR_RECEBIDO, pacote);

  if (status == STATUS_OK)
  {
    mostrarDisplay("Recebido novo", "Salvo TV-Box", pacote);
    return true;
  }

  if (status == STATUS_DUPLICADO)
  {
    mostrarDisplay("Duplicado", "Sem ACK", pacote);
    return false;
  }

  mostrarDisplay("Falha TV-Box", "Sem ACK", pacote);
  return false;
}

void enviarAck(TPacoteRede recebido)
{
  TPacoteRede ack = recebido;

  ack.versao = VERSAO_PROTOCOLO;
  ack.tipo_dispositivo = TIPO_DISPOSITIVO;
  ack.tipo_mensagem = MSG_ACK;
  ack.comando_serial = CMD_NENHUM;
  ack.destino_id = recebido.remetente_id;
  ack.remetente_id = ID_DISPOSITIVO;
  ack.status = STATUS_OK;

  enviarPacoteLoRa(ack);
  mostrarDisplay("ACK enviado", "Fluxo finalizado", recebido);
}

void processarDadoRecebido(TPacoteRede recebido)
{
  if (recebido.remetente_id == ID_DISPOSITIVO)
  {
    return;
  }

  /*
    O receptor finaliza o fluxo:
    - se for novo na TV-Box, salva e confirma;
    - se for repetido, descarta sem confirmar.
  */
  if (registrarRecebidoNaTvBox(recebido))
  {
    enviarAck(recebido);
  }
}

void processarLoRa()
{
  TPacoteRede recebido;

  if (!lerPacoteLoRa(&recebido))
  {
    return;
  }

  if (recebido.versao != VERSAO_PROTOCOLO)
  {
    return;
  }

  if (recebido.tipo_mensagem == MSG_DADO)
  {
    processarDadoRecebido(recebido);
  }
}

void setup()
{
  Serial.begin(115200);

  pinMode(OLED_RST, OUTPUT);
  digitalWrite(OLED_RST, LOW);
  delay(20);
  digitalWrite(OLED_RST, HIGH);

  Wire.begin(OLED_SDA, OLED_SCL);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3c, false, false))
  {
    for (;;);
  }

  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setTextSize(1);

  while (initComunicacaoLoRa() == false);

  mostrarAguardando();
}

void loop()
{
  processarLoRa();
}
