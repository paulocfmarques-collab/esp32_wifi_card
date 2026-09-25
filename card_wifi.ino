#include <WiFi.h>
#include <WiFiUdp.h>
#include <WebServer.h>
#include <Preferences.h>
#include "esp_system.h"
#include <SPI.h>
#include <SD.h>

// Pinos personalizados
#define SD_CS 13
#define SD_MOSI 23
#define SD_MISO 19
#define SD_SCK 18

#define LED_AZUL 2
#define LED_VERDE 15

#define BOTAO_RESET 0 // Pino do botão de Reset (G4 conectado ao GND)

unsigned long tempoVerde = 0;
unsigned long tempoAzul = 0;

bool estadoVerde = false;
bool estadoAzul = false;

bool blinkAtivo = false;
bool estadoLed = false;

unsigned long ultimoToggle = 0;
unsigned long intervaloBlink = 500; // ms

// Podemos usar VSPI (padrão)
SPIClass spiSD(VSPI);

WebServer server(80);
Preferences prefs;

// HTML da página de configuração
const char* htmlPage PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="pt-BR">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Configuração WiFi</title>
<style>
  body { font-family: Arial, sans-serif; margin: 40px; background-color: #f4f4f9; text-align: center; }
  .container { background: white; max-width: 300px; margin: auto; padding: 20px; border-radius: 8px; box-shadow: 0 4px 8px rgba(0,0,0,0.1); }
  input { width: 100%; padding: 8px; margin: 10px 0; box-sizing: border-box; }
  input[type="submit"] { background: #007bff; color: white; border: none; cursor: pointer; }
</style>
</head>
<body>
<div class="container">
  <h2>Configuração WiFi</h2>
  <form action="/salvar" method="POST">
    <label>SSID:</label>
    <input type="text" name="ssid" placeholder="Nome da rede" required>

    <label>Senha:</label>
    <input type="password" name="senha" placeholder="Senha da rede">

    <input type="submit" value="Salvar">
  </form>
</div>
</body>
</html>
)rawliteral";

WiFiUDP udp;
const int udpPort = 4210;

void salvarWifi() 
{
  Serial.println("===== SALVAR WIFI =====");
  String novoSSID = server.arg("ssid");
  String novaSenha = server.arg("senha");

  prefs.begin("wifi", false);
  prefs.putString("ssid", novoSSID);
  prefs.putString("senha", novaSenha);
  prefs.end();

  Serial.println("Dados gravados");
  server.send(200, "text/html", "<h2>Configuracao salva! O ESP32 esta reiniciando...</h2>");
  delay(2000);
  ESP.restart();
}

void iniciarPortal() 
{
  WiFi.mode(WIFI_AP);
  WiFi.softAP("ESP32_CONFIG");
  Serial.println("\nPortal WiFi iniciado");
  Serial.print("Conecte-se e acesse o IP: ");
  Serial.println(WiFi.softAPIP());

  server.on("/", HTTP_GET, []() {
      server.send(200, "text/html", htmlPage);
  });
  server.on("/salvar", HTTP_POST, salvarWifi);
  server.begin();
}

bool conectarWifi() 
{
  prefs.begin("wifi", true);
  String ssid = prefs.getString("ssid", "");
  String password = prefs.getString("senha", "");
  prefs.end();

  if (ssid == "") return false;

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), password.c_str());
  Serial.print("Conectando a rede: "); Serial.println(ssid);

  int tentativas = 0;
  while (WiFi.status() != WL_CONNECTED && tentativas < 20) 
  {
    delay(500);
    Serial.print(".");
    digitalWrite(LED_AZUL, !digitalRead(LED_AZUL)); 
    tentativas++;
  }
  digitalWrite(LED_AZUL, LOW);
  return WiFi.status() == WL_CONNECTED;
}

void zerarConfiguracoes() 
{
  Serial.println("\n===== APAGANDO CONFIGURAÇÕES DE WI-FI =====");

  prefs.begin("wifi", false);
  prefs.clear(); // Apaga o SSID e a Senha salvos
  prefs.end();
  
  Serial.println("Memoria limpa com sucesso!");
  
  // Resposta final via UDP antes de reiniciar
  udp.beginPacket(udp.remoteIP(), udp.remotePort());
  udp.print("WiFi zerado. Reiniciando em modo Portal...\n");
  escreveArquivo("/log.txt", "WiFi zerado. Reiniciando em modo Portal...\n");
  udp.endPacket();
  
  // Pisca o LED rápido como feedback visual
  for(int i=0; i<10; i++) {
    digitalWrite(LED_AZUL, HIGH); delay(100);
    digitalWrite(LED_AZUL, LOW); delay(100);
  }
  
  ESP.restart(); // Reinicia o ESP32
}

