/*
  02-Intermediario.ino
  Dispositivo: ESP32 LoRa v2 + TV-Box via Serial

  Função na rede:
  - Recebe dados do transmissor ou de outro intermediário.
  - Consulta/registra o pacote na TV-Box para descartar duplicados.
  - Se o pacote for novo, salva como pendente na TV-Box e só então envia ACK ao nó anterior.
  - O intermediário NÃO encaminha dados novos diretamente via LoRa.
  - A TV-Box envia os pendentes de volta pela Serial em ordem de fila; o intermediário retransmite via LoRa.
  - Quando recebe ACK do próximo nó, remove o pacote pendente da TV-Box.

  Observação importante:
  - Duplicados já existentes na TV-Box são ignorados localmente, mas recebem ACK.
    Isso permite que o nó anterior descarte o pacote que ele ainda mantinha pendente.
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
#define ID_DISPOSITIVO     2
#define TIPO_DISPOSITIVO   TIPO_INTERMEDIARIO

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
#define MAX_SALTOS        200
#define TAM_PACOTE        sizeof(TPacoteRede)

/* Tempos principais */
const unsigned long TIMEOUT_ACK_MS   = 1800;
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

TPacoteRede pacoteEmEspera;
bool aguardandoAck = false;

/*
  Indica se há pacotes pendentes na TV-Box.

  No intermediário, essa flag é usada para documentar e reforçar a prioridade
  da fila: novos dados recebidos por LoRa nunca são retransmitidos diretamente.
  Eles são primeiro salvos na TV-Box e somente pacotes entregues pela TV-Box
  com CMD_NENHUM podem sair novamente pelo rádio LoRa.
*/
bool filaTvBoxAtiva = false;

unsigned long momentoEnvio = 0;

/* -------------------- Funções utilitárias -------------------- */

bool mesmaChavePacote(TPacoteRede a, TPacoteRede b)
{
  return (a.origem_id == b.origem_id && a.sequencia == b.sequencia);
}

/*
  A TV-Box envia STATUS_VAZIO quando não existem pendentes.
  Isso mantém o intermediário ciente do estado da fila, igual ao transmissor.
*/
bool ehAvisoFilaVazia(TPacoteRede pacote)
{
  return (pacote.comando_serial == CMD_RESPOSTA_TVBOX &&
          pacote.tipo_mensagem == MSG_RESPOSTA_TVBOX &&
          pacote.status == STATUS_VAZIO &&
          pacote.sequencia == 0);
}

void atualizarEstadoFilaTvBox(TPacoteRede pacote)
{
  if (pacote.comando_serial == CMD_NENHUM && pacote.tipo_mensagem == MSG_DADO)
  {
    filaTvBoxAtiva = true;
  }
  else if (ehAvisoFilaVazia(pacote))
  {
    filaTvBoxAtiva = false;
  }
}

void mostrarDisplay(String linha1, String linha2, TPacoteRede pacote)
{
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("INTERMEDIARIO");

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
  display.print("Saltos: ");
  display.print(pacote.saltos);
  display.print(" Rem: ");
  display.print(pacote.remetente_id);

  display.setCursor(0, 50);
  display.print("T:");
  display.print(pacote.temperatura, 1);
  display.print(" U:");
  display.print(pacote.umidade, 1);

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

/*
  Envia comando para a TV-Box e aguarda resposta.
*/
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
        atualizarEstadoFilaTvBox(resposta);
        return resposta.status;
      }

      atualizarEstadoFilaTvBox(resposta);

      /*
        Se um pacote pendente da TV-Box chegar enquanto estamos aguardando
        a resposta de outro comando, não transmitimos esse pacote aqui.
        A TV-Box reenviará o primeiro pendente no próximo ciclo, preservando
        a prioridade da fila e evitando envio fora de ordem.
      */
    }
  }

  return STATUS_ERRO;
}

uint8_t registrarRecebidoNaTvBox(TPacoteRede pacote)
{
  uint8_t status = enviarComandoTvBox(CMD_REGISTRAR_RECEBIDO, pacote);

  if (status == STATUS_OK)
  {
    mostrarDisplay("Recebido novo", "Registrado TV-Box", pacote);
    return STATUS_OK;
  }

  if (status == STATUS_DUPLICADO)
  {
    /*
      O pacote já está na TV-Box local.
      Ele não deve ser salvo novamente nem reenfileirado, mas deve ser
      confirmado com ACK para que o nó anterior possa remover esse pacote
      da própria fila de pendentes.
    */
    mostrarDisplay("Duplicado", "ACK liberado", pacote);
    return STATUS_DUPLICADO;
  }

  mostrarDisplay("Falha TV-Box", "Sem ACK", pacote);
  return STATUS_ERRO;
}

bool armazenarPendenteNaTvBox(TPacoteRede pacote)
{
  pacote.tipo_dispositivo = TIPO_DISPOSITIVO;
  pacote.remetente_id = ID_DISPOSITIVO;
  pacote.destino_id = 0;
  pacote.tipo_mensagem = MSG_DADO;
  pacote.comando_serial = CMD_ARMAZENAR_PENDENTE;
  pacote.status = STATUS_OK;

  uint8_t status = enviarComandoTvBox(CMD_ARMAZENAR_PENDENTE, pacote);

  if (status == STATUS_OK || status == STATUS_DUPLICADO)
  {
    filaTvBoxAtiva = true;
    mostrarDisplay("Na fila", "Aguardando reenvio", pacote);
    return true;
  }

  mostrarDisplay("Falha TV-Box", "Nao enfileirou", pacote);
  return false;
}

void removerPendenteDaTvBox(TPacoteRede pacote)
{
  pacote.comando_serial = CMD_REMOVER_PENDENTE;
  enviarComandoTvBox(CMD_REMOVER_PENDENTE, pacote);
}

