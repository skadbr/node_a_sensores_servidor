/*
  ==========================================================
  NODE A - SERVIDOR WEBSOCKET (VERSAO COMPLETA - SENSORES + AUDIO + WEB)
  ==========================================================
  Wemos D1 Mini (ESP8266)

  Funcao:
   - Cria uma rede WiFi propria (modo Access Point)
   - Le sensor de temperatura (termopar tipo K via modulo MAX6675) e
     sensor de pressao (PS10, saida analogica 1-5V, 0-10bar), enviando
     os dados via WebSocket a cada 500ms
   - Recebe evento (som1, som2, som3, ...) de QUALQUER cliente
     conectado e toca o audio correspondente no modulo DFPlayer Mini
     + mini alto-falante
   - Ao um cliente se conectar, informa automaticamente quantos
     audios existem no cartao SD. Essa quantidade e lida do cartao
     NA PRIMEIRA CONEXAO recebida (nao no boot) - assim, mesmo se o
     cartao SD ainda nao estiver pronto logo ao ligar (boot "frio"),
     a leitura so acontece quando o primeiro cliente ja estiver
     conectando, dando mais tempo do cartao inicializar. O resultado
     fica em cache: reconexoes seguintes reaproveitam a mesma
     contagem, sem reler a SD toda hora. Durante as tentativas de
     leitura, o cliente recebe mensagens de progresso ("tentativa").
   - SUPORTA MULTIPLOS CLIENTES SIMULTANEOS: o Node B (display
     Nextion) e um NAVEGADOR WEB podem estar conectados ao mesmo
     tempo - o Node A responde/transmite para todos igualmente (o
     WebSocketsServer ja trata isso nativamente, sem necessidade de
     logica extra).
   - Serve uma PAGINA WEB (HTTP, porta 80) com painel completo
     (gauges de temperatura/pressao, selecao de som, status de
     conexao), tema visual "Batmovel" a mesma linha do Nextion.
     Acesse http://192.168.4.1/ de qualquer navegador conectado na
     rede WiFi do Node A para usar.

  Bibliotecas necessarias (Gerenciador de Bibliotecas do Arduino IDE):
   - ESP8266WiFi            (ja vem com o core do ESP8266)
   - ESP8266WebServer        (ja vem com o core do ESP8266)
   - WebSockets             (autor: Markus Sattler / Links2004)
   - ArduinoJson             (autor: Benoit Blanchon)
   - MAX6675                 (autor: Adafruit - busque "MAX6675" no
     Gerenciador de Bibliotecas)
   - SoftwareSerial          (ja vem com o core do ESP8266)
   - DFRobotDFPlayerMini     (autor: DFRobot)
  (O sensor PS10 nao precisa de biblioteca - e lido direto pelo ADC)

  IMPORTANTE: instale o suporte a placas ESP8266 no Arduino IDE
  (Preferencias > URLs Adicionais para Gerenciadores de Placas:
  http://arduino.esp8266.com/stable/package_esp8266com_index.json).
  Selecione a placa que fez o upload funcionar no seu caso (ex:
  "LOLIN(WEMOS) D1 R1"). O codigo usa NUMEROS DE GPIO diretamente
  (ex: 5 em vez de D1), o que elimina qualquer ambiguidade entre
  placas diferentes - a ligacao fisica sempre sera a mesma.

  Ligacoes (numero de GPIO real -> rotulo correspondente no
  Wemos D1 Mini, caso queira conferir pela serigrafia da placa):
   - MAX6675 SCK  -> GPIO0  (rotulo D3)
   - MAX6675 CS   -> GPIO2  (rotulo D4)
   - MAX6675 SO   -> GPIO14 (rotulo D5)
   - MAX6675 VCC  -> 3V3, GND -> GND
   - PS10 fio vermelho (VCC)  -> 5V (fonte externa ou pino 5V do Wemos)
   - PS10 fio preto (GND)     -> GND (comum com o Wemos)
   - PS10 fio verde (sinal, 1-5V) -> DIVISOR DE TENSAO -> pino A0 do Wemos
     ATENCAO - OBRIGATORIO usar um divisor de tensao entre o sinal do
     PS10 e o pino A0! O sinal do sensor chega a 5V, mas o A0 do Wemos
     D1 Mini so aceita ate ~3.2V - ligar direto DANIFICA a entrada
     analogica do ESP8266.
     Circuito do divisor (2 resistores em serie entre o sinal e o GND,
     com o pino A0 ligado no meio dos dois):
       Sinal do PS10 --[R1 = 10k]-- (no A0) --[R2 = 15k]-- GND
     Essa proporcao (10k/15k) reduz o sinal de 5V para 3.0V no A0,
     com boa margem de seguranca.
   - DFPlayer Mini RX -> GPIO4 (rotulo D2) do Wemos (recomendado resistor 1k em serie)
   - DFPlayer Mini TX -> GPIO5 (rotulo D1) do Wemos
   - DFPlayer Mini VCC -> 5V, GND -> GND (GND comum com o Wemos)
   - Mini alto-falante -> saida SPK_1/SPK_2 do DFPlayer Mini

  OBS: o ESP8266 so tem uma UART de hardware, ja usada pelo Serial
  (debug/gravacao), por isso o DFPlayer e ligado via SoftwareSerial.

  IMPORTANTE - Calibracao do PS10:
  A conversao ADC -> tensao no pino A0 do Wemos D1 Mini varia um pouco
  de placa para placa (o valor TENSAO_REF_PLACA abaixo e uma estimativa
  tipica). Para calibrar com precisao:
   1) Meca com um multimetro a tensao real no fio verde do PS10 (sinal)
      em repouso (pressao atmosferica/zero) - deve estar proximo de 1V.
   2) Compare com o valor impresso no Serial Monitor (que mostra a
      tensao calculada pelo codigo).
   3) Se houver diferenca, ajuste a constante TENSAO_REF_PLACA
      proporcionalmente ate os valores baterem.

  TARA AUTOMATICA DO ZERO: alem da calibracao acima (que ajusta a
  PRECISAO da leitura), o codigo tambem faz uma TARA automatica no
  boot (funcao calibrarZeroPressao()): ele mede a pressao "crua" logo
  ao ligar, assumindo que nesse momento nao ha nada injetado no
  sensor (so a pressao atmosferica local), e usa esse valor como
  referencia de 0 bar - descontando esse offset de todas as leituras
  seguintes. Isso faz o mostrador sempre comecar do 0 bar em repouso,
  independente da altitude/pressao atmosferica de onde o Node A for
  ligado. IMPORTANTE: nao ligue o Node A com pressao ja injetada no
  sensor, ou a tara vai calibrar errado (vai considerar aquele
  momento como "zero").

  IMPORTANTE - Cartao microSD do DFPlayer Mini:
   - Formate o cartao em FAT32.
   - Crie uma pasta chamada "01" (dois digitos) na raiz do cartao
     - NAO use "mp3"; a pasta numerada e necessaria para o DFPlayer
       conseguir informar a quantidade de arquivos.
   - Coloque os arquivos de audio dentro dela, nomeados
     EXATAMENTE como: 001.mp3, 002.mp3, 003.mp3, 004.mp3, ...
     (tres digitos, sequenciais, sem pular numeros)
   - som1 -> 001.mp3 | som2 -> 002.mp3 | som3 -> 003.mp3 | etc.
   - Basta adicionar/remover arquivos na pasta "01" e reiniciar o
     Node A - a quantidade e detectada na primeira conexao do Node B
     apos o boot e fica em cache, sem precisar editar o codigo.
  ==========================================================
*/

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WebSocketsServer.h>
#include <ArduinoJson.h>
#include <max6675.h>
#include <SoftwareSerial.h>
#include <DFRobotDFPlayerMini.h>

// ---------- Configuracao da rede WiFi (Node A cria a rede) ----------
const char* AP_SSID = "ESP32_Sensores"; // nome mantido para nao precisar mudar o Node B
const char* AP_PASS = "12345678";       // minimo 8 caracteres

// ---------- Quantidade de audios na pasta "01" do cartao SD ----------
// Detectada automaticamente na primeira conexao do Node B (nao no
// boot) - nao precisa editar aqui, e o valor fica em cache.
int numAudios = 0;

// ---------- Pinos (GPIO diretos - evita ambiguidade entre variantes de placa) ----------
// Rotulos correspondentes na serigrafia do Wemos D1 Mini: D3, D4, D5, D2, D1
#define PINO_MAX6675_SCK  0    // D3 no Wemos D1 Mini
#define PINO_MAX6675_CS   2    // D4 no Wemos D1 Mini
#define PINO_MAX6675_SO   14   // D5 no Wemos D1 Mini
#define PINO_PRESSAO_ADC  A0   // unica entrada analogica do Wemos D1 Mini
#define PINO_DFPLAYER_RX  5    // D1 no Wemos D1 Mini - recebe do TX do DFPlayer
#define PINO_DFPLAYER_TX  4    // D2 no Wemos D1 Mini - envia para o RX do DFPlayer

// ---------- Objetos ----------
WebSocketsServer webSocket(81);
ESP8266WebServer httpServer(80); // serve a pagina web do painel
MAX6675 termopar(PINO_MAX6675_SCK, PINO_MAX6675_CS, PINO_MAX6675_SO);
SoftwareSerial serialDFPlayer(PINO_DFPLAYER_RX, PINO_DFPLAYER_TX); // RX, TX
DFRobotDFPlayerMini dfPlayer;

float ultimaTemperaturaValida = 0;

// Temperatura suavizada por filtro de media movel exponencial (EMA) -
// reduz o ruido natural do MAX6675 (leitura pode oscilar +-1C entre
// amostras por ruido eletrico), evitando que o valor exibido fique
// "pulando" entre graus vizinhos sem uma mudanca real de temperatura.
float temperaturaFiltrada = NAN; // NAN = ainda sem leitura, inicializa na primeira

// Fator de suavizacao do filtro (0 a 1): quanto MENOR, mais suave
// (mais resistente a ruido, porem mais lento pra acompanhar mudancas
// reais de temperatura). Quanto MAIOR, mais responsivo, porem mais
// sujeito a ruido. 0.15 da uma boa estabilidade sem ficar lento
// demais para mudancas reais.
const float FATOR_FILTRO_TEMPERATURA = 0.15;

// Offset de tara do sensor de pressao - calculado automaticamente no
// boot (assume que nesse momento nao ha pressao injetada, so a
// pressao atmosferica local). Esse valor e subtraido de toda leitura
// seguinte, entao a leitura "em repouso" sempre marca 0 bar,
// independente da altitude/pressao atmosferica do local.
float offsetPressaoBar = 0.0;

// ---------- Constantes de conversao do sensor de pressao PS10 ----------
// Tensao aproximada (em Volts) correspondente ao valor maximo do ADC
// (1023) no pino A0 do Wemos D1 Mini. Pode variar por placa - calibrar
// se necessario (ver instrucoes no cabecalho do arquivo).
const float TENSAO_REF_PLACA = 3.2;

// Proporcao do divisor de tensao externo (R1=10k, R2=15k):
// FATOR = (R1+R2)/R2 = 25/15 - usado para "desfazer" o divisor e
// recuperar a tensao real do sinal do PS10 a partir da tensao lida no A0.
const float FATOR_DIVISOR_EXTERNO = 25.0 / 15.0;

// Faixa de saida do PS10: 1V = 0 bar, 5V = 10 bar (sensor 0-10bar)
const float TENSAO_SENSOR_MIN = 1.0;
const float TENSAO_SENSOR_MAX = 5.0;
const float PRESSAO_MIN_BAR   = 0.0;
const float PRESSAO_MAX_BAR   = 10.0;

// ---------- Controle de envio periodico dos sensores ----------
unsigned long ultimoEnvio = 0;
// 500ms ainda parece quase instantaneo no mostrador (principalmente
// com o filtro de temperatura ja suavizando as leituras), mas gera
// bem menos trafego/carga no Node A que 300ms - isso da mais folga
// para o Node A processar os eventos de audio (som1/som2/...)
// prontamente, sem competir tanto com o envio continuo dos sensores.
const unsigned long INTERVALO_ENVIO = 500;

// ==========================================================
// Toca o arquivo NNN.mp3 dentro da pasta "01" do cartao SD.
// numero=1 -> 001.mp3 | numero=2 -> 002.mp3 | numero=3 -> 003.mp3
void tocarAudio(int numero) {
  dfPlayer.playFolder(1, numero); // pasta "01", faixa "numero"
  Serial.printf("[DFPLAYER] Tocando som%d.\n", numero);
}

// ==========================================================
float lerTemperatura() {
  float leitura = termopar.readCelsius();
  if (isnan(leitura)) {
    // NAN = termopar desconectado ou erro de leitura (MAX6675 requer
    // pelo menos 250ms entre leituras, ja garantido pelo INTERVALO_ENVIO)
    Serial.println("AVISO: falha na leitura do MAX6675 (termopar desconectado?). Mantendo ultima leitura.");
    return ultimaTemperaturaValida;
  }
  ultimaTemperaturaValida = leitura;

  // Aplica o filtro de media movel exponencial para suavizar o ruido
  if (isnan(temperaturaFiltrada)) {
    temperaturaFiltrada = leitura; // primeira leitura valida, inicializa direto
  } else {
    temperaturaFiltrada += FATOR_FILTRO_TEMPERATURA * (leitura - temperaturaFiltrada);
  }

  return temperaturaFiltrada;
}

// Le o sensor de pressao PS10 (analogico) e converte para bar.
// Retorna o valor "cru", sem descontar a tara (offset de calibracao).
float lerPressaoBruta() {
  // Faz a media de varias leituras para reduzir ruido do sinal analogico
  const int NUM_AMOSTRAS = 10;
  long soma = 0;
  for (int i = 0; i < NUM_AMOSTRAS; i++) {
    soma += analogRead(PINO_PRESSAO_ADC);
    delay(2);
  }
  float leituraADC = soma / (float)NUM_AMOSTRAS;

  float tensaoNoA0 = (leituraADC / 1023.0) * TENSAO_REF_PLACA;
  float tensaoSensor = tensaoNoA0 * FATOR_DIVISOR_EXTERNO;

  float pressaoBar = (tensaoSensor - TENSAO_SENSOR_MIN) /
                      (TENSAO_SENSOR_MAX - TENSAO_SENSOR_MIN) *
                      (PRESSAO_MAX_BAR - PRESSAO_MIN_BAR);

  return pressaoBar;
}

// Retorna a pressao com a tara aplicada (offsetPressaoBar
// descontado) - passo intermediario antes do ajuste fino de ganho.
float lerPressao() {
  return lerPressaoBruta() - offsetPressaoBar;
}