void executa_comando(String cmd)
{
  char *pLinha = (char *) calloc(255, sizeof(char));

  Serial.print("Comando recebido: ");
  Serial.println(cmd);
  sprintf(pLinha, "Comando recebido: %s\n", cmd.c_str());
  escreveArquivo("/log.txt", pLinha);

  if (cmd == "RESET_WIFI") // Comando de RESET_WIFI
  {
    zerarConfiguracoes(); //Chama a função que limpa a memória e reinicia
  }
  else if (cmd == "LED_ON") // Comando para ligar o LED
  {
    blinkAtivo = false;
    estadoLed = true;
    digitalWrite(LED_AZUL, HIGH);

    udp.beginPacket(udp.remoteIP(), udp.remotePort());
    udp.print("LED ligado\n");
    escreveArquivo("/log.txt", "LED ligado\n");
    udp.endPacket();
  }
  else if (cmd == "LED_OFF") // Comando para desligar o LED
  {
    blinkAtivo = false;
    estadoLed = false;
    digitalWrite(LED_AZUL, LOW);

    udp.beginPacket(udp.remoteIP(), udp.remotePort());
    udp.print("LED desligado\n");
    escreveArquivo("/log.txt", "LED desligado\n");
    udp.endPacket();
  }
  else if (cmd.startsWith("LED_PISCA")) // Comando para piscar o LED uma quantidade de vezes
  {
    int piscadas = 10;
    int tempo = 250;

    int p1 = cmd.indexOf(':');
    int p2 = cmd.indexOf(':', p1 + 1);

    if (p1 > 0 && p2 > 0) 
    {
      piscadas = cmd.substring(p1 + 1, p2).toInt();
      tempo = cmd.substring(p2 + 1).toInt();
    }

    for (int i = 0; i < piscadas; i++) 
    {
      digitalWrite(LED_AZUL, HIGH);  delay(tempo);
      digitalWrite(LED_AZUL, LOW);   delay(tempo);
    }

    udp.beginPacket(udp.remoteIP(), udp.remotePort());
    sprintf(pLinha, "LED piscou %d vezes com %d ms\n", piscadas, tempo);
    udp.printf(pLinha);
    escreveArquivo("/log.txt", pLinha);
    udp.endPacket();
  }
  else if (cmd.startsWith("LED_BLINK")) // Comando para piscar led com tempo
  {
    int p = cmd.indexOf(':');

    if (p > 0) 
    {
      intervaloBlink = cmd.substring(p + 1).toInt();
    }

    blinkAtivo = true;

    udp.beginPacket(udp.remoteIP(), udp.remotePort());
    sprintf(pLinha, "Blink iniciado (%lu ms)\n", intervaloBlink);
    udp.printf(pLinha);
    escreveArquivo("/log.txt", pLinha);
    udp.endPacket();
  }
  else if (cmd == "TEMP") // Comando ler a temperatura
  {
    udp.beginPacket(udp.remoteIP(), udp.remotePort());
    sprintf(pLinha, "CPU Temp: %.2f\n", temperatureRead());
    udp.printf(pLinha);
    escreveArquivo("/log.txt", pLinha);
    udp.endPacket();
  }
  else if (cmd == "CPU") // Informações sobre a CPU
  {
    udp.beginPacket(udp.remoteIP(), udp.remotePort());
    // udp.printf("Modelo: %s\n", ESP.getChipModel());
    // udp.printf("Revisao: %d\n", ESP.getChipRevision());
    // udp.printf("Nucleos: %d\n", ESP.getChipCores());
    // udp.printf("CPU: %d MHz\n", ESP.getCpuFreqMHz());
    // udp.printf("RAM livre: %u bytes\n", ESP.getFreeHeap());
    sprintf(pLinha, "Modelo: %s\nRevisao: %d\nNucleos: %d\nCPU: %d MHz\nRAM livre: %u bytes\n", 
            ESP.getChipModel(), ESP.getChipRevision(), ESP.getChipCores(), ESP.getCpuFreqMHz(), ESP.getFreeHeap());
    udp.printf(pLinha);
    escreveArquivo("/log.txt", pLinha);
    udp.endPacket();
  }
  else if (cmd == "RAM") // Informações sobre a RAM
  {
    udp.beginPacket(udp.remoteIP(), udp.remotePort());
    // udp.printf("Heap livre: %u\n", ESP.getFreeHeap());
    // udp.printf("Menor heap livre: %u\n", ESP.getMinFreeHeap());
    // udp.printf("Maior bloco livre: %u\n", ESP.getMaxAllocHeap());
    sprintf(pLinha, "Heap livre: %u\nMenor heap livre: %u\nMaior bloco livre: %u\n",
            ESP.getFreeHeap(), ESP.getMinFreeHeap(), ESP.getMaxAllocHeap());
    udp.printf(pLinha);
    escreveArquivo("/log.txt", pLinha);
    udp.endPacket();
  }
  else if (cmd == "FLASH") // Informações sobre a flash
  {
    udp.beginPacket(udp.remoteIP(), udp.remotePort());
    udp.printf("Flash total: %u\n", ESP.getFlashChipSize());
    udp.printf("Velocidade Flash: %u\n", ESP.getFlashChipSpeed());
    udp.printf("Tamanho Sketch: %u\n", ESP.getSketchSize());
    udp.printf("Espaco livre: %u\n", ESP.getFreeSketchSpace());
    sprintf(pLinha, "Flash total: %u\nVelocidade Flash: %u\nTamanho Sketch: %u\nEspaco livre: %u\n",
            ESP.getFlashChipSize(), ESP.getFlashChipSpeed(), ESP.getSketchSize(), ESP.getFreeSketchSpace());
    udp.printf(pLinha);
    escreveArquivo("/log.txt", pLinha);
    udp.endPacket();
  }
  else if (cmd == "INIT") // Motivo do reset
  {
    udp.beginPacket(udp.remoteIP(), udp.remotePort());
    sprintf(pLinha, "Motivo reset: %d\n", esp_reset_reason());
    udp.printf(pLinha);
    escreveArquivo("/log.txt", pLinha);
    udp.endPacket();
  }
  else if (cmd == "UPTIME") // Tempo ligado
  {
    udp.beginPacket(udp.remoteIP(), udp.remotePort());
    sprintf(pLinha, "Uptime: %lu ms\n", millis());
    udp.printf(pLinha);
    escreveArquivo("/log.txt", pLinha);
    udp.endPacket();
  }
  else if (cmd == "MAC") // MAC address
  {
    udp.beginPacket(udp.remoteIP(), udp.remotePort());
    sprintf(pLinha, "MAC: %s\n", WiFi.macAddress().c_str());
    udp.printf(pLinha);
    escreveArquivo("/log.txt", pLinha);
    udp.endPacket();
  }
  else if (cmd == "NET_INFO") // MAC address
  {
    udp.beginPacket(udp.remoteIP(), udp.remotePort());
    // udp.printf("IP: ");
    // udp.println(WiFi.localIP());
    // udp.printf("Gateway: ");
    // udp.println(WiFi.gatewayIP());
    // udp.printf("Mascara de rede: ");
    // udp.println(WiFi.subnetMask());
    // udp.printf("RSSI: %d dbm\n", WiFi.RSSI());
    // udp.printf("Nome da Rede: %s\n", WiFi.SSID());
    sprintf(pLinha, "IP: %s\nGateway: %s\nMascara de rede: %s\nRSSI: %d dbm\nNome da Rede: %s\n",
            WiFi.localIP().toString().c_str(), WiFi.gatewayIP().toString().c_str(), WiFi.subnetMask().toString().c_str(), 
            WiFi.RSSI(), WiFi.SSID());
    udp.printf(pLinha);
    escreveArquivo("/log.txt", pLinha);
    udp.endPacket();
  }
  else if (cmd == "TESTE_SERIAL") 
  {
    leArquivo("/teste.txt");
    udp.beginPacket(udp.remoteIP(), udp.remotePort());
    udp.println("Arquivo 'teste.txt' enviado para a porta serial.");
    escreveArquivo("/log.txt", "Arquivo 'teste.txt' enviado para a porta serial\n");
    udp.endPacket();
  }
  else if (cmd == "LOG_SERIAL") 
  {
    leArquivo("/log.txt");
    udp.beginPacket(udp.remoteIP(), udp.remotePort());
    udp.println("Arquivo 'log.txt' enviado para a porta serial.");
    escreveArquivo("/log.txt", "Arquivo 'log.txt' enviado para a porta serial\n");
    udp.endPacket();
  }
  else if (cmd == "TESTE_UDP") 
  {
    enviaArquivo("/teste.txt");
    udp.beginPacket(udp.remoteIP(), udp.remotePort());
    escreveArquivo("/log.txt", "Arquivo 'teste.txt' enviado por UDP\n");
    udp.endPacket();
  }
  else if (cmd == "LOG_UDP")
  {
    enviaArquivo("/log.txt");
    udp.beginPacket(udp.remoteIP(), udp.remotePort());
    udp.println("Arquivo 'log.txt' enviado por UDP");
    escreveArquivo("/log.txt", "Arquivo 'log.txt' enviado por UDP\n");
    udp.endPacket();
  }
  else if (cmd == "DEL_LOG")
  {
    udp.beginPacket(udp.remoteIP(), udp.remotePort());
    if(SD.remove("/log.txt"))
    {
      udp.println("Arquivo 'log.txt' deletado!");
    }
    else
    {
      udp.println("Falaha deletado o arquivo 'log.txt'!");
    }
    udp.endPacket();
  }
  else if (cmd == "LOG_EXIST")
  {
    udp.beginPacket(udp.remoteIP(), udp.remotePort());
    if(SD.exists("/log.txt"))
    {
      udp.println("Arquivo 'log.txt' esta no cartao SD.");
    }
    else
    {
      udp.println("Arquivo 'log.txt' NAO esta no cartao SD.");
    }
    udp.endPacket();
  }
  else // Comando invalido
  {
    udp.beginPacket(udp.remoteIP(), udp.remotePort());
    udp.print("Comando desconhecido\n");
    escreveArquivo("/log.txt", "Comando desconhecido\n");
    udp.endPacket();
  }
  free(pLinha);
}