void enviarAck(TPacoteRede recebido)
{
  TPacoteRede ack = recebido;

  ack.versao = VERSAO_PROTOCOLO;
  ack.tipo_dispositivo = TIPO_DISPOSITIVO;
  ack.tipo_mensagem = MSG_ACK;
  ack.comando_serial = CMD_NENHUM;
  ack.destino_id = recebido.remetente_id;  /* ACK volta para quem transmitiu o último salto */
  ack.remetente_id = ID_DISPOSITIVO;
  ack.status = STATUS_OK;

  enviarPacoteLoRa(ack);
  mostrarDisplay("ACK enviado", "Para no anterior", recebido);
}

bool iniciarEnvioComAck(TPacoteRede pacote)
{
  if (aguardandoAck)
  {
    return false;
  }

  pacote.versao = VERSAO_PROTOCOLO;
  pacote.tipo_dispositivo = TIPO_DISPOSITIVO;
  pacote.tipo_mensagem = MSG_DADO;
  pacote.comando_serial = CMD_NENHUM;
  pacote.remetente_id = ID_DISPOSITIVO;
  pacote.destino_id = 0;
  pacote.status = STATUS_OK;

  pacoteEmEspera = pacote;
  filaTvBoxAtiva = true;
  aguardandoAck = true;
  momentoEnvio = millis();

  enviarPacoteLoRa(pacoteEmEspera);
  mostrarDisplay("Retransmitindo", "Aguardando ACK", pacoteEmEspera);

  return true;
}

/* -------------------- Processamento principal -------------------- */

void processarSerialTvBox()
{
  TPacoteRede pacote;

  if (!lerPacoteSerial(&pacote))
  {
    return;
  }

  atualizarEstadoFilaTvBox(pacote);

  /*
    A TV-Box envia pacotes pendentes com CMD_NENHUM.

    Regra de prioridade do intermediário:
    - só sai pelo LoRa aquilo que veio da fila da TV-Box;
    - dados novos recebidos pelo rádio são apenas registrados/enfileirados;
    - se já estiver aguardando ACK, não inicia outro envio.
  */
  if (pacote.comando_serial == CMD_NENHUM && pacote.tipo_mensagem == MSG_DADO)
  {
    iniciarEnvioComAck(pacote);
  }
}

void processarDadoRecebido(TPacoteRede recebido)
{
  /*
    Evita tratar eco do próprio pacote transmitido pelo rádio.
  */
  if (recebido.remetente_id == ID_DISPOSITIVO)
  {
    return;
  }

  if (recebido.saltos >= MAX_SALTOS)
  {
    mostrarDisplay("Max saltos", "Descartado", recebido);
    return;
  }

  /*
    Primeiro registra na TV-Box.

    Regra atualizada:
    - STATUS_OK: pacote novo, será salvo como pendente para retransmissão.
    - STATUS_DUPLICADO: pacote já existia; ignoramos localmente, mas enviamos ACK
      para o nó anterior descartar o pendente.
    - STATUS_ERRO: não há garantia de armazenamento; não envia ACK.
  */
  uint8_t statusRegistro = registrarRecebidoNaTvBox(recebido);

  if (statusRegistro == STATUS_DUPLICADO)
  {
    enviarAck(recebido);
    return;
  }

  if (statusRegistro != STATUS_OK)
  {
    return;
  }

  /*
    Prepara o pacote para seguir adiante na rede.
    A TV-Box manterá esse pacote como pendente até algum receptor/intermediário
    confirmar o recebimento do próximo salto.

    Mesmo que a fila esteja vazia, este intermediário não retransmite o dado
    recebido diretamente. O dado sempre entra primeiro em pacotes_pendentes.
    Assim, a saída LoRa do intermediário respeita a ordem da fila da TV-Box.

    Importante: primeiro salvamos como pendente e somente depois enviamos ACK
    ao nó anterior. Assim, o transmissor/intermediário anterior só descarta
    definitivamente o pacote depois que este intermediário garantiu a gravação.
  */
  TPacoteRede encaminhar = recebido;
  encaminhar.saltos++;
  encaminhar.tipo_dispositivo = TIPO_DISPOSITIVO;
  encaminhar.remetente_id = ID_DISPOSITIVO;
  encaminhar.destino_id = 0;
  encaminhar.tipo_mensagem = MSG_DADO;
  encaminhar.comando_serial = CMD_NENHUM;
  encaminhar.status = STATUS_OK;

  if (armazenarPendenteNaTvBox(encaminhar))
  {
    enviarAck(recebido);
  }
}

void processarAckRecebido(TPacoteRede ack)
{
  if (ack.destino_id != ID_DISPOSITIVO)
  {
    return;
  }

  if (!aguardandoAck)
  {
    return;
  }

  if (!mesmaChavePacote(ack, pacoteEmEspera))
  {
    return;
  }

  removerPendenteDaTvBox(pacoteEmEspera);
  aguardandoAck = false;

  mostrarDisplay("ACK recebido", "Removido fila", ack);
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
  else if (recebido.tipo_mensagem == MSG_ACK)
  {
    processarAckRecebido(recebido);
  }
}

void processarTimeoutAck()
{
  if (!aguardandoAck)
  {
    return;
  }

  /*
    Como o pacote transmitido pelo intermediário já está salvo na TV-Box,
    no timeout basta liberar o estado. A TV-Box reenviará o mesmo pacote depois.
  */
  if (millis() - momentoEnvio >= TIMEOUT_ACK_MS)
  {
    mostrarDisplay("Sem ACK", "Mantido TV-Box", pacoteEmEspera);
    aguardandoAck = false;
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
  processarSerialTvBox();
  processarLoRa();
  processarTimeoutAck();
}
