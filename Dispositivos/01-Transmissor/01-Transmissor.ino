/* Bibliotecas para o Display OLED*/
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

/* Bibliotecas para comunicação LoRa */
#include <LoRa.h>
#include <SPI.h>

/* Pinagem para o Display Oled */
#define OLED_SDA 4
#define OLED_SCL 15 
#define OLED_RST 16
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

/* Pinagem para comunicação com radio LoRa */
#define SCK_LORA           5
#define MISO_LORA          19
#define MOSI_LORA          27
#define RESET_PIN_LORA     14
#define SS_PIN_LORA        18
#define HIGH_GAIN_LORA     20  /* dBm */
#define BAND               915E6  /* 915MHz de frequencia */

uint32_t fatorE = 7;     /* Valor do fator de espalhamento */
int id_dispositivo = 1;  /* O id do dispositivo deve ser único para cada dispositivo */

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RST); /* Definicões do Display OLED */

/* Estrutura do pacote de dados */
typedef struct __attribute__((__packed__))  
{
  int contador;
  int id_dispositivo; /* O dispositivo que enviou o pacote */
  byte tipo_mensagem; /* Tipos de Mensagem: 1 - Envio da mensagem, 2 - Confirmação de recebimento */
  char mensagem[20];
} TDadosLora;

void aguardando_dados_display() {
  /* Imprimir mensagem dizendo o tipo do dispositivo */
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Transmissor");
  display.setCursor(0, 10);
  display.println("Em espera...");
  display.display();
  
  delay(200);
}

void escreve_medicoes_display(TDadosLora dados_lora, int state)
{      
    display.clearDisplay();

    display.setCursor(0, 0);
    display.println("TRANSMISSOR");

    display.setCursor(0, 10);
    display.print("Cont: ");
    display.println(dados_lora.contador);

    display.setCursor(0, 20);
    display.print("Tipo: ");
    display.println(dados_lora.tipo_mensagem);
    
    display.setCursor(0, 30);
    display.print("Mensagem: ");
    display.println(dados_lora.mensagem);

    display.setCursor(0, 40);
    display.print("Status: ");
    if (state == 1){
      display.println("Esp32");  
    } else if (state == 2){
      display.println("Tv-Box");  
    }
    
    display.display();
}

void envia_medicoes_serial(TDadosLora dados_lora) 
{
  Serial.println("TRANSMISSOR");
  
  Serial.print("Contador: ");
  Serial.println(dados_lora.contador);

  Serial.print("Tipo Mensagem: ");
  Serial.println(dados_lora.tipo_mensagem);

  Serial.print("Mensagem: ");
  Serial.println(dados_lora.mensagem);
  
  Serial.println(" ");
}

TDadosLora enviar_dados_lora(TDadosLora dados_lora) /* Transmite os dados via LoRa */   
{
  LoRa.beginPacket();
  LoRa.write((unsigned char *)&dados_lora, sizeof(TDadosLora));
  LoRa.endPacket();
  return dados_lora;
}