void enviaArquivo(char* filename)
{
  udp.beginPacket(udp.remoteIP(), udp.remotePort());
  
  if (!SD.exists(filename)) 
  {
    udp.printf("Arquivo '%s' não encontrado.\n", filename);
  }
  else 
  {
    File file = SD.open(filename);
    if (!file) 
    {
      udp.println("Erro ao abrir o arquivo.");
    }
    else
    {
      udp.println("\nConteúdo do arquivo:");
      while (file.available()) {
        String line = file.readStringUntil('\n');
        udp.println(line);
      }
      file.close();
      udp.println("Leitura finalizada.");
    }
  }
  udp.endPacket();
}

void leArquivo(char* filename)
{
  if (!SD.exists(filename)) {
    Serial.printf("Arquivo '%s' não encontrado.\n", filename);
    return;
  }

  File file = SD.open(filename);
  if (!file) {
    Serial.println("Erro ao abrir o arquivo.");
    return;
  }

  Serial.println("Conteúdo do arquivo:");
  while (file.available()) {
    String line = file.readStringUntil('\n');
    Serial.println(line);
  }

  file.close();
  Serial.println("Leitura finalizada.");
}

void escreveArquivo(char* filename, char* sLinha)
{
  File file;

  if(SD.exists(filename))
  {
     file = SD.open(filename, FILE_APPEND);
  }
  else
  {
     file = SD.open(filename, FILE_APPEND, true);
  }
  file.write((uint8_t*)sLinha, strlen(sLinha));
  file.close();
}

