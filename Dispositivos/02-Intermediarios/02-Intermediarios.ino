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
#define HIGH_GAIN_LORA     20 /* dBm */
#define BAND               915E6 /* 915MHz de frequencia */

uint32_t fatorE = 7;     /* Valor do fator de espalhamento */
int id_dispositivo = 2;  /* O id do dispositivo deve ser único para cada dispositivo */

/* Definicões do Display OLED */
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RST);

/* Estrutura do pacote de dados */
typedef struct __attribute__((__packed__))  
{
  int contador;
  int id_dispositivo; /* O dispositivo que enviou o pacote */
  int qtd_fila;        /* Quantidade de elementos que estão na fila */
  byte tipo_mensagem; /* Tipos de Mensagem: 1 - Envio da mensagem, 2 - Confirmação de recebimento */
  byte comando;       /* Tipos de comandos: 1 - Enfileirar, 2 - Desenfila */
  char mensagem[20];
} TDadosLora;

void aguardando_dados_display();
void escreve_medicoes_display(TDadosLora dados_lora);
void envia_medicoes_serial(TDadosLora dados_lora);
void envia_informacoes_lora(TDadosLora dados_lora);
bool init_comunicacao_lora(void);
void recebe_informacoes();

void aguardando_dados_display() {
  /* Imprimir mensagem dizendo para esperar a chegada dos dados */
  Serial.println("Aguardando dados...");
  
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Aguardando dados...");
  display.display();
  
  delay(200);
}

void escreve_medicoes_display(TDadosLora dados_lora)
{     
  display.clearDisplay();

  display.setCursor(0, 0);
  display.println("INTERMEDIARIO");

  display.setCursor(0, 10);
  display.print("Cont: ");
  display.println(dados_lora.contador);

  display.setCursor(0, 20);
  display.print("Tipo: ");
  display.print(dados_lora.tipo_mensagem);

  display.setCursor(0, 30);
  display.print("Mensagem: ");
  display.print(dados_lora.mensagem);
  
  display.display();
}

void envia_medicoes_serial(TDadosLora dados_lora) 
{
  Serial.println("INTERMEDIARIO");
  
  Serial.print("Contador: ");
  Serial.println(dados_lora.contador);

  Serial.print("Tipo Mensagem: ");
  Serial.println(dados_lora.tipo_mensagem);

  Serial.print("Mensagem: ");
  Serial.println(dados_lora.mensagem);
  
  Serial.println(" ");
}

void envia_informacoes_lora(TDadosLora dados_lora) 
{
  LoRa.beginPacket();
  LoRa.write((unsigned char *)&dados_lora, sizeof(TDadosLora));
  LoRa.endPacket();
}

bool init_comunicacao_lora(void)
{
    bool status_init = false;
    Serial.println("[LoRa Receptor] Tentando iniciar comunicacao com o radio LoRa...");
    SPI.begin(SCK_LORA, MISO_LORA, MOSI_LORA, SS_PIN_LORA);
    LoRa.setPins(SS_PIN_LORA, RESET_PIN_LORA, LORA_DEFAULT_DIO0_PIN);
    
    if (!LoRa.begin(BAND)) {
      status_init = false;
      
      Serial.println("[LoRa Receptor] Comunicacao com o radio LoRa falhou. Nova tentativa em 1 segundo...");        
      
      display.clearDisplay();
      display.setCursor(0, 0);
      display.println("Radio LoRa");
      display.setCursor(0, 10);
      display.println("Status: Conectando...");
      display.setCursor(0, 20);
      display.println("Tentativas: Cada 1s");
      display.display();

      delay(1000);
    } else {
      status_init = true;
      LoRa.setSpreadingFactor(fatorE); /* Fator de Espalhamento */
      LoRa.setTxPower(HIGH_GAIN_LORA); /* Configura o ganho do receptor LoRa para 20dBm, o maior ganho possível (visando maior alcance possível) */ 
      LoRa.setSignalBandwidth(125E3);  /* Largura de banda fixa de 125 kHz *//* Suporta valores: 7.8E3, 10.4E3, 15.6E3, 20.8E3, 31.25E3, 41.7E3, 62.5E3, 125E3, 250E3 e 500E3 */
      LoRa.setCodingRate4(5);          /* Taxa de código - Suporta valores entre 5 e 8 */
      LoRa.setSyncWord(0x55);          /* Palavra de sincronização. Deve ser a mesma no transmissor e receptor */
    }

    return status_init;
}

void recebe_informacoes(){
  TDadosLora dados_lora;
  char byte_recebido;
  int tam_pacote = LoRa.parsePacket(); /* Verifica se chegou alguma informação do tamanho esperado */
  char * ptInformaraoRecebida = NULL;
  
  if (tam_pacote == sizeof(TDadosLora)) {
    ptInformaraoRecebida = (char *)&dados_lora; /* Recebe os dados conforme protocolo */
    
    while (LoRa.available())
    {
        byte_recebido = (char)LoRa.read();
        *ptInformaraoRecebida = byte_recebido;
        ptInformaraoRecebida++;
    }

    envia_medicoes_serial(dados_lora);
    escreve_medicoes_display(dados_lora);
    
    if (dados_lora.tipo_mensagem == 1){ /* Mensagem recebida com sucesso */
      dados_lora.tipo_mensagem = 2; /* Confirmação de recebimento */
      envia_informacoes_lora(dados_lora); /* Envia a confirmação de recebimento da mensagem */
    }
  }
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
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3c, false, false)) { /* Endereço 0x3C para 128 x 32 */
    Serial.println(F("Falha no Display OLED"));
    for(;;); /* Loop infinito */
  } else {
    display.clearDisplay();
    display.setTextColor(WHITE);
    display.setTextSize(1);
  }
  
  while(init_comunicacao_lora() == false); /* Tenta, até obter sucesso na comunicacao com o chip LoRa */

  /* Imprimir mensagem dizendo para esperar a chegada dos dados */
  aguardando_dados_display();
}

void loop()
{ 
  recebe_informacoes();
}