bool init_comunicacao_lora(void)
{
    bool status_init = false;
    Serial.println("[LoRa Emissor] Tentando iniciar comunicacao com o radio LoRa...");
    SPI.begin(SCK_LORA, MISO_LORA, MOSI_LORA, SS_PIN_LORA);
    LoRa.setPins(SS_PIN_LORA, RESET_PIN_LORA, LORA_DEFAULT_DIO0_PIN);

    display.clearDisplay();
    
    if (!LoRa.begin(BAND)) 
    {
      Serial.println("[LoRa Emissor] Comunicacao com o radio LoRa falhou. Nova tentativa em 1 segundo...");        
      status_init = false;

      display.setCursor(0, 0);
      display.println("Radio LoRa");
      display.setCursor(0, 10);
      display.println("Status: Conectando...");
      display.setCursor(0, 20);
      display.println("Tentativas: Cada 1s");
      display.display();

      delay(1000);
    }
    else
    {
      LoRa.setSpreadingFactor(fatorE); /* Fator de Espalhamento */
      LoRa.setTxPower(HIGH_GAIN_LORA); /* Configura o ganho do receptor LoRa para 20dBm, o maior ganho possível (visando maior alcance possível) */ 
      LoRa.setSignalBandwidth(125E3);  /* Largura de banda fixa de 125 kHz - Suporta valores: 7.8E3, 10.4E3, 15.6E3, 20.8E3, 31.25E3, 41.7E3, 62.5E3, 125E3, 250E3 e 500E3 */
      LoRa.setCodingRate4(5);          /* Taxa de código - Suporta valores entre 5 e 8 */
      LoRa.setSyncWord(0x55);          /* Palavra de sincronização. Deve ser a mesma no transmissor e receptor */
      
      Serial.println("[LoRa Emissor] Comunicacao com o radio LoRa ok");
      status_init = true;

      display.setCursor(0, 0);
      display.println("Radio LoRa");
      display.setCursor(0, 10);
      display.println("Status: Ok");
      display.display();
    }

    return status_init;
}

TDadosLora dados_receber = {0, id_dispositivo, 2, ""}; /* Inicializando o pacote */

void recebe_informacoes(){ /* Recebe os pacotes de confirmação de recepção */
  TDadosLora dados_temp;   /* Inicializando o pacote */
  char byte_recebido;
  int tam_pacote = LoRa.parsePacket();          /* Verifica se chegou alguma informação do tamanho esperado */
  char * ptInformaraoRecebida = NULL;
  
  if (tam_pacote == sizeof(TDadosLora)) {       /* Recebe a confirmação de recebimento da mensagem */
    ptInformaraoRecebida = (char *)&dados_temp; /* Recebe os dados conforme protocolo */
    
    while (LoRa.available()) 
    {
        byte_recebido = (char)LoRa.read();
        *ptInformaraoRecebida = byte_recebido;
        ptInformaraoRecebida++;
    }

    /* Ele só aceita os pacotes que ele enviou do tipo de confirmação de recepção */
    if(dados_temp.id_dispositivo == id_dispositivo && dados_temp.tipo_mensagem == 2){
      dados_receber = dados_temp;
      //envia_medicoes_serial(dados_receber);
      //escreve_medicoes_display(dados_receber);
    }
  }
}

#define TAMANHO_FILA 10 // Define o tamanho máximo da fila

// Fila de elementos do tipo TDadosLora
TDadosLora fila[TAMANHO_FILA];
int frente = 0; // Índice para o início da fila
int traseira = -1; // Índice para o fim da fila
int tamanhoAtual = 0; // Número de elementos na fila

// Função para verificar se a fila está cheia
bool isFilaCheia() {
  return (tamanhoAtual == TAMANHO_FILA);
}

// Função para verificar se a fila está vazia
bool isFilaVazia() {
  return (tamanhoAtual == 0);
}

// Função para remover um elemento da fila
void desenfileirar() {
  if (isFilaVazia()) {
    Serial.println("Erro: Fila vazia");
    return;
  }
  frente = (frente + 1) % TAMANHO_FILA; // Incrementa circularmente o índice frontal
  tamanhoAtual--;
}

// Função para adicionar um elemento à fila
void enfileirar(TDadosLora novoDado) {
  if (isFilaCheia()) {
    desenfileirar();
  }
  traseira = (traseira + 1) % TAMANHO_FILA; // Incrementa circularmente o índice traseiro
  fila[traseira] = novoDado; // Adiciona o novo elemento à fila
  tamanhoAtual++;
}