// ---------- Ajuste fino de ganho/offset residual ----------
// Depois da tara (zero), ainda pode sobrar um pequeno erro de GANHO
// (a leitura "abre" mais ou menos que o real conforme a pressao
// aumenta) - isso e diferente do offset de zero, que a tara ja
// corrige. Calculado empiricamente com 2 pontos de teste reais:
//   - Em repouso (pressao real = 0 bar): estava mostrando -0.1 bar
//   - Injetando 1.2 bar reais: estava mostrando 1.4 bar
// Resolvendo a reta "medido = GANHO * real + OFFSET" com esses 2
// pontos: GANHO ≈ 1.25 e OFFSET ≈ -0.1. A correcao abaixo inverte
// essa reta para recuperar o valor real a partir do medido.
//
// Para recalibrar no futuro com novos pontos de teste (P1_real,
// P1_medido) e (P2_real, P2_medido):
//   GANHO_RESIDUAL  = (P2_medido - P1_medido) / (P2_real - P1_real)
//   OFFSET_RESIDUAL = P1_medido - GANHO_RESIDUAL * P1_real
const float GANHO_RESIDUAL_PRESSAO  = 1.25;
const float OFFSET_RESIDUAL_PRESSAO = -0.1;

// Retorna a pressao final, com tara + ajuste fino de ganho aplicados.
// Esta e a funcao que deve ser usada no restante do codigo. O valor
// pode ser negativo (ex: vacuo/depressao), o que e esperado e normal
// - o gauge no Node B ja limita visualmente o ponteiro na escala
// configurada (-0.5 a 2 bar).
float lerPressaoCorrigida() {
  float pressaoTarada = lerPressao();
  return (pressaoTarada - OFFSET_RESIDUAL_PRESSAO) / GANHO_RESIDUAL_PRESSAO;
}

// Calibra o zero do sensor de pressao (tara). Deve ser chamada uma
// vez no boot, assumindo que nesse momento nao ha pressao injetada
// no sensor - so a pressao atmosferica do local. Faz varias leituras
// e usa a media, para reduzir o efeito de ruido pontual na calibracao.
void calibrarZeroPressao() {
  Serial.println("Calibrando zero do sensor de pressao (PS10)...");
  const int AMOSTRAS_CALIBRAGEM = 20;
  float soma = 0;
  for (int i = 0; i < AMOSTRAS_CALIBRAGEM; i++) {
    soma += lerPressaoBruta();
    delay(50);
  }
  offsetPressaoBar = soma / AMOSTRAS_CALIBRAGEM;
  Serial.printf("Offset de pressao calibrado: %.3f bar (sera descontado de todas as leituras)\n", offsetPressaoBar);
}

void enviarDadosSensores() {
  float temperatura = lerTemperatura();
  float pressao = lerPressaoCorrigida();

  JsonDocument doc;
  doc["temp"]  = temperatura;
  doc["press"] = pressao;

  String saida;
  serializeJson(doc, saida);

  webSocket.broadcastTXT(saida);
  Serial.println("[WS] Enviado: " + saida);
}

// Informa a um cliente especifico quantos audios estao disponiveis.
// Chamado sempre que um novo cliente (Node B) se conecta.
void enviarQuantidadeAudios(uint8_t num) {
  JsonDocument doc;
  doc["num_audios"] = numAudios;

  String saida;
  serializeJson(doc, saida);

  webSocket.sendTXT(num, saida);
  Serial.println("[WS] Enviado num_audios: " + saida);
}

// Le a quantidade de audios na pasta "01" do cartao SD, com
// retentativas (o cartao pode nao estar pronto logo apos ligar).
// So executa a busca completa se ainda nao tiver uma contagem
// valida - reconexoes seguintes reaproveitam o valor em cache.
void lerQuantidadeAudios(uint8_t num) {

    if (numAudios > 0) {
      Serial.println("Quantidade de audios ja conhecida, pulando releitura.");
      return;
    }

    delay(200); // pequena pausa antes de consultar o cartao

    // Tenta ler a quantidade de audios varias vezes. Logo apos ligar
    // a alimentacao (boot "frio"), o cartao SD as vezes ainda nao esta
    // totalmente pronto na primeira consulta e a leitura falha
    // (retorna -1) - mas funciona normalmente alguns instantes depois.
    // No reset (boot "quente"), a alimentacao ja estava estavel ha
    // mais tempo, entao costuma funcionar de primeira.
    const int MAX_TENTATIVAS_LEITURA_SD = 15;
    for (int tentativa = 1; tentativa <= MAX_TENTATIVAS_LEITURA_SD; tentativa++) {
      numAudios = dfPlayer.readFileCountsInFolder(1); // conta arquivos na pasta "01"
      if (numAudios > 0) {
        Serial.printf("Audios encontrados na pasta 01: %d (tentativa %d)\n", numAudios, tentativa);
        break;
      }
      Serial.printf("Tentativa %d: leitura da pasta 01 falhou (retornou %d). Tentando novamente...\n", tentativa, numAudios);

      JsonDocument doc;
      doc["tentativa"] = tentativa;
      String saida;
      serializeJson(doc, saida);
      webSocket.sendTXT(num, saida);

      delay(500);
    }

    if (numAudios <= 0) {
      Serial.println("ERRO: nao foi possivel ler a quantidade de audios na pasta 01 apos varias tentativas!");
      numAudios = 0; // evita enviar valor negativo ao Node B

      JsonDocument doc;
      doc["erro"] = "Falha ao ler audios na SD";
      String saida;
      serializeJson(doc, saida);
      webSocket.sendTXT(num, saida);
    }
}

// ==========================================================
// ---------- Pagina web do painel (tema "Batmovel") ----------
// Fica guardada na memoria flash (PROGMEM), nao consome RAM. Conecta
// via WebSocket na mesma porta 81 que o Node B ja usa - o Node A
// responde aos dois ao mesmo tempo, sem nenhuma logica extra
// necessaria no C++ (o WebSocketsServer ja suporta varios clientes).
// ==========================================================
// ---------- Imagens de fundo dos gauges (SVG, editaveis) ----------
// Cada uma fica guardada separadamente na memoria flash (PROGMEM) e
// e servida pelo Node A numa URL propria (/gauge_temp_bg.svg e
// /gauge_press_bg.svg). O ponteiro (agulha) NAO faz parte dessas
// imagens - ele e um SVG pequeno e transparente, desenhado por cima
// via JavaScript (ver PAGE_HTML mais abaixo), cuja logica de angulo
// e generica e funciona para qualquer fundo.
//
// PARA TROCAR O DESIGN DO FUNDO: basta substituir todo o conteudo
// entre os R"rawliteral( ... )rawliteral" abaixo pelo novo codigo SVG
// (gerado no Inkscape, CorelDraw, etc) e gravar de novo no Node A -
// nao e necessario mexer em mais nada no resto do codigo, contanto
// que a escala de valores usada no desenho continue batendo com as
// constantes TEMP_MIN/TEMP_MAX e PRESS_MIN/PRESS_MAX definidas no
// JavaScript (dentro de PAGE_HTML).