void setup() {

  pinMode(LED_VERDE, OUTPUT);
  pinMode(LED_AZUL, OUTPUT);

  pinMode(BOTAO_RESET, INPUT_PULLUP);

  delay(3000);
  Serial.begin(115200);
  delay(1000);
  Serial.println("Inicializando cartão SD...");

  // Inicializa SPI com os pinos definidos
  spiSD.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);

  // Inicializa o SD usando SPI customizado
  if (!SD.begin(SD_CS, spiSD)) {
    Serial.println("Falha ao montar o cartão SD.");
    return;
  }

  Serial.println("Cartão SD montado com sucesso.");

  if (conectarWifi()) 
  {
    Serial.print("\nConectado com sucesso! IP obtido: ");
    Serial.println(WiFi.localIP());
    server.stop();
    escreveArquivo("/log.txt","Conectado com sucesso! IP obtido: ");
    escreveArquivo("/log.txt",(char *)WiFi.localIP().toString().c_str());
    escreveArquivo("/log.txt","\n");
    WiFi.softAPdisconnect(true);
    udp.begin(udpPort);
  } 
  else 
  {
    iniciarPortal();
  }
}

void loop() {

  unsigned long agora = millis();

  if (WiFi.status() != WL_CONNECTED) 
  {
    server.handleClient();
  }
  else if (digitalRead(BOTAO_RESET) == LOW)
  {
    zerarConfiguracoes();
  }
  else 
  {
    // Código UDP (só roda se estiver conectado no Wi-Fi)
    if (udp.parsePacket()) 
    {
      char packetBuffer[255];
      int len = udp.read(packetBuffer, sizeof(packetBuffer) - 1);

      if (len > 0) 
      {
        packetBuffer[len] = '\0';
      }
      executa_comando(String(packetBuffer));
    }
  }

  // LED VERDE - 1 segundo
  if (agora - tempoVerde >= 1000) {
    tempoVerde = agora;
    estadoVerde = !estadoVerde;
    digitalWrite(LED_VERDE, estadoVerde);
  }
  
  // Controle do Blink assíncrono
  if (blinkAtivo) 
  {
    unsigned long agora = millis();
    if (agora - ultimoToggle >= intervaloBlink) 
    {
      ultimoToggle = agora;
      estadoLed = !estadoLed;
      digitalWrite(LED_AZUL, estadoLed);
    }
  }  
  
  // // LED AZUL - 500 ms
  // if (agora - tempoAzul >= 500) {
  //   tempoAzul = agora;
  //   estadoAzul = !estadoAzul;
  //   digitalWrite(LED_AZUL, estadoAzul);
  // }

}