// Função para imprimir a fila
void imprimirFila() {
  if (isFilaVazia()) {
    Serial.println("Fila está vazia");
    return;
  }
  
  Serial.println("Conteúdo da fila:");
  int indiceAtual = frente; // Começa pelo início da fila
  
  for (int i = 0; i < tamanhoAtual; i++) {
    TDadosLora dado = fila[indiceAtual];
    
    // Imprimindo os campos da estrutura
    Serial.print("Contador: ");
    Serial.print(dado.contador);
    Serial.print(", ID Dispositivo: ");
    Serial.print(dado.id_dispositivo);
    Serial.print(", Tipo Mensagem: ");
    Serial.print(dado.tipo_mensagem);
    Serial.print(", Mensagem: ");
    Serial.println(dado.mensagem);
    
    // Incrementa circularmente o índice para o próximo elemento
    indiceAtual = (indiceAtual + 1) % TAMANHO_FILA;
  }
  Serial.println("");
}

int cont = 1;

TDadosLora capturar_dados(){ /* Captura as informações dos sensores para transmissão a cada período de tempo definido */
  TDadosLora dados_transmitir;
  dados_transmitir.contador = cont;
  dados_transmitir.id_dispositivo = id_dispositivo;
  dados_transmitir.tipo_mensagem = 1;
  memset(dados_transmitir.mensagem, 0, sizeof(dados_transmitir.mensagem));
  sprintf(dados_transmitir.mensagem, "Ola mundo");
  cont++;

  return dados_transmitir;
}

void setup() 
{
  /* Monitor Serial */
  Serial.begin(115200);

  /* Preparando a inicialização do display OLED */
  pinMode(OLED_RST, OUTPUT);
  digitalWrite(OLED_RST, LOW);
  delay(20);
  digitalWrite(OLED_RST, HIGH);

  /* Inicialização do display OLED */
  Wire.begin(OLED_SDA, OLED_SCL);
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3c, false, false)) { // Address 0x3C for 128x32
    Serial.println(F("Falha no Display Oled"));
    for(;;); // Don't proceed, loop forever
  }

  /* Mensagem inicial */
  Serial.println("TRANSMISSOR LORA");
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setTextSize(1);
  display.setCursor(0,0);
  display.print("Transmissor LoRa");
  display.display();
  delay(2000);
  
  while(init_comunicacao_lora() == false); /* Tenta, até obter sucesso na comunicacao com o chip LoRa */

  delay(2000);

  /* Imprimir mensagem dizendo que está aguardando o funcionamento */
  aguardando_dados_display();
}

unsigned long tempoAnterior1 = 0;
unsigned long tempoAnterior2 = 0;
const long intervalo1 = 4000;
const long intervalo2 = 2000;
TDadosLora dados_enviados = {0, id_dispositivo, 1, ""}; /* Útimo pacote que foi transmitido */
bool isInit = true; /* Verifica se está na primeira iteração */

void loop()
{
  if (millis() - tempoAnterior1 >= intervalo1) { /* Captura os dados a cada intervalo de tempo e decide se vai enviar ou armazenar */
    TDadosLora dados_enviar = capturar_dados(); /* Dados capturados para envio ou armazenamento */
    tempoAnterior1 = millis();
    
    if(tamanhoAtual == 0) { /* O armazenamento está vazio */  
      if(isInit || dados_enviados.contador == dados_receber.contador) { /* Significa que o pacote foi recebido com sucesso */
        dados_enviados = enviar_dados_lora(dados_enviar);
        isInit = false;
        escreve_medicoes_display(dados_enviados, 1);
      } else {
        enfileirar(dados_enviados);
        enfileirar(dados_enviar);
        imprimirFila();
      }
    } else if(tamanhoAtual > 0) { /* O armazenamento possui dados */
      enfileirar(dados_enviar);
      imprimirFila();
    }
  }

  if (millis() - tempoAnterior2 >= intervalo2) { /* Captura os dados da fila para envio e, se obtiver sucesso, remove o elemento da fila */
    tempoAnterior2 = millis();
    
    if(tamanhoAtual > 0){
      TDadosLora dados_enviar = fila[frente]; /* Próximo da fila */
      
      if (dados_enviados.contador == dados_receber.contador){
        desenfileirar();
        imprimirFila();
      }
      dados_enviados = enviar_dados_lora(dados_enviar);
      escreve_medicoes_display(dados_enviar, 2);
    }
  }
  
  recebe_informacoes();
}