const char GAUGE_TEMP_BG_SVG[] PROGMEM = R"rawliteral(<svg xmlns="http://www.w3.org/2000/svg" xml:space="preserve" width="56.0916mm" height="56.0916mm" version="1.1" style="shape-rendering:geometricPrecision; text-rendering:geometricPrecision; image-rendering:optimizeQuality; fill-rule:evenodd; clip-rule:evenodd"
viewBox="0 0 571.11 571.11"
 xmlns:xlink="http://www.w3.org/1999/xlink"
 xmlns:xodm="http://www.corel.com/coreldraw/odm/2003">
 <defs>
  <style type="text/css">
   <![CDATA[
    .str2 {stroke:#FFCC00;stroke-width:4.04;stroke-miterlimit:22.9256;stroke-opacity:0.400000}
    .str6 {stroke:#FFCC00;stroke-width:5.09;stroke-miterlimit:22.9256;stroke-opacity:0.400000}
    .str1 {stroke:#3A2E10;stroke-width:16.16;stroke-miterlimit:22.9256}
    .str4 {stroke:#FF3030;stroke-width:2.69;stroke-miterlimit:22.9256}
    .str3 {stroke:#FFCC00;stroke-width:2.69;stroke-miterlimit:22.9256}
    .str0 {stroke:black;stroke-width:10.78;stroke-miterlimit:22.9256}
    .str5 {stroke:#FFCC00;stroke-width:8.08;stroke-linecap:round;stroke-miterlimit:22.9256}
    .fil1 {fill:none;fill-rule:nonzero}
    .fil2 {fill:none;fill-rule:nonzero}
    .fil4 {fill:#FFF212;fill-rule:nonzero}
    .fil5 {fill:#ED3237;fill-rule:nonzero}
    .fil8 {fill:#8A7322;fill-rule:nonzero}
    .fil6 {fill:#E8D8A0;fill-rule:nonzero}
    .fil7 {fill:#FFCC00;fill-rule:nonzero}
    .fil9 {fill:#0A1118;fill-rule:nonzero;fill-opacity:0.400000}
    .fil3 {fill:url(#id0);fill-rule:nonzero}
    .fil0 {fill:url(#id1);fill-rule:nonzero}
   ]]>
  </style>
  <radialGradient id="id0" gradientUnits="objectBoundingBox" cx="50%" cy="50%" r="50%" fx="50%" fy="50%">
   <stop offset="0" style="stop-opacity:0.54902; stop-color:#FFCC00"/>
   <stop offset="0.6" style="stop-opacity:0.14902; stop-color:#FFCC00"/>
   <stop offset="1" style="stop-opacity:0; stop-color:#FFCC00"/>
  </radialGradient>
  <radialGradient id="id1" gradientUnits="userSpaceOnUse" gradientTransform="matrix(1.1 -0 -0 1.1 -29 -29)" cx="285.56" cy="285.56" r="280.17" fx="285.56" fy="285.56">
   <stop offset="0" style="stop-opacity:1; stop-color:#121A24"/>
   <stop offset="0.701961" style="stop-opacity:1; stop-color:#080C12"/>
   <stop offset="1" style="stop-opacity:1; stop-color:#020305"/>
  </radialGradient>
 </defs>
 <g id="Camada_x0020_1">
  <metadata id="CorelCorpID_0Corel-Layer"/>
  <g id="gauge_temp_bg.svg">
   <path class="fil0 str0" d="M565.73 285.56c0,-154.73 -125.44,-280.17 -280.17,-280.17 -154.73,0 -280.17,125.44 -280.17,280.17 0,154.73 125.44,280.17 280.17,280.17 154.73,0 280.17,-125.44 280.17,-280.17z"/>
   <path class="fil1 str1" d="M75.42 408.28c-21.28,-36.84 -32.51,-78.76 -32.51,-121.33 0,-133.99 108.66,-242.65 242.65,-242.65 133.99,0 242.65,108.66 242.65,242.65 0,0 0,0 0,0"/>
   <path class="fil2 str2" d="M61.3 420.5c-22.71,-39.32 -34.69,-84.05 -34.69,-129.48 0,-142.99 115.96,-258.95 258.95,-258.95 142.99,0 258.95,115.96 258.95,258.95 0,0 0,0 0,0"/>
   <circle class="fil3" cx="285.56" cy="285.56" r="107.76" text-anchor="middle" dominant-baseline="middle"/>
   <g>
    <path class="fil4 str3" d="M285.56 285.56l17.61 -208.77c14.46,2.16 29.59,5.79 43,9.69 24.35,7.09 41.96,17.11 41.96,17.11l-102.57 181.97z"/>
    <path class="fil5 str4" d="M285.56 285.56l102.57 -181.97c96.36,49.09 113.25,178.02 102.89,178.21l-205.46 3.76z"/>
    <g>
     <line class="fil1 str5" x1="135.36" y1="372.43" x2="111.5" y2= "386.21" />
     <path class="fil6" d="M69.27 394.72l0 -3.78 7.44 0 0 3.78 -7.44 0zm8.68 0.03l3.66 -0.44c0.12,0.94 0.43,1.65 0.94,2.15 0.51,0.5 1.13,0.74 1.85,0.74 0.78,0 1.43,-0.3 1.96,-0.89 0.53,-0.59 0.79,-1.39 0.79,-2.4 0,-0.95 -0.25,-1.71 -0.76,-2.26 -0.51,-0.56 -1.13,-0.84 -1.86,-0.84 -0.48,0 -1.06,0.1 -1.73,0.29l0.42 -3.07c1.02,0.03 1.8,-0.2 2.33,-0.67 0.54,-0.47 0.8,-1.1 0.8,-1.88 0,-0.67 -0.2,-1.19 -0.59,-1.59 -0.4,-0.39 -0.92,-0.59 -1.58,-0.59 -0.65,0 -1.2,0.23 -1.65,0.68 -0.46,0.45 -0.73,1.11 -0.84,1.97l-3.47 -0.59c0.24,-1.2 0.61,-2.16 1.1,-2.88 0.49,-0.72 1.17,-1.28 2.04,-1.69 0.87,-0.41 1.86,-0.61 2.94,-0.61 1.86,0 3.34,0.59 4.46,1.78 0.92,0.97 1.39,2.08 1.39,3.3 0,1.74 -0.95,3.13 -2.84,4.16 1.13,0.24 2.03,0.79 2.71,1.63 0.68,0.85 1.02,1.87 1.02,3.06 0,1.73 -0.63,3.21 -1.9,4.43 -1.26,1.22 -2.83,1.83 -4.72,1.83 -1.78,0 -3.26,-0.51 -4.43,-1.55 -1.17,-1.03 -1.86,-2.37 -2.04,-4.04zm21.84 -14.61c1.91,0 3.4,0.68 4.48,2.05 1.28,1.62 1.92,4.3 1.92,8.05 0,3.74 -0.65,6.43 -1.94,8.07 -1.07,1.35 -2.56,2.03 -4.47,2.03 -1.92,0 -3.47,-0.74 -4.64,-2.22 -1.17,-1.48 -1.76,-4.12 -1.76,-7.91 0,-3.72 0.65,-6.41 1.94,-8.04 1.07,-1.35 2.56,-2.03 4.47,-2.03zm0 3.14c-0.45,0 -0.85,0.15 -1.2,0.44 -0.35,0.29 -0.63,0.81 -0.82,1.57 -0.26,0.98 -0.39,2.63 -0.39,4.95 0,2.32 0.12,3.92 0.34,4.78 0.23,0.87 0.52,1.45 0.87,1.74 0.35,0.29 0.75,0.43 1.2,0.43 0.45,0 0.85,-0.15 1.21,-0.44 0.35,-0.29 0.62,-0.81 0.82,-1.57 0.26,-0.97 0.39,-2.62 0.39,-4.94 0,-2.32 -0.12,-3.91 -0.35,-4.78 -0.23,-0.87 -0.52,-1.45 -0.87,-1.74 -0.35,-0.29 -0.75,-0.44 -1.2,-0.44z"/>
     <line class="fil1 str5" x1="111.98" y1="285.17" x2="84.42" y2= "285.17"  text-anchor="middle" dominant-baseline="middle"/>
     <path class="fil6" d="M56.77 265.34c1.91,0 3.4,0.68 4.48,2.05 1.28,1.62 1.92,4.3 1.92,8.05 0,3.74 -0.65,6.43 -1.94,8.07 -1.07,1.35 -2.56,2.03 -4.47,2.03 -1.92,0 -3.47,-0.74 -4.64,-2.22 -1.17,-1.48 -1.76,-4.12 -1.76,-7.91 0,-3.72 0.65,-6.41 1.94,-8.04 1.07,-1.35 2.56,-2.03 4.47,-2.03zm0 3.14c-0.45,0 -0.85,0.15 -1.2,0.44 -0.35,0.29 -0.63,0.81 -0.82,1.57 -0.26,0.98 -0.39,2.63 -0.39,4.95 0,2.32 0.12,3.92 0.34,4.78 0.23,0.87 0.52,1.45 0.87,1.74 0.35,0.29 0.75,0.43 1.2,0.43 0.45,0 0.85,-0.15 1.21,-0.44 0.35,-0.29 0.62,-0.81 0.82,-1.57 0.26,-0.97 0.39,-2.62 0.39,-4.94 0,-2.32 -0.12,-3.91 -0.35,-4.78 -0.23,-0.87 -0.52,-1.45 -0.87,-1.74 -0.35,-0.29 -0.75,-0.44 -1.2,-0.44z"/>
     <line class="fil1 str5" x1="174.32" y1="151.49" x2="156.6" y2= "130.38"  text-anchor="middle" dominant-baseline="middle"/>
     <path class="fil6" d="M124.78 104.21l3.77 -0.39c0.11,0.85 0.43,1.53 0.96,2.03 0.53,0.5 1.14,0.75 1.84,0.75 0.79,0 1.46,-0.32 2.01,-0.97 0.55,-0.65 0.82,-1.62 0.82,-2.92 0,-1.22 -0.27,-2.14 -0.82,-2.75 -0.54,-0.61 -1.25,-0.91 -2.12,-0.91 -1.09,0 -2.07,0.48 -2.93,1.43l-3.06 -0.44 1.93 -10.25 10 0 0 3.53 -7.13 0 -0.6 3.36c0.84,-0.42 1.7,-0.63 2.59,-0.63 1.68,0 3.1,0.61 4.27,1.84 1.17,1.22 1.75,2.81 1.75,4.77 0,1.63 -0.47,3.08 -1.42,4.36 -1.28,1.75 -3.07,2.62 -5.34,2.62 -1.82,0 -3.31,-0.49 -4.46,-1.47 -1.15,-0.98 -1.84,-2.3 -2.06,-3.95zm21.67 -14.77c1.91,0 3.4,0.68 4.48,2.05 1.28,1.62 1.92,4.3 1.92,8.05 0,3.74 -0.65,6.43 -1.94,8.07 -1.07,1.35 -2.56,2.03 -4.47,2.03 -1.92,0 -3.47,-0.74 -4.64,-2.22 -1.17,-1.48 -1.76,-4.12 -1.76,-7.91 0,-3.72 0.65,-6.41 1.94,-8.04 1.07,-1.35 2.56,-2.03 4.47,-2.03zm0 3.14c-0.45,0 -0.85,0.15 -1.2,0.44 -0.35,0.29 -0.63,0.81 -0.82,1.57 -0.26,0.98 -0.39,2.63 -0.39,4.95 0,2.32 0.12,3.92 0.34,4.78 0.23,0.87 0.52,1.45 0.87,1.74 0.35,0.29 0.75,0.43 1.2,0.43 0.45,0 0.85,-0.15 1.21,-0.44 0.35,-0.29 0.62,-0.81 0.82,-1.57 0.26,-0.97 0.39,-2.62 0.39,-4.94 0,-2.32 -0.12,-3.91 -0.35,-4.78 -0.23,-0.87 -0.52,-1.45 -0.87,-1.74 -0.35,-0.29 -0.75,-0.44 -1.2,-0.44z"/>
     <line class="fil1 str5" x1="316.81" y1="113.3" x2="321.58" y2= "86.18"  text-anchor="middle" dominant-baseline="middle"/>
     <path class="fil6" d="M314.2 59.04l-3.78 0 0 -14.28c-1.38,1.3 -3,2.25 -4.88,2.87l0 -3.44c0.99,-0.32 2.06,-0.93 3.21,-1.84 1.16,-0.9 1.95,-1.95 2.38,-3.15l3.06 0 0 19.84zm12.06 -19.84c1.91,0 3.4,0.68 4.48,2.05 1.28,1.62 1.92,4.3 1.92,8.05 0,3.74 -0.65,6.43 -1.94,8.07 -1.07,1.35 -2.56,2.03 -4.47,2.03 -1.92,0 -3.47,-0.74 -4.64,-2.22 -1.17,-1.48 -1.76,-4.12 -1.76,-7.91 0,-3.72 0.65,-6.41 1.94,-8.04 1.07,-1.35 2.56,-2.03 4.47,-2.03zm0 3.14c-0.45,0 -0.85,0.15 -1.2,0.44 -0.35,0.29 -0.63,0.81 -0.82,1.57 -0.26,0.98 -0.39,2.63 -0.39,4.95 0,2.32 0.12,3.92 0.34,4.78 0.23,0.87 0.52,1.45 0.87,1.74 0.35,0.29 0.75,0.43 1.2,0.43 0.45,0 0.85,-0.15 1.21,-0.44 0.35,-0.29 0.62,-0.81 0.82,-1.57 0.26,-0.97 0.39,-2.62 0.39,-4.94 0,-2.32 -0.12,-3.91 -0.35,-4.78 -0.23,-0.87 -0.52,-1.45 -0.87,-1.74 -0.35,-0.29 -0.75,-0.44 -1.2,-0.44zm15.32 -3.14c1.91,0 3.4,0.68 4.48,2.05 1.28,1.62 1.92,4.3 1.92,8.05 0,3.74 -0.65,6.43 -1.94,8.07 -1.07,1.35 -2.56,2.03 -4.47,2.03 -1.92,0 -3.47,-0.74 -4.64,-2.22 -1.17,-1.48 -1.76,-4.12 -1.76,-7.91 0,-3.72 0.65,-6.41 1.94,-8.04 1.07,-1.35 2.56,-2.03 4.47,-2.03zm0 3.14c-0.45,0 -0.85,0.15 -1.2,0.44 -0.35,0.29 -0.63,0.81 -0.82,1.57 -0.26,0.98 -0.39,2.63 -0.39,4.95 0,2.32 0.12,3.92 0.34,4.78 0.23,0.87 0.52,1.45 0.87,1.74 0.35,0.29 0.75,0.43 1.2,0.43 0.45,0 0.85,-0.15 1.21,-0.44 0.35,-0.29 0.62,-0.81 0.82,-1.57 0.26,-0.97 0.39,-2.62 0.39,-4.94 0,-2.32 -0.12,-3.91 -0.35,-4.78 -0.23,-0.87 -0.52,-1.45 -0.87,-1.74 -0.35,-0.29 -0.75,-0.44 -1.2,-0.44z"/>
     <line class="fil1 str5" x1="437.63" y1="197.92" x2="461.49" y2= "184.14"  text-anchor="middle" dominant-baseline="middle"/>
     <path class="fil6" d="M473.19 170.36l-3.78 0 0 -14.28c-1.38,1.3 -3,2.25 -4.88,2.87l0 -3.44c0.99,-0.32 2.06,-0.93 3.21,-1.84 1.16,-0.9 1.95,-1.95 2.38,-3.15l3.06 0 0 19.84zm5.71 -5.07l3.77 -0.39c0.11,0.85 0.43,1.53 0.96,2.03 0.53,0.5 1.14,0.75 1.84,0.75 0.79,0 1.46,-0.32 2.01,-0.97 0.55,-0.65 0.82,-1.62 0.82,-2.92 0,-1.22 -0.27,-2.14 -0.82,-2.75 -0.54,-0.61 -1.25,-0.91 -2.12,-0.91 -1.09,0 -2.07,0.48 -2.93,1.43l-3.06 -0.44 1.93 -10.25 10 0 0 3.53 -7.13 0 -0.6 3.36c0.84,-0.42 1.7,-0.63 2.59,-0.63 1.68,0 3.1,0.61 4.27,1.84 1.17,1.22 1.75,2.81 1.75,4.77 0,1.63 -0.47,3.08 -1.42,4.36 -1.28,1.75 -3.07,2.62 -5.34,2.62 -1.82,0 -3.31,-0.49 -4.46,-1.47 -1.15,-0.98 -1.84,-2.3 -2.06,-3.95zm21.67 -14.77c1.91,0 3.4,0.68 4.48,2.05 1.28,1.62 1.92,4.3 1.92,8.05 0,3.74 -0.65,6.43 -1.94,8.07 -1.07,1.35 -2.56,2.03 -4.47,2.03 -1.92,0 -3.47,-0.74 -4.64,-2.22 -1.17,-1.48 -1.76,-4.12 -1.76,-7.91 0,-3.72 0.65,-6.41 1.94,-8.04 1.07,-1.35 2.56,-2.03 4.47,-2.03zm0 3.14c-0.45,0 -0.85,0.15 -1.2,0.44 -0.35,0.29 -0.63,0.81 -0.82,1.57 -0.26,0.98 -0.39,2.63 -0.39,4.95 0,2.32 0.12,3.92 0.34,4.78 0.23,0.87 0.52,1.45 0.87,1.74 0.35,0.29 0.75,0.43 1.2,0.43 0.45,0 0.85,-0.15 1.21,-0.44 0.35,-0.29 0.62,-0.81 0.82,-1.57 0.26,-0.97 0.39,-2.62 0.39,-4.94 0,-2.32 -0.12,-3.91 -0.35,-4.78 -0.23,-0.87 -0.52,-1.45 -0.87,-1.74 -0.35,-0.29 -0.75,-0.44 -1.2,-0.44z"/>
     <line class="fil1 str5" x1="461.01" y1="285.17" x2="488.56" y2= "285.17"  text-anchor="middle" dominant-baseline="middle"/>
     <path class="fil6" d="M503.96 285.17l-3.78 0 0 -14.28c-1.38,1.3 -3,2.25 -4.88,2.87l0 -3.44c0.99,-0.32 2.06,-0.93 3.21,-1.84 1.16,-0.9 1.95,-1.95 2.38,-3.15l3.06 0 0 19.84zm8.91 -10.67c-0.97,-0.41 -1.69,-0.98 -2.13,-1.7 -0.44,-0.72 -0.66,-1.52 -0.66,-2.37 0,-1.47 0.51,-2.69 1.54,-3.65 1.02,-0.96 2.48,-1.44 4.37,-1.44 1.87,0 3.32,0.48 4.36,1.44 1.03,0.96 1.55,2.18 1.55,3.65 0,0.91 -0.24,1.73 -0.72,2.44 -0.47,0.71 -1.14,1.26 -2.01,1.63 1.1,0.44 1.93,1.08 2.5,1.93 0.57,0.85 0.85,1.82 0.85,2.93 0,1.83 -0.58,3.31 -1.74,4.45 -1.16,1.14 -2.7,1.71 -4.63,1.71 -1.79,0 -3.28,-0.47 -4.47,-1.42 -1.41,-1.11 -2.11,-2.65 -2.11,-4.59 0,-1.07 0.27,-2.05 0.8,-2.95 0.53,-0.89 1.36,-1.58 2.49,-2.07zm0.79 -3.8c0,0.75 0.21,1.35 0.63,1.77 0.43,0.42 0.99,0.63 1.69,0.63 0.72,0 1.29,-0.22 1.71,-0.64 0.43,-0.43 0.64,-1.02 0.64,-1.77 0,-0.71 -0.21,-1.28 -0.63,-1.71 -0.43,-0.43 -0.99,-0.64 -1.68,-0.64 -0.73,0 -1.3,0.22 -1.73,0.65 -0.43,0.43 -0.64,1.01 -0.64,1.72zm-0.36 8.46c0,1.05 0.27,1.87 0.8,2.46 0.53,0.59 1.19,0.88 1.99,0.88 0.78,0 1.42,-0.28 1.93,-0.85 0.51,-0.57 0.76,-1.38 0.76,-2.45 0,-0.93 -0.26,-1.68 -0.78,-2.25 -0.52,-0.57 -1.17,-0.85 -1.97,-0.85 -0.92,0 -1.61,0.32 -2.06,0.96 -0.45,0.64 -0.68,1.34 -0.68,2.09zm18.04 -13.83c1.91,0 3.4,0.68 4.48,2.05 1.28,1.62 1.92,4.3 1.92,8.05 0,3.74 -0.65,6.43 -1.94,8.07 -1.07,1.35 -2.56,2.03 -4.47,2.03 -1.92,0 -3.47,-0.74 -4.64,-2.22 -1.17,-1.48 -1.76,-4.12 -1.76,-7.91 0,-3.72 0.65,-6.41 1.94,-8.04 1.07,-1.35 2.56,-2.03 4.47,-2.03zm0 3.14c-0.45,0 -0.85,0.15 -1.2,0.44 -0.35,0.29 -0.63,0.81 -0.82,1.57 -0.26,0.98 -0.39,2.63 -0.39,4.95 0,2.32 0.12,3.92 0.34,4.78 0.23,0.87 0.52,1.45 0.87,1.74 0.35,0.29 0.75,0.43 1.2,0.43 0.45,0 0.85,-0.15 1.21,-0.44 0.35,-0.29 0.62,-0.81 0.82,-1.57 0.26,-0.97 0.39,-2.62 0.39,-4.94 0,-2.32 -0.12,-3.91 -0.35,-4.78 -0.23,-0.87 -0.52,-1.45 -0.87,-1.74 -0.35,-0.29 -0.75,-0.44 -1.2,-0.44z"/>
    </g>
    <path class="fil7" d="M196.51 453.78l0 -13.48 -4.8 0 0 -2.74 12.87 0 0 2.74 -4.78 0 0 13.48 -3.29 0zm13.14 0l0 -16.22 12.01 0 0 2.74 -8.72 0 0 3.6 8.11 0 0 2.74 -8.11 0 0 4.4 9.04 0 0 2.74 -12.33 0zm18.24 0l0 -16.22 4.92 0 2.91 11.06 2.87 -11.06 4.93 0 0 16.22 -3.04 0 -0.01 -12.77 -3.2 12.77 -3.16 0 -3.18 -12.77 -0.01 12.77 -3.04 0zm21.86 0l0 -16.22 5.25 0c1.99,0 3.29,0.08 3.89,0.24 0.93,0.25 1.71,0.77 2.33,1.59 0.63,0.82 0.94,1.87 0.94,3.16 0,1 -0.18,1.83 -0.54,2.51 -0.36,0.68 -0.82,1.21 -1.37,1.6 -0.55,0.38 -1.12,0.64 -1.7,0.77 -0.78,0.15 -1.91,0.23 -3.39,0.23l-2.13 0 0 6.12 -3.29 0zm3.29 -13.48l0 4.62 1.79 0c1.29,0 2.15,-0.09 2.58,-0.26 0.43,-0.17 0.77,-0.43 1.02,-0.8 0.25,-0.36 0.37,-0.78 0.37,-1.26 0,-0.59 -0.17,-1.08 -0.52,-1.47 -0.35,-0.39 -0.78,-0.63 -1.31,-0.72 -0.39,-0.07 -1.17,-0.11 -2.35,-0.11l-1.57 0zm14.77 13.48l0 -16.22 12.01 0 0 2.74 -8.72 0 0 3.6 8.11 0 0 2.74 -8.11 0 0 4.4 9.04 0 0 2.74 -12.33 0zm18.28 0l0 -16.22 6.9 0c1.73,0 2.99,0.15 3.78,0.44 0.79,0.29 1.42,0.81 1.89,1.55 0.47,0.75 0.71,1.6 0.71,2.56 0,1.22 -0.36,2.22 -1.07,3.02 -0.71,0.79 -1.78,1.29 -3.2,1.5 0.71,0.41 1.29,0.87 1.75,1.36 0.46,0.49 1.08,1.37 1.86,2.64l1.98 3.16 -3.92 0 -2.35 -3.54c-0.84,-1.26 -1.42,-2.05 -1.73,-2.38 -0.31,-0.32 -0.64,-0.55 -0.99,-0.67 -0.35,-0.12 -0.9,-0.18 -1.66,-0.18l-0.66 0 0 6.77 -3.29 0zm3.29 -9.36l2.42 0c1.57,0 2.55,-0.07 2.94,-0.2 0.39,-0.13 0.7,-0.36 0.92,-0.69 0.22,-0.32 0.33,-0.73 0.33,-1.22 0,-0.55 -0.15,-0.98 -0.44,-1.32 -0.29,-0.34 -0.7,-0.55 -1.23,-0.63 -0.27,-0.04 -1.07,-0.06 -2.39,-0.06l-2.56 0 0 4.12zm30.64 9.36l-3.54 0 -1.4 -3.69 -6.51 0 -1.35 3.69 -3.47 0 6.3 -16.22 3.47 0 6.5 16.22zm-6 -6.43l-2.25 -6 -2.19 6 4.44 0zm12.66 6.43l0 -13.48 -4.8 0 0 -2.74 12.87 0 0 2.74 -4.78 0 0 13.48 -3.29 0zm13.36 -16.22l3.29 0 0 8.79c0,1.4 0.04,2.3 0.12,2.71 0.14,0.66 0.47,1.2 0.99,1.6 0.53,0.4 1.24,0.6 2.15,0.6 0.92,0 1.61,-0.19 2.08,-0.57 0.47,-0.38 0.75,-0.85 0.84,-1.4 0.09,-0.55 0.14,-1.47 0.14,-2.76l0 -8.98 3.29 0 0 8.53c0,1.95 -0.09,3.33 -0.26,4.13 -0.18,0.8 -0.5,1.48 -0.98,2.04 -0.47,0.56 -1.11,0.99 -1.9,1.32 -0.8,0.33 -1.83,0.49 -3.11,0.49 -1.54,0 -2.71,-0.18 -3.51,-0.54 -0.8,-0.36 -1.43,-0.82 -1.89,-1.4 -0.46,-0.57 -0.77,-1.17 -0.91,-1.8 -0.22,-0.93 -0.32,-2.3 -0.32,-4.12l0 -8.66zm19.31 16.22l0 -16.22 6.9 0c1.73,0 2.99,0.15 3.78,0.44 0.79,0.29 1.42,0.81 1.89,1.55 0.47,0.75 0.71,1.6 0.71,2.56 0,1.22 -0.36,2.22 -1.07,3.02 -0.71,0.79 -1.78,1.29 -3.2,1.5 0.71,0.41 1.29,0.87 1.75,1.36 0.46,0.49 1.08,1.37 1.86,2.64l1.98 3.16 -3.92 0 -2.35 -3.54c-0.84,-1.26 -1.42,-2.05 -1.73,-2.38 -0.31,-0.32 -0.64,-0.55 -0.99,-0.67 -0.35,-0.12 -0.9,-0.18 -1.66,-0.18l-0.66 0 0 6.77 -3.29 0zm3.29 -9.36l2.42 0c1.57,0 2.55,-0.07 2.94,-0.2 0.39,-0.13 0.7,-0.36 0.92,-0.69 0.22,-0.32 0.33,-0.73 0.33,-1.22 0,-0.55 -0.15,-0.98 -0.44,-1.32 -0.29,-0.34 -0.7,-0.55 -1.23,-0.63 -0.27,-0.04 -1.07,-0.06 -2.39,-0.06l-2.56 0 0 4.12zm30.64 9.36l-3.54 0 -1.4 -3.69 -6.51 0 -1.35 3.69 -3.47 0 6.3 -16.22 3.47 0 6.5 16.22zm-6 -6.43l-2.25 -6 -2.19 6 4.44 0z"/>
    <path class="fil8" d="M209.1 472.19l0 16.88 -0.04 0 -17.35 -8.41 0 -0.09 17.35 -8.38 0.04 0zm13.92 0.19l6.17 0c1.22,0 2.13,0.05 2.73,0.15 0.6,0.1 1.14,0.32 1.61,0.64 0.47,0.32 0.87,0.75 1.19,1.29 0.32,0.54 0.47,1.14 0.47,1.8 0,0.72 -0.19,1.38 -0.58,1.98 -0.39,0.6 -0.92,1.05 -1.58,1.35 0.94,0.28 1.66,0.74 2.16,1.41 0.5,0.66 0.75,1.44 0.75,2.33 0,0.7 -0.16,1.39 -0.49,2.05 -0.33,0.66 -0.77,1.2 -1.34,1.59 -0.56,0.4 -1.26,0.64 -2.09,0.73 -0.52,0.05 -1.77,0.09 -3.76,0.1l-5.25 0 0 -15.43zm3.12 2.56l0 3.56 2.04 0c1.21,0 1.97,-0.02 2.26,-0.05 0.53,-0.06 0.95,-0.24 1.26,-0.55 0.31,-0.31 0.46,-0.7 0.46,-1.2 0,-0.48 -0.13,-0.86 -0.4,-1.16 -0.26,-0.3 -0.65,-0.48 -1.17,-0.54 -0.31,-0.04 -1.2,-0.05 -2.66,-0.05l-1.79 0zm0 6.12l0 4.14 2.89 0c1.12,0 1.84,-0.03 2.14,-0.09 0.46,-0.09 0.84,-0.29 1.13,-0.62 0.29,-0.33 0.44,-0.76 0.44,-1.31 0,-0.47 -0.11,-0.86 -0.34,-1.18 -0.22,-0.32 -0.55,-0.56 -0.97,-0.71 -0.42,-0.15 -1.35,-0.22 -2.77,-0.22l-2.52 0zm28.93 6.75l-3.37 0 -1.33 -3.51 -6.19 0 -1.28 3.51 -3.31 0 5.99 -15.43 3.3 0 6.18 15.43zm-5.71 -6.12l-2.14 -5.71 -2.08 5.71 4.23 0zm12.05 6.12l0 -12.82 -4.57 0 0 -2.61 12.24 0 0 2.61 -4.55 0 0 12.82 -3.12 0zm10.73 -4.12l0 -2.95 5.82 0 0 2.95 -5.82 0zm13.81 4.12l0 -12.82 -4.57 0 0 -2.61 12.24 0 0 2.61 -4.55 0 0 12.82 -3.12 0zm12.28 0l0 -15.43 11.42 0 0 2.61 -8.3 0 0 3.43 7.72 0 0 2.61 -7.72 0 0 4.18 8.6 0 0 2.61 -11.72 0zm27.05 -5.69l3.02 0.93c-0.46,1.7 -1.24,2.96 -2.31,3.79 -1.08,0.82 -2.44,1.24 -4.1,1.24 -2.05,0 -3.74,-0.7 -5.06,-2.1 -1.32,-1.4 -1.98,-3.32 -1.98,-5.75 0,-2.57 0.66,-4.57 1.99,-5.99 1.33,-1.42 3.07,-2.13 5.23,-2.13 1.89,0 3.42,0.56 4.6,1.68 0.7,0.66 1.23,1.61 1.58,2.85l-3.08 0.73c-0.18,-0.8 -0.56,-1.43 -1.14,-1.89 -0.58,-0.46 -1.29,-0.69 -2.11,-0.69 -1.15,0 -2.08,0.41 -2.79,1.23 -0.71,0.82 -1.07,2.15 -1.07,3.99 0,1.95 0.35,3.34 1.05,4.16 0.7,0.83 1.62,1.24 2.74,1.24 0.83,0 1.54,-0.26 2.14,-0.79 0.6,-0.53 1.03,-1.35 1.29,-2.49zm8.28 5.69l0 -15.43 3.12 0 0 6.08 6.08 0 0 -6.08 3.13 0 0 15.43 -3.13 0 0 -6.75 -6.08 0 0 6.75 -3.12 0z"/>
    <path class="fil8" d="M183.58 522.8l0 -15.43 11.42 0 0 2.61 -8.3 0 0 3.43 7.72 0 0 2.61 -7.72 0 0 4.18 8.6 0 0 2.61 -11.72 0zm16.96 0l0 -15.43 3.03 0 6.35 10.34 0 -10.34 2.89 0 0 15.43 -3.12 0 -6.25 -10.14 0 10.14 -2.89 0zm25.54 -5.67l0 -2.61 6.7 0 0 6.16c-0.65,0.64 -1.6,1.19 -2.83,1.68 -1.24,0.48 -2.49,0.72 -3.76,0.72 -1.61,0 -3.02,-0.34 -4.22,-1.02 -1.2,-0.68 -2.1,-1.65 -2.7,-2.91 -0.6,-1.26 -0.91,-2.64 -0.91,-4.12 0,-1.61 0.34,-3.04 1.01,-4.29 0.68,-1.25 1.66,-2.21 2.96,-2.88 0.99,-0.51 2.22,-0.77 3.69,-0.77 1.92,0 3.41,0.4 4.49,1.21 1.07,0.81 1.77,1.92 2.08,3.34l-3.1 0.58c-0.21,-0.76 -0.63,-1.36 -1.22,-1.8 -0.6,-0.44 -1.34,-0.66 -2.24,-0.66 -1.36,0 -2.44,0.43 -3.24,1.3 -0.8,0.87 -1.2,2.15 -1.2,3.85 0,1.84 0.41,3.21 1.22,4.13 0.82,0.92 1.88,1.38 3.2,1.38 0.65,0 1.3,-0.13 1.96,-0.38 0.66,-0.25 1.22,-0.57 1.69,-0.93l0 -1.96 -3.56 0zm12.06 5.67l0 -15.43 3.13 0 0 15.43 -3.13 0zm8.9 0l0 -15.43 3.03 0 6.35 10.34 0 -10.34 2.89 0 0 15.43 -3.12 0 -6.25 -10.14 0 10.14 -2.89 0zm18.15 0l0 -15.43 11.42 0 0 2.61 -8.3 0 0 3.43 7.72 0 0 2.61 -7.72 0 0 4.18 8.6 0 0 2.61 -11.72 0zm29.22 0l0 -12.82 -4.57 0 0 -2.61 12.24 0 0 2.61 -4.55 0 0 12.82 -3.12 0zm12.5 0l0 -15.43 11.42 0 0 2.61 -8.3 0 0 3.43 7.72 0 0 2.61 -7.72 0 0 4.18 8.6 0 0 2.61 -11.72 0zm16.92 0l0 -15.43 4.68 0 2.77 10.52 2.73 -10.52 4.69 0 0 15.43 -2.89 0 -0.01 -12.15 -3.04 12.15 -3 0 -3.03 -12.15 -0.01 12.15 -2.89 0zm20.8 0l0 -15.43 5 0c1.89,0 3.13,0.08 3.7,0.23 0.88,0.23 1.62,0.74 2.22,1.51 0.6,0.78 0.89,1.78 0.89,3.01 0,0.95 -0.17,1.74 -0.51,2.39 -0.34,0.65 -0.78,1.15 -1.31,1.52 -0.53,0.37 -1.07,0.61 -1.61,0.73 -0.74,0.15 -1.82,0.22 -3.23,0.22l-2.03 0 0 5.82 -3.12 0zm3.12 -12.82l0 4.4 1.7 0c1.22,0 2.04,-0.08 2.45,-0.24 0.41,-0.16 0.74,-0.41 0.97,-0.76 0.23,-0.35 0.35,-0.74 0.35,-1.2 0,-0.56 -0.16,-1.03 -0.49,-1.39 -0.33,-0.37 -0.74,-0.6 -1.25,-0.69 -0.37,-0.07 -1.11,-0.1 -2.23,-0.1l-1.5 0zm21.64 14.07l0 -16.88 0.04 0 17.35 8.38 0 0.09 -17.35 8.41 -0.04 0z"/>
   </g>
   <path class="fil9 str6" d="M285.56 330.48c-14.19,-14.19 -37.83,-9.46 -75.66,-23.64 14.19,-14.19 9.46,-37.83 -18.91,-56.74 28.37,0 47.29,14.19 66.2,-4.73 9.46,14.19 18.91,9.46 28.37,-4.73 9.46,14.19 18.91,18.91 28.37,4.73 18.91,18.91 37.83,4.73 66.2,4.73 -28.37,18.91 -33.1,42.56 -18.91,56.74 -37.83,14.19 -61.47,9.46 -75.66,23.64z"/>
  </g>
 </g>
</svg>
)rawliteral";

const char GAUGE_PRESS_BG_SVG[] PROGMEM = R"rawliteral(<svg xmlns="http://www.w3.org/2000/svg" xml:space="preserve" width="56.0916mm" height="56.0916mm" version="1.1" style="shape-rendering:geometricPrecision; text-rendering:geometricPrecision; image-rendering:optimizeQuality; fill-rule:evenodd; clip-rule:evenodd"
viewBox="0 0 432.55 432.55"
 xmlns:xlink="http://www.w3.org/1999/xlink"
 xmlns:xodm="http://www.corel.com/coreldraw/odm/2003">
 <defs>
  <style type="text/css">
   <![CDATA[
    .str2 {stroke:#FFCC00;stroke-width:3.06;stroke-miterlimit:22.9256;stroke-opacity:0.400000}
    .str6 {stroke:#FFCC00;stroke-width:3.86;stroke-miterlimit:22.9256;stroke-opacity:0.400000}
    .str1 {stroke:#3A2E10;stroke-width:12.24;stroke-miterlimit:22.9256}
    .str4 {stroke:#FF3030;stroke-width:2.04;stroke-miterlimit:22.9256}
    .str3 {stroke:#FFCC00;stroke-width:2.04;stroke-miterlimit:22.9256}
    .str0 {stroke:black;stroke-width:8.16;stroke-miterlimit:22.9256}
    .str5 {stroke:#FFCC00;stroke-width:6.12;stroke-linecap:round;stroke-miterlimit:22.9256}
    .fil1 {fill:none;fill-rule:nonzero}
    .fil2 {fill:none;fill-rule:nonzero}
    .fil3 {fill:#FFF212;fill-rule:nonzero}
    .fil4 {fill:#ED3237;fill-rule:nonzero}
    .fil8 {fill:#8A7322;fill-rule:nonzero}
    .fil5 {fill:#E8D8A0;fill-rule:nonzero}
    .fil7 {fill:#FFCC00;fill-rule:nonzero}
    .fil9 {fill:#0A1118;fill-rule:nonzero;fill-opacity:0.400000}
    .fil0 {fill:url(#id0);fill-rule:nonzero}
    .fil6 {fill:url(#id1);fill-rule:nonzero}
   ]]>
  </style>
  <radialGradient id="id0" gradientUnits="userSpaceOnUse" gradientTransform="matrix(1.1 -0 -0 1.1 -22 -22)" cx="216.27" cy="216.27" r="212.19" fx="216.27" fy="216.27">
   <stop offset="0" style="stop-opacity:1; stop-color:#121A24"/>
   <stop offset="0.701961" style="stop-opacity:1; stop-color:#080C12"/>
   <stop offset="1" style="stop-opacity:1; stop-color:#020305"/>
  </radialGradient>
  <radialGradient id="id1" gradientUnits="userSpaceOnUse" gradientTransform="matrix(0.999995 2.96705E-06 -2.96703E-06 0.99999 0 0)" cx="216.27" cy="216.27" r="74.43" fx="216.27" fy="216.27">
   <stop offset="0" style="stop-opacity:0.54902; stop-color:#FFCC00"/>
   <stop offset="0.6" style="stop-opacity:0.14902; stop-color:#FFCC00"/>
   <stop offset="1" style="stop-opacity:0; stop-color:#FFCC00"/>
  </radialGradient>
 </defs>
 <g id="Camada_x0020_1">
  <metadata id="CorelCorpID_0Corel-Layer"/>
  <path class="fil0 str0" d="M428.47 216.27c0,-117.19 -95,-212.19 -212.19,-212.19 -117.19,0 -212.19,95 -212.19,212.19 0,117.19 95,212.19 212.19,212.19 117.19,0 212.19,-95 212.19,-212.19z"/>
  <path class="fil1 str1" d="M57.12 302.72c-16.12,-27.9 -24.62,-59.65 -24.62,-91.89 0,-101.48 82.3,-183.78 183.78,-183.78 101.48,0 183.78,82.3 183.78,183.78 0,0 0,0 0,0"/>
  <path class="fil2 str2" d="M46.43 311.97c-17.2,-29.78 -26.27,-63.66 -26.27,-98.06 0,-108.29 87.82,-196.12 196.12,-196.12 108.3,0 196.12,87.82 196.12,196.12 0,0 0,0 0,0"/>
  <g id="gauge_press_bg.svg">
   <path class="fil3 str3" d="M216.27 216.27l0 -163.74c17.17,0 34.28,2.72 50.59,8.02l-50.59 155.72z"/>
   <path class="fil4 str4" d="M216.27 216.27l50.59 -155.72c67.45,21.91 113.15,84.8 113.15,155.73l-163.74 -0.01z"/>
   <g>
    <line class="fil1 str5" x1="116.28" y1="316.27" x2="100.48" y2= "332.06" />
    <path class="fil5" d="M66.72 343.58l0 -3.06 6.03 0 0 3.06 -6.03 0zm12.31 -11.81c1.55,0 2.76,0.55 3.63,1.66 1.04,1.31 1.56,3.49 1.56,6.52 0,3.03 -0.53,5.21 -1.57,6.54 -0.87,1.09 -2.07,1.64 -3.62,1.64 -1.56,0 -2.81,-0.6 -3.76,-1.8 -0.95,-1.2 -1.43,-3.34 -1.43,-6.41 0,-3.02 0.52,-5.19 1.57,-6.52 0.87,-1.09 2.07,-1.64 3.62,-1.64zm0 2.55c-0.36,0 -0.69,0.12 -0.97,0.36 -0.29,0.23 -0.51,0.66 -0.67,1.27 -0.21,0.79 -0.31,2.13 -0.31,4.01 0,1.88 0.09,3.17 0.28,3.88 0.19,0.7 0.42,1.17 0.7,1.41 0.28,0.23 0.61,0.35 0.97,0.35 0.36,0 0.69,-0.12 0.98,-0.36 0.28,-0.23 0.51,-0.66 0.66,-1.27 0.21,-0.79 0.31,-2.12 0.31,-4 0,-1.88 -0.09,-3.17 -0.28,-3.87 -0.19,-0.7 -0.42,-1.18 -0.7,-1.41 -0.28,-0.24 -0.61,-0.36 -0.97,-0.36zm7.87 13.53l0 -3.06 3.06 0 0 3.06 -3.06 0zm5.6 -4.1l3.06 -0.32c0.09,0.69 0.35,1.24 0.77,1.65 0.43,0.41 0.93,0.61 1.49,0.61 0.64,0 1.18,-0.26 1.63,-0.79 0.44,-0.52 0.67,-1.31 0.67,-2.37 0,-0.99 -0.22,-1.73 -0.66,-2.23 -0.44,-0.49 -1.01,-0.74 -1.72,-0.74 -0.88,0 -1.67,0.39 -2.37,1.16l-2.48 -0.36 1.56 -8.31 8.1 0 0 2.86 -5.78 0 -0.48 2.72c0.68,-0.34 1.38,-0.51 2.1,-0.51 1.36,0 2.52,0.5 3.46,1.49 0.95,0.99 1.42,2.28 1.42,3.86 0,1.32 -0.38,2.5 -1.15,3.54 -1.04,1.41 -2.49,2.12 -4.33,2.12 -1.48,0 -2.68,-0.4 -3.61,-1.19 -0.93,-0.8 -1.49,-1.86 -1.67,-3.2z"/>
    <line class="fil1 str5" x1="74.86" y1="216.27" x2="52.53" y2= "216.27"  text-anchor="middle" dominant-baseline="middle"/>
    <path class="fil5" d="M30.13 200.2c1.55,0 2.76,0.55 3.63,1.66 1.04,1.31 1.56,3.49 1.56,6.52 0,3.03 -0.53,5.21 -1.57,6.54 -0.87,1.09 -2.07,1.64 -3.62,1.64 -1.56,0 -2.81,-0.6 -3.76,-1.8 -0.95,-1.2 -1.43,-3.34 -1.43,-6.41 0,-3.02 0.52,-5.19 1.57,-6.52 0.87,-1.09 2.07,-1.64 3.62,-1.64zm0 2.55c-0.36,0 -0.69,0.12 -0.97,0.36 -0.29,0.23 -0.51,0.66 -0.67,1.27 -0.21,0.79 -0.31,2.13 -0.31,4.01 0,1.88 0.09,3.17 0.28,3.88 0.19,0.7 0.42,1.17 0.7,1.41 0.28,0.23 0.61,0.35 0.97,0.35 0.36,0 0.69,-0.12 0.98,-0.36 0.28,-0.23 0.51,-0.66 0.66,-1.27 0.21,-0.79 0.31,-2.12 0.31,-4 0,-1.88 -0.09,-3.17 -0.28,-3.87 -0.19,-0.7 -0.42,-1.18 -0.7,-1.41 -0.28,-0.24 -0.61,-0.36 -0.97,-0.36z"/>
    <line class="fil1 str5" x1="116.28" y1="116.28" x2="100.48" y2= "100.48"  text-anchor="middle" dominant-baseline="middle"/>
    <path class="fil5" d="M75.32 68.63c1.55,0 2.76,0.55 3.63,1.66 1.04,1.31 1.56,3.49 1.56,6.52 0,3.03 -0.53,5.21 -1.57,6.54 -0.87,1.09 -2.07,1.64 -3.62,1.64 -1.56,0 -2.81,-0.6 -3.76,-1.8 -0.95,-1.2 -1.43,-3.34 -1.43,-6.41 0,-3.02 0.52,-5.19 1.57,-6.52 0.87,-1.09 2.07,-1.64 3.62,-1.64zm0 2.55c-0.36,0 -0.69,0.12 -0.97,0.36 -0.29,0.23 -0.51,0.66 -0.67,1.27 -0.21,0.79 -0.31,2.13 -0.31,4.01 0,1.88 0.09,3.17 0.28,3.88 0.19,0.7 0.42,1.17 0.7,1.41 0.28,0.23 0.61,0.35 0.97,0.35 0.36,0 0.69,-0.12 0.98,-0.36 0.28,-0.23 0.51,-0.66 0.66,-1.27 0.21,-0.79 0.31,-2.12 0.31,-4 0,-1.88 -0.09,-3.17 -0.28,-3.87 -0.19,-0.7 -0.42,-1.18 -0.7,-1.41 -0.28,-0.24 -0.61,-0.36 -0.97,-0.36zm7.87 13.53l0 -3.06 3.06 0 0 3.06 -3.06 0zm5.6 -4.1l3.06 -0.32c0.09,0.69 0.35,1.24 0.77,1.65 0.43,0.41 0.93,0.61 1.49,0.61 0.64,0 1.18,-0.26 1.63,-0.79 0.44,-0.52 0.67,-1.31 0.67,-2.37 0,-0.99 -0.22,-1.73 -0.66,-2.23 -0.44,-0.49 -1.01,-0.74 -1.72,-0.74 -0.88,0 -1.67,0.39 -2.37,1.16l-2.48 -0.36 1.56 -8.31 8.1 0 0 2.86 -5.78 0 -0.48 2.72c0.68,-0.34 1.38,-0.51 2.1,-0.51 1.36,0 2.52,0.5 3.46,1.49 0.95,0.99 1.42,2.28 1.42,3.86 0,1.32 -0.38,2.5 -1.15,3.54 -1.04,1.41 -2.49,2.12 -4.33,2.12 -1.48,0 -2.68,-0.4 -3.61,-1.19 -0.93,-0.8 -1.49,-1.86 -1.67,-3.2z"/>
    <line class="fil1 str5" x1="216.27" y1="74.86" x2="216.27" y2= "52.54"  text-anchor="middle" dominant-baseline="middle"/>
    <path class="fil5" d="M218.84 30.21l-3.06 0 0 -11.57c-1.12,1.05 -2.43,1.83 -3.95,2.33l0 -2.79c0.8,-0.26 1.67,-0.76 2.6,-1.49 0.94,-0.73 1.58,-1.58 1.93,-2.55l2.48 0 0 16.08z"/>
    <line class="fil1 str5" x1="316.27" y1="116.28" x2="332.06" y2= "100.48"  text-anchor="middle" dominant-baseline="middle"/>
    <path class="fil5" d="M341.1 84.71l-3.06 0 0 -11.57c-1.12,1.05 -2.43,1.83 -3.95,2.33l0 -2.79c0.8,-0.26 1.67,-0.76 2.6,-1.49 0.94,-0.73 1.58,-1.58 1.93,-2.55l2.48 0 0 16.08zm5.23 0l0 -3.06 3.06 0 0 3.06 -3.06 0zm5.6 -4.1l3.06 -0.32c0.09,0.69 0.35,1.24 0.77,1.65 0.43,0.41 0.93,0.61 1.49,0.61 0.64,0 1.18,-0.26 1.63,-0.79 0.44,-0.52 0.67,-1.31 0.67,-2.37 0,-0.99 -0.22,-1.73 -0.66,-2.23 -0.44,-0.49 -1.01,-0.74 -1.72,-0.74 -0.88,0 -1.67,0.39 -2.37,1.16l-2.48 -0.36 1.56 -8.31 8.1 0 0 2.86 -5.78 0 -0.48 2.72c0.68,-0.34 1.38,-0.51 2.1,-0.51 1.36,0 2.52,0.5 3.46,1.49 0.95,0.99 1.42,2.28 1.42,3.86 0,1.32 -0.38,2.5 -1.15,3.54 -1.04,1.41 -2.49,2.12 -4.33,2.12 -1.48,0 -2.68,-0.4 -3.61,-1.19 -0.93,-0.8 -1.49,-1.86 -1.67,-3.2z"/>
    <line class="fil1 str5" x1="357.68" y1="216.27" x2="380.01" y2= "216.27"  text-anchor="middle" dominant-baseline="middle"/>
    <path class="fil5" d="M407.43 213.44l0 2.84 -10.74 0c0.11,-1.08 0.47,-2.1 1.05,-3.06 0.58,-0.96 1.73,-2.24 3.44,-3.84 1.38,-1.29 2.22,-2.16 2.54,-2.62 0.42,-0.63 0.63,-1.26 0.63,-1.88 0,-0.68 -0.18,-1.21 -0.55,-1.58 -0.36,-0.37 -0.87,-0.55 -1.51,-0.55 -0.64,0 -1.15,0.19 -1.53,0.58 -0.38,0.38 -0.59,1.02 -0.65,1.92l-3.06 -0.31c0.18,-1.69 0.75,-2.9 1.71,-3.64 0.96,-0.73 2.16,-1.1 3.6,-1.1 1.58,0 2.82,0.43 3.72,1.28 0.9,0.85 1.35,1.91 1.35,3.18 0,0.72 -0.13,1.41 -0.39,2.06 -0.26,0.65 -0.67,1.33 -1.23,2.05 -0.37,0.48 -1.04,1.16 -2.01,2.05 -0.97,0.89 -1.59,1.48 -1.84,1.77 -0.26,0.29 -0.47,0.58 -0.63,0.85l6.1 0z"/>
   </g>
   <path class="fil6" d="M290.7 216.27c0,-41.1 -33.32,-74.43 -74.43,-74.43 -41.1,0 -74.43,33.32 -74.43,74.43 -0,41.1 33.32,74.43 74.43,74.43 41.11,0 74.43,-33.32 74.43,-74.43z" text-anchor="middle" dominant-baseline="middle"/>
   <path class="fil7" d="M143.24 340.93l0 -10.21 -3.64 0 0 -2.08 9.75 0 0 2.08 -3.62 0 0 10.21 -2.49 0zm9.95 -12.29l2.49 0 0 6.66c0,1.06 0.03,1.74 0.09,2.05 0.11,0.5 0.35,0.91 0.75,1.21 0.4,0.31 0.94,0.46 1.62,0.46 0.69,0 1.22,-0.14 1.58,-0.43 0.35,-0.29 0.57,-0.64 0.64,-1.06 0.07,-0.42 0.11,-1.12 0.11,-2.09l0 -6.8 2.49 0 0 6.46c0,1.48 -0.07,2.52 -0.2,3.13 -0.13,0.61 -0.38,1.12 -0.74,1.54 -0.36,0.42 -0.84,0.75 -1.44,1 -0.6,0.25 -1.39,0.37 -2.36,0.37 -1.17,0 -2.06,-0.13 -2.66,-0.41 -0.6,-0.27 -1.08,-0.62 -1.43,-1.06 -0.35,-0.43 -0.58,-0.89 -0.69,-1.36 -0.16,-0.7 -0.24,-1.74 -0.24,-3.12l0 -6.56zm14.79 12.29l0 -12.29 5.23 0c1.31,0 2.27,0.11 2.86,0.33 0.6,0.22 1.07,0.61 1.43,1.18 0.36,0.57 0.54,1.21 0.54,1.94 0,0.92 -0.27,1.68 -0.81,2.29 -0.54,0.6 -1.35,0.98 -2.42,1.14 0.53,0.31 0.97,0.65 1.32,1.03 0.35,0.37 0.82,1.04 1.41,2l1.5 2.4 -2.97 0 -1.78 -2.68c-0.64,-0.95 -1.07,-1.55 -1.31,-1.8 -0.23,-0.25 -0.48,-0.42 -0.75,-0.51 -0.27,-0.09 -0.68,-0.14 -1.26,-0.14l-0.5 0 0 5.13 -2.49 0zm2.49 -7.09l1.84 0c1.19,0 1.93,-0.05 2.23,-0.15 0.29,-0.1 0.53,-0.27 0.7,-0.52 0.17,-0.25 0.25,-0.56 0.25,-0.92 0,-0.41 -0.11,-0.75 -0.33,-1 -0.22,-0.25 -0.53,-0.41 -0.93,-0.48 -0.2,-0.03 -0.81,-0.04 -1.81,-0.04l-1.94 0 0 3.12zm12.13 -5.2l4.91 0c0.98,0 1.7,0.04 2.18,0.12 0.48,0.08 0.91,0.25 1.28,0.51 0.38,0.26 0.69,0.6 0.94,1.02 0.25,0.43 0.38,0.91 0.38,1.44 0,0.57 -0.15,1.1 -0.46,1.58 -0.31,0.48 -0.73,0.84 -1.26,1.08 0.75,0.22 1.32,0.59 1.72,1.12 0.4,0.53 0.6,1.14 0.6,1.86 0,0.56 -0.13,1.1 -0.39,1.63 -0.26,0.53 -0.62,0.95 -1.07,1.27 -0.45,0.31 -1,0.51 -1.66,0.58 -0.41,0.04 -1.41,0.07 -2.99,0.08l-4.18 0 0 -12.29zm2.49 2.04l0 2.83 1.62 0c0.96,0 1.56,-0.01 1.8,-0.04 0.42,-0.05 0.76,-0.19 1,-0.44 0.24,-0.24 0.36,-0.56 0.36,-0.96 0,-0.38 -0.11,-0.69 -0.31,-0.92 -0.21,-0.24 -0.52,-0.38 -0.93,-0.43 -0.25,-0.03 -0.95,-0.04 -2.12,-0.04l-1.42 0zm0 4.87l0 3.29 2.3 0c0.89,0 1.46,-0.03 1.7,-0.07 0.37,-0.07 0.67,-0.23 0.9,-0.49 0.23,-0.26 0.35,-0.61 0.35,-1.05 0,-0.37 -0.09,-0.68 -0.27,-0.94 -0.18,-0.26 -0.44,-0.45 -0.78,-0.56 -0.34,-0.12 -1.07,-0.18 -2.2,-0.18l-2 0zm11.62 -0.7c0,-1.25 0.19,-2.3 0.56,-3.16 0.28,-0.63 0.66,-1.19 1.14,-1.69 0.48,-0.5 1.01,-0.87 1.59,-1.11 0.77,-0.32 1.65,-0.49 2.65,-0.49 1.81,0 3.26,0.56 4.35,1.69 1.09,1.13 1.63,2.69 1.63,4.7 0,1.99 -0.54,3.54 -1.62,4.66 -1.08,1.12 -2.52,1.68 -4.33,1.68 -1.83,0 -3.28,-0.56 -4.36,-1.68 -1.08,-1.12 -1.62,-2.65 -1.62,-4.62zm2.56 -0.08c0,1.39 0.32,2.45 0.97,3.17 0.64,0.72 1.46,1.08 2.45,1.08 0.99,0 1.8,-0.36 2.44,-1.07 0.63,-0.71 0.95,-1.78 0.95,-3.21 0,-1.41 -0.31,-2.46 -0.93,-3.15 -0.62,-0.69 -1.44,-1.04 -2.47,-1.04 -1.02,0 -1.85,0.35 -2.48,1.05 -0.63,0.7 -0.94,1.76 -0.94,3.18zm20.71 -6.13l4.91 0c0.98,0 1.7,0.04 2.18,0.12 0.48,0.08 0.91,0.25 1.28,0.51 0.38,0.26 0.69,0.6 0.94,1.02 0.25,0.43 0.38,0.91 0.38,1.44 0,0.57 -0.15,1.1 -0.46,1.58 -0.31,0.48 -0.73,0.84 -1.26,1.08 0.75,0.22 1.32,0.59 1.72,1.12 0.4,0.53 0.6,1.14 0.6,1.86 0,0.56 -0.13,1.1 -0.39,1.63 -0.26,0.53 -0.62,0.95 -1.07,1.27 -0.45,0.31 -1,0.51 -1.66,0.58 -0.41,0.04 -1.41,0.07 -2.99,0.08l-4.18 0 0 -12.29zm2.49 2.04l0 2.83 1.62 0c0.96,0 1.56,-0.01 1.8,-0.04 0.42,-0.05 0.76,-0.19 1,-0.44 0.24,-0.24 0.36,-0.56 0.36,-0.96 0,-0.38 -0.11,-0.69 -0.31,-0.92 -0.21,-0.24 -0.52,-0.38 -0.93,-0.43 -0.25,-0.03 -0.95,-0.04 -2.12,-0.04l-1.42 0zm0 4.87l0 3.29 2.3 0c0.89,0 1.46,-0.03 1.7,-0.07 0.37,-0.07 0.67,-0.23 0.9,-0.49 0.23,-0.26 0.35,-0.61 0.35,-1.05 0,-0.37 -0.09,-0.68 -0.27,-0.94 -0.18,-0.26 -0.44,-0.45 -0.78,-0.56 -0.34,-0.12 -1.07,-0.18 -2.2,-0.18l-2 0zm11.62 -0.7c0,-1.25 0.19,-2.3 0.56,-3.16 0.28,-0.63 0.66,-1.19 1.14,-1.69 0.48,-0.5 1.01,-0.87 1.59,-1.11 0.77,-0.32 1.65,-0.49 2.65,-0.49 1.81,0 3.26,0.56 4.35,1.69 1.09,1.13 1.63,2.69 1.63,4.7 0,1.99 -0.54,3.54 -1.62,4.66 -1.08,1.12 -2.52,1.68 -4.33,1.68 -1.83,0 -3.28,-0.56 -4.36,-1.68 -1.08,-1.12 -1.62,-2.65 -1.62,-4.62zm2.56 -0.08c0,1.39 0.32,2.45 0.97,3.17 0.64,0.72 1.46,1.08 2.45,1.08 0.99,0 1.8,-0.36 2.44,-1.07 0.63,-0.71 0.95,-1.78 0.95,-3.21 0,-1.41 -0.31,-2.46 -0.93,-3.15 -0.62,-0.69 -1.44,-1.04 -2.47,-1.04 -1.02,0 -1.85,0.35 -2.48,1.05 -0.63,0.7 -0.94,1.76 -0.94,3.18zm13.19 0.08c0,-1.25 0.19,-2.3 0.56,-3.16 0.28,-0.63 0.66,-1.19 1.14,-1.69 0.48,-0.5 1.01,-0.87 1.59,-1.11 0.77,-0.32 1.65,-0.49 2.65,-0.49 1.81,0 3.26,0.56 4.35,1.69 1.09,1.13 1.63,2.69 1.63,4.7 0,1.99 -0.54,3.54 -1.62,4.66 -1.08,1.12 -2.52,1.68 -4.33,1.68 -1.83,0 -3.28,-0.56 -4.36,-1.68 -1.08,-1.12 -1.62,-2.65 -1.62,-4.62zm2.56 -0.08c0,1.39 0.32,2.45 0.97,3.17 0.64,0.72 1.46,1.08 2.45,1.08 0.99,0 1.8,-0.36 2.44,-1.07 0.63,-0.71 0.95,-1.78 0.95,-3.21 0,-1.41 -0.31,-2.46 -0.93,-3.15 -0.62,-0.69 -1.44,-1.04 -2.47,-1.04 -1.02,0 -1.85,0.35 -2.48,1.05 -0.63,0.7 -0.94,1.76 -0.94,3.18zm12.88 2.17l2.42 -0.24c0.15,0.81 0.44,1.4 0.88,1.78 0.44,0.38 1.04,0.57 1.79,0.57 0.8,0 1.4,-0.17 1.8,-0.51 0.4,-0.34 0.61,-0.73 0.61,-1.18 0,-0.29 -0.09,-0.54 -0.25,-0.74 -0.17,-0.2 -0.47,-0.38 -0.89,-0.53 -0.29,-0.1 -0.95,-0.28 -1.98,-0.54 -1.32,-0.33 -2.25,-0.74 -2.79,-1.22 -0.75,-0.68 -1.13,-1.5 -1.13,-2.48 0,-0.63 0.18,-1.22 0.53,-1.76 0.35,-0.55 0.86,-0.96 1.53,-1.25 0.67,-0.29 1.47,-0.43 2.41,-0.43 1.54,0 2.69,0.34 3.47,1.02 0.78,0.68 1.18,1.59 1.22,2.73l-2.49 0.09c-0.11,-0.63 -0.33,-1.09 -0.68,-1.37 -0.35,-0.28 -0.87,-0.42 -1.56,-0.42 -0.71,0 -1.28,0.15 -1.68,0.45 -0.26,0.19 -0.39,0.45 -0.39,0.77 0,0.29 0.12,0.54 0.37,0.75 0.31,0.27 1.07,0.54 2.27,0.83 1.2,0.28 2.09,0.58 2.67,0.88 0.58,0.31 1.03,0.72 1.36,1.25 0.33,0.53 0.49,1.18 0.49,1.96 0,0.7 -0.19,1.36 -0.59,1.98 -0.39,0.62 -0.94,1.07 -1.66,1.37 -0.72,0.3 -1.61,0.45 -2.67,0.45 -1.55,0 -2.75,-0.36 -3.58,-1.07 -0.83,-0.71 -1.33,-1.76 -1.49,-3.13zm17.09 3.98l0 -10.21 -3.64 0 0 -2.08 9.75 0 0 2.08 -3.62 0 0 10.21 -2.49 0z"/>
   <path class="fil8" d="M168.67 354.07l0 12.75 -0.03 0 -13.59 -6.35 0 -0.07 13.59 -6.33 0.03 0zm11.51 11.8l0 -11.64 4.13 0c0.63,0 1.21,0.04 1.75,0.13 0.54,0.09 1.01,0.25 1.41,0.47 0.4,0.23 0.72,0.52 0.94,0.89 0.23,0.37 0.34,0.83 0.34,1.39 0,0.26 -0.04,0.52 -0.11,0.79 -0.07,0.26 -0.18,0.51 -0.31,0.73 -0.14,0.23 -0.3,0.43 -0.49,0.61 -0.19,0.18 -0.41,0.31 -0.66,0.39l0 0.07c0.31,0.07 0.59,0.18 0.85,0.33 0.26,0.15 0.49,0.34 0.69,0.56 0.2,0.23 0.35,0.49 0.46,0.79 0.11,0.3 0.17,0.65 0.17,1.04 0,0.6 -0.12,1.11 -0.36,1.54 -0.24,0.43 -0.57,0.78 -1,1.06 -0.43,0.28 -0.92,0.49 -1.49,0.63 -0.57,0.14 -1.18,0.21 -1.83,0.21l-4.48 0zm2.72 -6.98l1.28 0c0.67,0 1.15,-0.12 1.45,-0.37 0.3,-0.25 0.45,-0.58 0.45,-1 0,-0.45 -0.15,-0.77 -0.46,-0.96 -0.31,-0.19 -0.78,-0.29 -1.43,-0.29l-1.3 0 0 2.62zm0 4.95l1.54 0c1.49,0 2.24,-0.52 2.24,-1.57 0,-0.51 -0.19,-0.88 -0.56,-1.1 -0.37,-0.22 -0.93,-0.33 -1.68,-0.33l-1.54 0 0 3zm13.53 -3.86l-0.3 1.07 2.57 0 -0.28 -1.07c-0.17,-0.58 -0.34,-1.2 -0.5,-1.86 -0.16,-0.65 -0.32,-1.29 -0.48,-1.89l-0.07 0c-0.15,0.62 -0.3,1.25 -0.45,1.9 -0.15,0.65 -0.32,1.26 -0.49,1.85zm-4.37 5.89l3.78 -11.64 3.28 0 3.78 11.64 -2.89 0 -0.74 -2.77 -3.68 0 -0.74 2.77 -2.78 0zm15.55 0l0 -9.43 -3.31 0 0 -2.21 9.37 0 0 2.21 -3.32 0 0 9.43 -2.74 0zm8.35 -3.59l0 -1.86 4.55 0 0 1.86 -4.55 0zm10.5 3.59l0 -9.43 -3.31 -0 0 -2.21 9.37 0 0 2.21 -3.31 0 0 9.43 -2.74 0zm10.17 0l0 -11.64 7.55 0 0 2.21 -4.83 0 0 2.34 4.11 0 0 2.2 -4.11 0 0 2.68 5.02 0 0 2.21 -7.74 0zm17.59 0.21c-0.76,0 -1.49,-0.13 -2.17,-0.38 -0.68,-0.26 -1.27,-0.63 -1.79,-1.13 -0.51,-0.5 -0.92,-1.12 -1.21,-1.87 -0.3,-0.74 -0.44,-1.6 -0.44,-2.58 0,-0.96 0.15,-1.82 0.46,-2.58 0.31,-0.76 0.72,-1.4 1.24,-1.92 0.52,-0.52 1.12,-0.92 1.81,-1.2 0.69,-0.27 1.42,-0.41 2.19,-0.41 0.78,0 1.47,0.15 2.09,0.45 0.62,0.3 1.13,0.66 1.54,1.06l-1.44 1.68c-0.31,-0.27 -0.63,-0.5 -0.97,-0.67 -0.34,-0.17 -0.73,-0.26 -1.16,-0.26 -0.41,0 -0.79,0.09 -1.16,0.26 -0.36,0.17 -0.68,0.42 -0.94,0.74 -0.27,0.32 -0.48,0.71 -0.63,1.18 -0.15,0.46 -0.23,0.99 -0.23,1.57 0,1.2 0.27,2.13 0.8,2.79 0.53,0.66 1.24,0.99 2.13,0.99 0.49,0 0.93,-0.1 1.32,-0.31 0.39,-0.21 0.73,-0.47 1.03,-0.78l1.44 1.64c-1.01,1.14 -2.31,1.71 -3.91,1.71zm7.96 -0.21l0 -11.64 2.72 0 0 4.48 4.17 0 0 -4.48 2.74 0 0 11.64 -2.74 0 0 -4.86 -4.17 0 0 4.86 -2.72 0z"/>
   <path class="fil8" d="M167.02 397.92l0 -9.71 -3.46 0 0 -1.98 9.27 0 0 1.98 -3.44 0 0 9.71 -2.37 0zm7.34 -11.69l2.37 0 0 6.33c0,1.01 0.03,1.66 0.09,1.95 0.1,0.48 0.34,0.87 0.71,1.15 0.38,0.29 0.89,0.43 1.54,0.43 0.66,0 1.16,-0.14 1.5,-0.41 0.34,-0.28 0.54,-0.61 0.61,-1.01 0.07,-0.4 0.1,-1.06 0.1,-1.99l0 -6.47 2.37 0 0 6.15c0,1.4 -0.06,2.4 -0.19,2.98 -0.13,0.58 -0.36,1.07 -0.7,1.47 -0.34,0.4 -0.8,0.72 -1.37,0.95 -0.57,0.24 -1.32,0.36 -2.24,0.36 -1.11,0 -1.96,-0.13 -2.53,-0.39 -0.57,-0.26 -1.03,-0.59 -1.36,-1.01 -0.33,-0.41 -0.55,-0.84 -0.66,-1.3 -0.16,-0.67 -0.23,-1.66 -0.23,-2.97l0 -6.24zm11.79 11.69l0 -11.69 4.97 0c1.25,0 2.16,0.11 2.72,0.32 0.57,0.21 1.02,0.58 1.36,1.12 0.34,0.54 0.51,1.15 0.51,1.84 0,0.88 -0.26,1.6 -0.77,2.17 -0.51,0.57 -1.28,0.93 -2.3,1.08 0.51,0.3 0.93,0.62 1.26,0.98 0.33,0.36 0.78,0.99 1.34,1.9l1.43 2.28 -2.82 0 -1.69 -2.55c-0.61,-0.91 -1.02,-1.48 -1.25,-1.71 -0.22,-0.23 -0.46,-0.4 -0.71,-0.48 -0.25,-0.09 -0.65,-0.13 -1.19,-0.13l-0.48 0 0 4.88 -2.37 0zm2.37 -6.74l1.75 0c1.13,0 1.84,-0.05 2.12,-0.14 0.28,-0.09 0.5,-0.26 0.66,-0.5 0.16,-0.23 0.24,-0.53 0.24,-0.88 0,-0.39 -0.11,-0.71 -0.32,-0.95 -0.21,-0.24 -0.51,-0.39 -0.89,-0.46 -0.19,-0.03 -0.77,-0.04 -1.72,-0.04l-1.84 0 0 2.97zm9.42 -4.95l4.67 0c0.93,0 1.62,0.04 2.07,0.11 0.46,0.08 0.86,0.24 1.22,0.48 0.36,0.25 0.66,0.57 0.9,0.97 0.24,0.41 0.36,0.86 0.36,1.37 0,0.54 -0.15,1.05 -0.44,1.5 -0.29,0.46 -0.69,0.8 -1.2,1.03 0.71,0.21 1.25,0.56 1.64,1.06 0.38,0.5 0.57,1.09 0.57,1.77 0,0.53 -0.12,1.05 -0.37,1.55 -0.25,0.5 -0.59,0.91 -1.01,1.21 -0.43,0.3 -0.96,0.48 -1.58,0.56 -0.39,0.04 -1.34,0.07 -2.85,0.08l-3.98 0 0 -11.69zm2.37 1.94l0 2.69 1.55 0c0.92,0 1.49,-0.01 1.71,-0.04 0.4,-0.05 0.72,-0.19 0.95,-0.42 0.23,-0.23 0.35,-0.53 0.35,-0.91 0,-0.36 -0.1,-0.65 -0.3,-0.88 -0.2,-0.23 -0.5,-0.36 -0.89,-0.41 -0.23,-0.03 -0.91,-0.04 -2.01,-0.04l-1.35 0zm0 4.64l0 3.13 2.19 0c0.85,0 1.39,-0.02 1.62,-0.07 0.35,-0.07 0.64,-0.22 0.86,-0.47 0.22,-0.25 0.33,-0.58 0.33,-1 0,-0.35 -0.08,-0.65 -0.26,-0.9 -0.17,-0.24 -0.42,-0.42 -0.74,-0.54 -0.32,-0.11 -1.02,-0.17 -2.1,-0.17l-1.91 0zm8.93 -0.66c0,-1.19 0.18,-2.19 0.54,-3 0.26,-0.6 0.63,-1.13 1.09,-1.6 0.46,-0.47 0.96,-0.82 1.51,-1.06 0.73,-0.31 1.57,-0.46 2.52,-0.46 1.72,0 3.1,0.54 4.14,1.61 1.03,1.07 1.55,2.56 1.55,4.47 0,1.89 -0.51,3.37 -1.54,4.44 -1.03,1.07 -2.4,1.6 -4.12,1.6 -1.74,0 -3.12,-0.53 -4.15,-1.59 -1.03,-1.06 -1.54,-2.52 -1.54,-4.39zm2.43 -0.08c0,1.32 0.31,2.33 0.92,3.01 0.61,0.68 1.39,1.03 2.33,1.03 0.94,0 1.72,-0.34 2.32,-1.02 0.6,-0.68 0.91,-1.7 0.91,-3.06 0,-1.34 -0.29,-2.34 -0.88,-3 -0.59,-0.66 -1.37,-0.99 -2.35,-0.99 -0.97,0 -1.76,0.33 -2.36,1 -0.6,0.67 -0.9,1.68 -0.9,3.02zm15.29 -5.84l4.67 0c0.93,0 1.62,0.04 2.07,0.11 0.46,0.08 0.86,0.24 1.22,0.48 0.36,0.25 0.66,0.57 0.9,0.97 0.24,0.41 0.36,0.86 0.36,1.37 0,0.54 -0.15,1.05 -0.44,1.5 -0.29,0.46 -0.69,0.8 -1.2,1.03 0.71,0.21 1.25,0.56 1.64,1.06 0.38,0.5 0.57,1.09 0.57,1.77 0,0.53 -0.12,1.05 -0.37,1.55 -0.25,0.5 -0.59,0.91 -1.01,1.21 -0.43,0.3 -0.96,0.48 -1.58,0.56 -0.39,0.04 -1.34,0.07 -2.85,0.08l-3.98 0 0 -11.69zm2.37 1.94l0 2.69 1.55 0c0.92,0 1.49,-0.01 1.71,-0.04 0.4,-0.05 0.72,-0.19 0.95,-0.42 0.23,-0.23 0.35,-0.53 0.35,-0.91 0,-0.36 -0.1,-0.65 -0.3,-0.88 -0.2,-0.23 -0.5,-0.36 -0.89,-0.41 -0.23,-0.03 -0.91,-0.04 -2.01,-0.04l-1.35 0zm0 4.64l0 3.13 2.19 0c0.85,0 1.39,-0.02 1.62,-0.07 0.35,-0.07 0.64,-0.22 0.86,-0.47 0.22,-0.25 0.33,-0.58 0.33,-1 0,-0.35 -0.08,-0.65 -0.26,-0.9 -0.17,-0.24 -0.42,-0.42 -0.74,-0.54 -0.32,-0.11 -1.02,-0.17 -2.1,-0.17l-1.91 0zm8.93 -0.66c0,-1.19 0.18,-2.19 0.54,-3 0.26,-0.6 0.63,-1.13 1.09,-1.6 0.46,-0.47 0.96,-0.82 1.51,-1.06 0.73,-0.31 1.57,-0.46 2.52,-0.46 1.72,0 3.1,0.54 4.14,1.61 1.03,1.07 1.55,2.56 1.55,4.47 0,1.89 -0.51,3.37 -1.54,4.44 -1.03,1.07 -2.4,1.6 -4.12,1.6 -1.74,0 -3.12,-0.53 -4.15,-1.59 -1.03,-1.06 -1.54,-2.52 -1.54,-4.39zm2.43 -0.08c0,1.32 0.31,2.33 0.92,3.01 0.61,0.68 1.39,1.03 2.33,1.03 0.94,0 1.72,-0.34 2.32,-1.02 0.6,-0.68 0.91,-1.7 0.91,-3.06 0,-1.34 -0.29,-2.34 -0.88,-3 -0.59,-0.66 -1.37,-0.99 -2.35,-0.99 -0.97,0 -1.76,0.33 -2.36,1 -0.6,0.67 -0.9,1.68 -0.9,3.02zm10.26 0.08c0,-1.19 0.18,-2.19 0.54,-3 0.26,-0.6 0.63,-1.13 1.09,-1.6 0.46,-0.47 0.96,-0.82 1.51,-1.06 0.73,-0.31 1.57,-0.46 2.52,-0.46 1.72,0 3.1,0.54 4.14,1.61 1.03,1.07 1.55,2.56 1.55,4.47 0,1.89 -0.51,3.37 -1.54,4.44 -1.03,1.07 -2.4,1.6 -4.12,1.6 -1.74,0 -3.12,-0.53 -4.15,-1.59 -1.03,-1.06 -1.54,-2.52 -1.54,-4.39zm2.43 -0.08c0,1.32 0.31,2.33 0.92,3.01 0.61,0.68 1.39,1.03 2.33,1.03 0.94,0 1.72,-0.34 2.32,-1.02 0.6,-0.68 0.91,-1.7 0.91,-3.06 0,-1.34 -0.29,-2.34 -0.88,-3 -0.59,-0.66 -1.37,-0.99 -2.35,-0.99 -0.97,0 -1.76,0.33 -2.36,1 -0.6,0.67 -0.9,1.68 -0.9,3.02zm10.13 2.06l2.3 -0.23c0.14,0.77 0.42,1.33 0.84,1.69 0.42,0.36 0.99,0.54 1.71,0.54 0.76,0 1.33,-0.16 1.71,-0.48 0.38,-0.32 0.58,-0.7 0.58,-1.13 0,-0.28 -0.08,-0.51 -0.24,-0.71 -0.16,-0.19 -0.44,-0.36 -0.85,-0.5 -0.28,-0.1 -0.9,-0.27 -1.88,-0.51 -1.26,-0.32 -2.14,-0.7 -2.65,-1.16 -0.71,-0.64 -1.07,-1.43 -1.07,-2.36 0,-0.6 0.17,-1.16 0.5,-1.68 0.34,-0.52 0.82,-0.91 1.46,-1.19 0.63,-0.27 1.4,-0.41 2.29,-0.41 1.46,0 2.56,0.32 3.3,0.97 0.74,0.65 1.13,1.52 1.16,2.6l-2.37 0.08c-0.1,-0.6 -0.32,-1.04 -0.64,-1.3 -0.33,-0.26 -0.82,-0.4 -1.48,-0.4 -0.68,0 -1.21,0.14 -1.6,0.42 -0.25,0.18 -0.37,0.42 -0.37,0.73 0,0.28 0.12,0.51 0.35,0.71 0.3,0.25 1.02,0.51 2.16,0.79 1.15,0.27 1.99,0.55 2.54,0.84 0.55,0.29 0.98,0.69 1.29,1.19 0.31,0.5 0.47,1.12 0.47,1.86 0,0.67 -0.19,1.3 -0.56,1.88 -0.37,0.59 -0.9,1.02 -1.58,1.3 -0.68,0.29 -1.53,0.43 -2.54,0.43 -1.48,0 -2.61,-0.34 -3.4,-1.02 -0.79,-0.68 -1.26,-1.67 -1.42,-2.98zm14.14 3.79l0 -9.71 -3.46 0 0 -1.98 9.27 0 0 1.98 -3.44 0 0 9.71 -2.37 0zm13.54 0.95l0 -12.78 0.03 0 13.14 6.35 0 0.07 -13.14 6.37 -0.03 0z"/>
  </g>
  <path class="fil9 str6" d="M216.27 243.79c-10.74,-10.74 -28.65,-7.16 -57.3,-17.91 10.74,-10.74 7.16,-28.65 -14.32,-42.98 21.49,0 35.81,10.74 50.14,-3.58 7.16,10.74 14.33,7.16 21.49,-3.58 7.16,10.74 14.33,14.33 21.49,3.58 14.33,14.33 28.65,3.58 50.14,3.58 -21.49,14.33 -25.07,32.23 -14.32,42.98 -28.65,10.74 -46.56,7.16 -57.3,17.91z"/>
 </g>
</svg>
)rawliteral";

void handleGaugeTempBg() {
  httpServer.send_P(200, "image/svg+xml", GAUGE_TEMP_BG_SVG);
}

void handleGaugePressBg() {
  httpServer.send_P(200, "image/svg+xml", GAUGE_PRESS_BG_SVG);
}

const char PAGE_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="pt-br">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Batmovel - Painel</title>
<style>
  * { box-sizing: border-box; margin: 0; padding: 0; }
  body {
    background: radial-gradient(ellipse at 50% 0%, #0a1428 0%, #020408 70%);
    color: #cfefff;
    font-family: 'Segoe UI', Arial, sans-serif;
    min-height: 100vh;
    display: flex;
    flex-direction: column;
    align-items: center;
    padding: 16px;
    overflow-x: hidden;
  }
  .titulo {
    font-size: 22px;
    font-weight: 800;
    letter-spacing: 4px;
    color: #ffe14d;
    text-shadow: 0 0 8px #ffcc00, 0 0 20px #ff9900;
    margin-bottom: 4px;
  }
  .subtitulo {
    font-size: 11px;
    letter-spacing: 2px;
    color: #4fd8ff;
    opacity: 0.8;
    margin-bottom: 12px;
  }
  .bat-logo {
    width: 46px;
    height: 20px;
    position: relative;
    margin-bottom: 6px;
    filter: drop-shadow(0 0 6px #4fd8ff);
  }
  .bat-logo::before, .bat-logo::after {
    content: "";
    position: absolute;
    top: 0;
    width: 0; height: 0;
    border-style: solid;
  }
  .bat-logo::before {
    left: 0;
    border-width: 20px 23px 0 0;
    border-color: #4fd8ff transparent transparent transparent;
  }
  .bat-logo::after {
    right: 0;
    border-width: 20px 0 0 23px;
    border-color: #4fd8ff transparent transparent transparent;
  }

  .status {
    font-size: 12px;
    padding: 4px 12px;
    border-radius: 12px;
    margin-bottom: 14px;
    border: 1px solid #333;
    transition: all 0.3s;
  }
  .status.ok { color: #7CFC00; border-color: #7CFC00; box-shadow: 0 0 8px #7CFC0055; }
  .status.warn { color: #ff5050; border-color: #ff5050; box-shadow: 0 0 8px #ff505055; }

  .gauges {
    display: flex;
    gap: 18px;
    flex-wrap: wrap;
    justify-content: center;
    width: 100%;
    max-width: 480px;
  }
  .gauge-box {
    background: linear-gradient(160deg, #0c1830, #050a16);
    border: 2px solid #123554;
    border-radius: 16px;
    box-shadow: 0 0 18px #0af0ff22, inset 0 0 20px #00131f;
    padding: 10px 14px 14px;
    width: 200px;
    text-align: center;
  }
  .gauge-label {
    font-size: 11px;
    letter-spacing: 2px;
    color: #4fd8ff;
    text-shadow: 0 0 6px #0af0ff88;
    margin-top: 4px;
  }
  .gauge-valor {
    font-size: 22px;
    font-weight: 700;
    color: #fff4c2;
    text-shadow: 0 0 4px #000, 0 0 10px #ffcc0099;
  }
  .gauge-unidade { font-size: 12px; color: #cfe9f2; }

  /* Camadas do gauge: a imagem de fundo (bezel, marcacoes, faixas,
     tudo isso vem do arquivo SVG externo - editavel sem mexer aqui)
     e o SVG do ponteiro ficam exatamente sobrepostos, um por cima do
     outro, ocupando o mesmo espaco. */
  .gauge-camadas { position: relative; width: 100%; }
  .gauge-bg-img { display: block; width: 100%; height: auto; }
  svg.gauge-needle-svg {
    position: absolute;
    top: 0; left: 0;
    width: 100%; height: 100%;
  }
  /* Valor numerico (temp/pressao) sobreposto DENTRO do mostrador,
     logo abaixo do pivo do ponteiro. A faixa 60%-73% da altura do
     gauge foi conferida renderizando o SVG de fundo: e um vao vazio
     entre o emblema central e os textos decorativos da parte de
     baixo (TEMPERATURA / TURBO BOOST), entao o numero fica visivel
     sem cobrir nada do desenho estatico do fundo. O ponteiro tambem
     nunca varre essa area (ele so se move na metade de cima do
     mostrador, de -90 a +90 graus). */
  .gauge-valor-overlay {
    position: absolute;
    left: 0; right: 0;
    top: 64%;
    transform: translateY(-50%);
    text-align: center;
    pointer-events: none;
  }
  /* IMPORTANTE: NAO colocar "transition" sobre o transform aqui.
     O ponteiro e movido via atributo SVG "transform" (rotate(ang cx cy)).
     Se o CSS tiver "transition: transform", o navegador interpola isso
     como uma matriz (matrix()), e nao como um angulo puro - em saltos
     bruscos de valor essa decomposicao de matriz introduz um pequeno
     deslocamento de translacao durante a transicao, fazendo o pivo
     "fugir" do centro por uma fracao de segundo (o efeito de
     "ponteiro balancando" relatado). A suavizacao do movimento e
     feita manualmente em JS (funcao animarPonteiro), interpolando o
     ANGULO em si e sempre reescrevendo rotate(ang cx cy) - assim o
     pivo nunca sai do centro. */
  .gauge-needle { stroke: #f4fbff; stroke-width: 3; stroke-linecap: round; paint-order: stroke; stroke-linejoin: round; filter: drop-shadow(0 0 2px #000a) drop-shadow(0 0 6px #00eaffcc); }
  .gauge-pivot { fill: #1a1a1a; stroke: #f4fbff; stroke-width: 1.5; }

  .painel-som {
    margin-top: 22px;
    background: linear-gradient(160deg, #0c1830, #050a16);
    border: 2px solid #123554;
    border-radius: 16px;
    box-shadow: 0 0 18px #0af0ff22, inset 0 0 20px #00131f;
    padding: 16px 20px;
    width: 100%;
    max-width: 480px;
    text-align: center;
  }
  .som-titulo {
    font-size: 11px;
    letter-spacing: 2px;
    color: #4fd8ff;
    margin-bottom: 8px;
  }
  .som-texto {
    font-size: 20px;
    font-weight: 700;
    color: #fff4c2;
    text-shadow: 0 0 8px #ffcc0088;
    margin-bottom: 16px;
    min-height: 26px;
  }
  .som-controles {
    display: flex;
    align-items: center;
    justify-content: center;
    gap: 18px;
  }
  .btn-seta {
    background: #0e2338;
    border: 2px solid #2c6a8f;
    color: #4fd8ff;
    font-size: 22px;
    width: 46px;
    height: 46px;
    border-radius: 50%;
    cursor: pointer;
    transition: all 0.15s;
  }
  .btn-seta:active { background: #123554; transform: scale(0.92); }

  .btn-play {
    background: radial-gradient(circle at 35% 30%, #3a2a00, #1a1200 70%);
    border: 3px solid #ffcc00;
    color: #ffe14d;
    font-size: 13px;
    font-weight: 800;
    letter-spacing: 1px;
    width: 90px;
    height: 90px;
    border-radius: 50%;
    cursor: pointer;
    box-shadow: 0 0 16px #ffcc0055, inset 0 0 12px #00000088;
    transition: all 0.15s;
  }
  .btn-play:active { transform: scale(0.94); box-shadow: 0 0 26px #ffcc00aa, inset 0 0 12px #00000088; }
  .btn-play:disabled { opacity: 0.35; cursor: default; }

  .rodape {
    margin-top: 20px;
    font-size: 10px;
    color: #3d6a80;
    letter-spacing: 2px;
  }
</style>
</head>
<body>

  <div class="bat-logo"></div>
  <div class="titulo">BATMOVEL</div>
  <div class="subtitulo">PAINEL DE CONTROLE</div>
  <div id="status" class="status warn">Conectando...</div>

  <div class="gauges">
    <div class="gauge-box">
      <div class="gauge-camadas">
        <img class="gauge-bg-img" src="/gauge_press_bg.svg" alt="Mostrador pressao">
        <svg class="gauge-needle-svg" viewBox="0 0 220 220">
          <line class="gauge-needle" id="needlePress" x1="110" y1="110" x2="110" y2="45"></line>
          <circle class="gauge-pivot" cx="110" cy="110" r="6"></circle>
        </svg>
        <div class="gauge-valor-overlay gauge-valor"><span id="valPress">--</span><span class="gauge-unidade"> bar</span></div>
      </div>
    </div>

    <div class="gauge-box">
      <div class="gauge-camadas">
        <img class="gauge-bg-img" src="/gauge_temp_bg.svg" alt="Mostrador temperatura">
        <svg class="gauge-needle-svg" viewBox="0 0 220 220">
          <line class="gauge-needle" id="needleTemp" x1="110" y1="110" x2="110" y2="45"></line>
          <circle class="gauge-pivot" cx="110" cy="110" r="6"></circle>
        </svg>
        <div class="gauge-valor-overlay gauge-valor"><span id="valTemp">--</span><span class="gauge-unidade"> &deg;C</span></div>
      </div>
    </div>
  </div>

  <div class="painel-som">
    <div class="som-titulo">SELECAO DE AUDIO</div>
    <div class="som-texto" id="somTexto">Aguardando lista de audios...</div>
    <div class="som-controles">
      <button class="btn-seta" id="btnAnterior">&#9664;</button>
      <button class="btn-play" id="btnTocar" disabled>TOCAR</button>
      <button class="btn-seta" id="btnProximo">&#9654;</button>
    </div>
  </div>

  <div class="rodape">SK-TECH SISTEMAS</div>

<script>
  var ws = null;
  var numAudios = 0;
  var indiceSelecionado = 0;

  var TEMP_MIN = -30, TEMP_MAX = 180;
  var PRESS_MIN = -0.5, PRESS_MAX = 2;

  var elStatus = document.getElementById('status');
  var elSomTexto = document.getElementById('somTexto');
  var elValTemp = document.getElementById('valTemp');
  var elValPress = document.getElementById('valPress');
  var elNeedleTemp = document.getElementById('needleTemp');
  var elNeedlePress = document.getElementById('needlePress');
  var elBtnTocar = document.getElementById('btnTocar');

  function setStatus(texto, ok) {
    elStatus.textContent = texto;
    elStatus.className = 'status ' + (ok ? 'ok' : 'warn');
  }

  // ---------- Geometria do ponteiro ----------
  // Convencao: valor 0 fica na HORIZONTAL ESQUERDA (-90 graus), o
  // valor maximo (max) fica na HORIZONTAL DIREITA (+90 graus). Essa
  // mesma convencao foi usada para desenhar as marcacoes na imagem
  // de fundo (gauge_temp_bg.svg / gauge_press_bg.svg) - se voce
  // trocar o fundo por um desenho com outra faixa de valores, ajuste
  // TEMP_MIN/MAX ou PRESS_MIN/MAX abaixo para bater com o novo fundo.
  var GAUGE_CX = 110, GAUGE_CY = 110;

  // Estado de animacao de cada ponteiro (angulo atual exibido na tela
  // e o "id" do requestAnimationFrame em andamento, se houver). Isso
  // substitui a transicao CSS por uma suavizacao feita em JS, que
  // interpola o ANGULO (nao a matriz de transformacao) - assim o
  // pivo do ponteiro nunca sai do centro, mesmo com saltos bruscos
  // de valor.
  var animTemp = { atual: null, alvo: 0, rafId: null };
  var animPress = { atual: null, alvo: 0, rafId: null };

  function animarPonteiro(needleEl, estado, anguloAlvo) {
    estado.alvo = anguloAlvo;
    if (estado.atual === null) {
      // primeira leitura: posiciona direto, sem animar
      estado.atual = anguloAlvo;
      needleEl.setAttribute('transform', 'rotate(' + estado.atual + ' ' + GAUGE_CX + ' ' + GAUGE_CY + ')');
      return;
    }
    if (estado.rafId !== null) return; // ja existe uma animacao rodando, ela vai buscar o novo alvo sozinha

    function passo() {
      var diff = estado.alvo - estado.atual;
      if (Math.abs(diff) < 0.05) {
        estado.atual = estado.alvo;
        needleEl.setAttribute('transform', 'rotate(' + estado.atual + ' ' + GAUGE_CX + ' ' + GAUGE_CY + ')');
        estado.rafId = null;
        return;
      }
      estado.atual += diff * 0.18; // aproxima 18% da distancia restante a cada frame (suave e sem overshoot)
      needleEl.setAttribute('transform', 'rotate(' + estado.atual + ' ' + GAUGE_CX + ' ' + GAUGE_CY + ')');
      estado.rafId = requestAnimationFrame(passo);
    }
    estado.rafId = requestAnimationFrame(passo);
  }

  function anguloValor(valor, max) {
    return -90 + (valor / max) * 180;
  }

  function anguloGauge(valor, min, max) {
    var v = Math.max(min, Math.min(max, valor));
    return anguloValor(v, max);
  }

  function atualizarGauge(needleEl, valor, min, max, estadoAnim) {
    var angulo = anguloGauge(valor, min, max);
    // Usa o atributo "transform" nativo do SVG (rotate com centro
    // explicito em coordenadas do viewBox), em vez de CSS
    // transform-origin - isso evita bug de pivo desalinhado quando o
    // SVG e redimensionado responsivamente pelo CSS (width:100%).
    // A suavizacao do movimento fica por conta de animarPonteiro()
    // (interpola o angulo em JS), NAO de transition no CSS.
    animarPonteiro(needleEl, estadoAnim, angulo);
  }

  function atualizarSomTexto() {
    if (numAudios > 0) {
      elSomTexto.textContent = 'SOM ' + (indiceSelecionado + 1) + ' DE ' + numAudios;
      elBtnTocar.disabled = false;
    } else {
      elSomTexto.textContent = 'Aguardando lista de audios...';
      elBtnTocar.disabled = true;
    }
  }

  function mostrarMensagemTemporaria(texto) {
    var textoAnterior = elSomTexto.textContent;
    elSomTexto.textContent = texto;
    setTimeout(function () {
      atualizarSomTexto();
    }, 1500);
  }

  document.getElementById('btnAnterior').onclick = function () {
    if (numAudios <= 0) return;
    indiceSelecionado = (indiceSelecionado - 1 + numAudios) % numAudios;
    atualizarSomTexto();
  };
  document.getElementById('btnProximo').onclick = function () {
    if (numAudios <= 0) return;
    indiceSelecionado = (indiceSelecionado + 1) % numAudios;
    atualizarSomTexto();
  };
  elBtnTocar.onclick = function () {
    if (numAudios <= 0 || !ws || ws.readyState !== 1) return;
    var evento = 'som' + (indiceSelecionado + 1);
    ws.send(JSON.stringify({ event: evento }));
    mostrarMensagemTemporaria('SOM ' + (indiceSelecionado + 1) + ' ENVIADO');
  };

  function conectar() {
    var url = 'ws://' + location.hostname + ':81/';
    ws = new WebSocket(url);

    ws.onopen = function () {
      setStatus('Conectado', true);
    };

    ws.onclose = function () {
      setStatus('Desconectado - reconectando...', false);
      setTimeout(conectar, 3000);
    };

    ws.onerror = function () {
      ws.close();
    };

    ws.onmessage = function (evt) {
      try {
        var data = JSON.parse(evt.data);

        if (typeof data.num_audios !== 'undefined') {
          numAudios = data.num_audios;
          if (indiceSelecionado >= numAudios) indiceSelecionado = 0;
          atualizarSomTexto();
        }
        if (typeof data.tentativa !== 'undefined') {
          mostrarMensagemTemporaria('TENTATIVA ' + data.tentativa);
        }
        if (typeof data.erro !== 'undefined') {
          mostrarMensagemTemporaria(data.erro);
        }
        if (typeof data.temp !== 'undefined') {
          elValTemp.textContent = data.temp.toFixed(0);
          atualizarGauge(elNeedleTemp, data.temp, TEMP_MIN, TEMP_MAX, animTemp);
        }
        if (typeof data.press !== 'undefined') {
          elValPress.textContent = data.press.toFixed(1);
          atualizarGauge(elNeedlePress, data.press, PRESS_MIN, PRESS_MAX, animPress);
        }
      } catch (e) { /* mensagem invalida, ignora */ }
    };
  }

  conectar();
</script>
</body>
</html>
)rawliteral";

void handleRoot() {
  httpServer.send_P(200, "text/html", PAGE_HTML);
}

// ==========================================================
void onWebSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED:
      Serial.printf("[WS] Cliente %u conectado\n", num);
      lerQuantidadeAudios(num);
      enviarQuantidadeAudios(num);
      break;

    case WStype_DISCONNECTED:
      Serial.printf("[WS] Cliente %u desconectado\n", num);
      break;

    case WStype_TEXT: {
      String msg = String((char*)payload);
      Serial.printf("[WS] Mensagem recebida: %s\n", msg.c_str());

      JsonDocument doc;
      DeserializationError err = deserializeJson(doc, msg);
      if (!err) {
        const char* evento = doc["event"];
        if (evento != nullptr) {
          String ev = String(evento);
          if (ev.startsWith("som")) {
            int numero = ev.substring(3).toInt(); // "som5" -> 5
            if (numero >= 1 && numero <= numAudios) {
              tocarAudio(numero);
            } else {
              Serial.printf("[WS] Numero de audio invalido: %d (max: %d)\n", numero, numAudios);
            }
          }
        }
      }
      break;
    }

    default:
      break;
  }
}

// ==========================================================
void setup() {
  Serial.begin(115200);

  // Sensor de temperatura (MAX6675 nao precisa de begin() - so um
  // pequeno tempo de estabilizacao apos ligar, recomendado pelo fabricante)
  delay(500);
  Serial.println("MAX6675 pronto.");

  // Sensor de pressao PS10 (analogico via A0) - nao precisa de
  // inicializacao, so o pino A0 ja disponivel para leitura.
  Serial.println("Sensor de pressao PS10 pronto (leitura via A0).");
  calibrarZeroPressao(); // tara automatica - assume sem pressao injetada agora

  // DFPlayer Mini (via SoftwareSerial)
  serialDFPlayer.begin(9600);
  delay(1000); // da tempo do cartao SD ficar pronto antes de inicializar
  if (!dfPlayer.begin(serialDFPlayer, false, true)) {
    // false = nao espera confirmacao (ACK); true = reseta o modulo ao iniciar
    // Alguns clones do DFPlayer sao instaveis para responder o ACK a tempo;
    // desativar essa espera evita falso "nao encontrado" mesmo com tudo certo.
    Serial.println("ERRO: DFPlayer Mini nao encontrado! Verifique fiacao/cartao SD.");
  } else {
    dfPlayer.volume(20); // volume de 0 a 30
    Serial.println("DFPlayer Mini pronto.");
  }

  // Cria a rede WiFi (Access Point)
  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.print("Access Point criado. IP: ");
  Serial.println(WiFi.softAPIP()); // normalmente 192.168.4.1

  // Inicia o servidor WebSocket
  webSocket.begin();
  webSocket.onEvent(onWebSocketEvent);

  // Inicia o servidor HTTP (pagina web do painel)
  httpServer.on("/", handleRoot);
  httpServer.on("/gauge_temp_bg.svg", handleGaugeTempBg);
  httpServer.on("/gauge_press_bg.svg", handleGaugePressBg);
  httpServer.begin();
  Serial.println("Servidor web iniciado. Acesse http://192.168.4.1/ pelo navegador.");
}

// ==========================================================
void loop() {
  webSocket.loop();
  httpServer.handleClient();

  if (millis() - ultimoEnvio >= INTERVALO_ENVIO) {
    ultimoEnvio = millis();
    enviarDadosSensores();
  }
}
