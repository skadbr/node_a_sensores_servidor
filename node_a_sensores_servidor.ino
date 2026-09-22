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
viewBox="0 0 577.1 577.1"
 xmlns:xlink="http://www.w3.org/1999/xlink"
 xmlns:xodm="http://www.corel.com/coreldraw/odm/2003">
 <defs>
  <font id="FontID1" horiz-adv-x="573" font-variant="normal" style="fill-rule:nonzero" font-weight="700">
	<font-face 
		font-family="Source Sans Pro">
		<font-face-src>
			<font-face-name name="Source Sans Pro Bold"/>
		</font-face-src>
	</font-face>
   <missing-glyph><path d="M0 0z"/></missing-glyph>
   <glyph unicode="▶" horiz-adv-x="876" d="M45.0229 -58.0148l0 783.011 1.98422 0 804.979 -389.002 0 -4.01568 -804.979 -389.994 -1.98422 0z"/>
   <glyph unicode="◀" horiz-adv-x="876" d="M832.003 724.996l0 -783.011 -1.98422 0 -805.027 389.994 0 4.01568 805.027 389.002 1.98422 0z"/>
  </font>
  <font id="FontID0" horiz-adv-x="722" font-variant="normal" style="fill-rule:nonzero" font-style="normal" font-weight="700">
	<font-face 
		font-family="Arial">
		<font-face-src>
			<font-face-name name="Arial Negrito"/>
		</font-face-src>
	</font-face>
   <missing-glyph><path d="M0 0z"/></missing-glyph>
   <glyph unicode="-" horiz-adv-x="333" d="M55.9867 190.983l0 137.029 270.03 0 0 -137.029 -270.03 0z"/>
   <glyph unicode="0" horiz-adv-x="556" d="M274.501 719.993c69.3274,0 123.503,-24.8337 162.676,-74.5011 46.4893,-58.6475 69.8078,-156.171 69.8078,-292.166 0,-135.809 -23.4664,-233.481 -70.3252,-292.831 -38.8396,-49.0022 -92.8307,-73.5033 -162.158,-73.5033 -69.66,0 -125.831,26.8293 -168.514,80.5248 -42.646,53.6585 -63.969,149.483 -63.969,287.14 0,135.181 23.4664,232.52 70.3252,291.833 38.8396,49.0022 92.8307,73.5033 162.158,73.5033zm0 -114.006c-16.3341,0 -31.0052,-5.32151 -43.6807,-16.0015 -12.8234,-10.4952 -22.8381,-29.49 -29.8226,-56.9845 -9.31264,-35.5137 -14.0059,-95.4915 -14.0059,-179.675 0,-84.3311 4.17591,-142.166 12.4908,-173.651 8.35181,-31.5225 18.847,-52.5129 31.5225,-63.0081 12.6755,-10.4952 27.1619,-15.6689 43.4959,-15.6689 16.3341,0 31.0052,5.32151 43.8285,16.0015 12.6755,10.4952 22.6534,29.49 29.6748,56.9845 9.31264,35.1811 14.0059,95.0111 14.0059,179.342 0,84.1833 -4.17591,142.018 -12.5277,173.503 -8.31486,31.5225 -18.8101,52.6608 -31.4856,63.1559 -12.6755,10.68 -27.1619,16.0015 -43.4959,16.0015z"/>
   <glyph unicode="1" horiz-adv-x="556" d="M393.016 0l-137.029 0 0 518.33c-50,-47.0067 -108.98,-81.8182 -176.977,-104.324l0 124.982c35.8093,11.6778 74.6489,33.8507 116.482,66.6667 42.0177,32.6681 70.6578,70.8426 86.3267,114.339l111.197 0 0 -719.993z"/>
   <glyph unicode="3" horiz-adv-x="556" d="M37.9897 189.837l133.001 16.1493c4.17591,-33.9985 15.5211,-59.9778 33.9985,-77.9749 18.5144,-17.997 40.8352,-27.014 66.9993,-27.014 28.1966,0 51.8477,10.8278 71.1752,32.3356 19.1796,21.5078 28.8248,50.5174 28.8248,87.1766 0,34.4789 -9.16482,61.9734 -27.6423,82.1508 -18.5144,20.1774 -41.02,30.34 -67.6644,30.34 -17.5166,0 -38.3592,-3.51072 -62.6755,-10.3474l15.1515 111.345c36.9919,-0.997783 65.1885,7.16925 84.6637,24.3533 19.5122,16.9993 29.1944,39.8004 29.1944,68.3296 0,24.1685 -7.16925,43.3112 -21.5078,57.8344 -14.3385,14.3016 -33.3333,21.4708 -57.1693,21.4708 -23.5033,0 -43.4959,-8.16704 -60.0148,-24.5011 -16.6667,-16.3341 -26.6445,-40.17 -30.34,-71.4708l-125.979 21.4708c8.83222,43.6807 21.9882,78.5292 39.8374,104.693 17.6644,25.9793 42.4982,46.4893 74.1685,61.493 31.6704,14.8189 67.3319,22.3208 106.652,22.3208 67.3319,0 121.323,-21.5078 162.01,-64.6711 33.4812,-35.3289 50.3326,-75.3141 50.3326,-119.808 0,-63.0081 -34.3311,-113.525 -103.178,-150.998 41.02,-8.83222 73.6881,-28.5292 98.337,-59.1648 24.5011,-30.6726 36.8441,-67.7014 36.8441,-111.013 0,-62.8234 -23.0229,-116.519 -68.847,-160.828 -45.8241,-44.3459 -102.846,-66.5188 -171.175,-66.5188 -64.6341,0 -118.33,18.6622 -160.828,56.1715 -42.646,37.3245 -67.3319,86.1789 -74.1685,146.674z"/>
   <glyph unicode="5" horiz-adv-x="556" d="M44.0133 183.851l136.992 14.1537c3.99113,-31.0052 15.4841,-55.6541 34.6637,-73.8359 19.3274,-18.1818 41.5004,-27.1619 66.6667,-27.1619 28.677,0 52.9933,11.6778 72.8381,35.1441 19.8448,23.5033 29.8226,58.8322 29.8226,106.024 0,44.3089 -9.83001,77.4945 -29.6748,99.6674 -19.66,22.1729 -45.3067,33.1486 -76.9771,33.1486 -39.5048,0 -75.0185,-17.3319 -106.356,-51.9956l-110.976 16.0015 69.9926 371.988 363.008 0 0 -127.975 -258.832 0 -21.6925 -121.84c30.5248,15.1515 61.8625,22.8381 93.8285,22.8381 61.0126,0 112.676,-22.1729 155.026,-66.6667 42.4982,-44.3459 63.6733,-101.996 63.6733,-173.023 0,-59.1648 -17.184,-111.826 -51.3673,-158.315 -46.6371,-63.3407 -111.308,-95.0111 -193.976,-95.0111 -66.1863,0 -120.177,17.8492 -161.826,53.3259 -41.833,35.6984 -66.6667,83.3333 -74.8337,143.533z"/>
   <glyph unicode="8" horiz-adv-x="556" d="M160.347 387.177c-35.3289,15.0037 -61.1973,35.4767 -77.1988,61.8256 -16.1493,26.1641 -24.1316,54.9889 -24.1316,86.1789 0,53.4738 18.6622,97.6349 55.8019,132.483 37.1766,34.8485 90.17,52.3282 158.684,52.3282 67.997,0 120.658,-17.4797 158.167,-52.3282 37.5092,-34.8485 56.3193,-79.0096 56.3193,-132.483 0,-33.1855 -8.64745,-62.6755 -25.9793,-88.6918 -17.184,-25.8315 -41.5004,-45.6393 -72.8381,-59.3126 39.8374,-16.0015 69.9926,-39.357 90.8352,-69.9926 20.6578,-30.8574 31.0052,-66.1863 31.0052,-106.356 0,-66.3341 -21.0273,-120.177 -63.1929,-161.678 -41.9808,-41.5004 -98.0044,-62.1582 -167.997,-62.1582 -65.0037,0 -118.995,17.184 -162.306,51.5152 -51.0347,40.3178 -76.5336,96.0089 -76.5336,166.482 0,38.8396 9.68219,74.5011 28.8618,107.021 19.1426,32.4834 49.3348,57.5018 90.5026,75.1663zm28.6401 137.99c0,-27.3466 7.68662,-48.8174 23.0229,-64.1537 15.4841,-15.3363 35.9941,-23.0229 61.493,-23.0229 25.9793,0 46.6741,7.83444 62.1582,23.3555 15.5211,15.4841 23.3555,36.9919 23.3555,64.3385 0,25.8315 -7.68662,46.4893 -23.0229,61.9734 -15.4841,15.5211 -35.8093,23.3555 -60.9756,23.3555 -26.3489,0 -47.1914,-7.83444 -62.6755,-23.5033 -15.5211,-15.6689 -23.3555,-36.5115 -23.3555,-62.3429zm-12.9712 -306.984c0,-38.1744 9.64523,-67.8492 28.9727,-89.1722 19.3274,-21.3599 43.3481,-32.003 72.1729,-32.003 28.3444,0 51.663,10.3104 70.1774,30.8204 18.4775,20.51 27.6792,50.1848 27.6792,88.8396 0,33.8507 -9.34959,61.0126 -28.1966,81.4856 -18.8101,20.51 -42.646,30.8574 -71.4708,30.8574 -33.5181,0 -58.3518,-11.6778 -74.8337,-34.9963 -16.3341,-23.3555 -24.5011,-48.5218 -24.5011,-75.8315z"/>
   <glyph unicode="A" horiz-adv-x="722" d="M718.003 0l-156.166 0 -61.8371 162.997 -287.165 0 -59.5003 -162.997 -153.335 0 277.997 715.981 153.155 0 286.851 -715.981zm-265.01 284.019l-99.4967 264.965 -96.6655 -264.965 196.162 0z"/>
   <glyph unicode="B" horiz-adv-x="722" d="M72.991 716.02l286.342 0c56.8338,0 99.0221,-2.36217 126.848,-7.03926 27.9681,-4.81882 52.8181,-14.6454 74.8335,-29.4799 21.9682,-15.0234 40.2986,-34.8183 54.9913,-59.6684 14.6454,-24.85 22.0154,-52.8181 22.0154,-83.668 0,-33.3538 -9.02348,-64.1565 -27.0232,-91.9828 -17.9997,-28.0153 -42.519,-48.8496 -73.3217,-62.8337 43.5111,-12.8502 76.8177,-34.5349 100.345,-65.1958 23.291,-30.6609 34.9601,-66.6604 34.9601,-108.14 0,-32.6924 -7.46445,-64.3454 -22.6296,-95.1954 -15.1651,-30.8027 -35.8577,-55.4637 -62.1722,-73.9831 -26.1728,-18.3304 -58.5345,-29.6688 -96.9906,-34.0152 -23.9996,-2.5039 -82.2034,-4.15741 -174.375,-4.81882l-243.823 0 0 716.02zm144.99 -119.006l0 -165.021 94.6757 0c56.1723,0 91.1797,0.85038 104.833,2.5039 24.661,2.8346 44.1725,11.3384 58.3455,25.5114 14.173,14.173 21.165,32.6452 21.165,55.6527 0,22.1571 -6.18888,40.0151 -18.3304,53.8574 -12.1888,13.7951 -30.3302,22.1571 -54.3299,24.9917 -14.362,1.65352 -55.5109,2.5039 -123.352,2.5039l-83.0066 0zm0 -284.027l0 -191.997 134.029 0c51.9677,0 85.1798,1.51179 99.1638,4.34639 21.4957,4.01568 38.9758,13.5116 52.4874,28.6767 13.5116,15.3068 20.3619,35.4798 20.3619,60.9912 0,21.6847 -5.19677,39.8261 -15.6848,54.8495 -10.3463,14.9761 -25.5114,25.9838 -45.1646,32.8341 -19.6532,6.80304 -62.503,10.2991 -128.36,10.2991l-116.833 0z"/>
   <glyph unicode="C" horiz-adv-x="722" d="M531.015 263.996l139.982 -42.9914c-21.4957,-78.8492 -57.3534,-137.336 -107.337,-175.651 -49.9835,-38.1726 -113.337,-57.3534 -190.154,-57.3534 -95.1954,0 -173.336,32.5034 -234.658,97.5103 -61.1801,65.0069 -91.8411,153.824 -91.8411,266.641 0,119.195 30.8027,211.839 92.3135,277.838 61.5108,65.999 142.533,99.0221 242.831,99.0221 87.6837,0 158.832,-26.0311 213.493,-77.857 32.6924,-30.6609 57.0227,-74.6445 73.3689,-132.14l-143.006 -34.0152c-8.5038,36.9915 -26.1728,66.3297 -53.007,87.8254 -27.0232,21.4957 -59.6684,32.1727 -97.9827,32.1727 -53.196,0 -96.3292,-18.9918 -129.352,-57.1645 -33.1648,-37.9837 -49.6528,-99.6835 -49.6528,-185.005 0,-90.3293 16.299,-154.816 48.8024,-193.178 32.6924,-38.5033 75.0224,-57.6369 127.179,-57.6369 38.5033,0 71.6682,12.1415 99.3528,36.6608 27.6374,24.4721 47.6685,62.8337 59.6684,115.321z"/>
   <glyph unicode="E" horiz-adv-x="666" d="M72.9822 0l0 715.981 530.02 0 0 -120.978 -384.999 0 0 -158.997 357.99 0 0 -121.023 -357.99 0 0 -194.005 398.975 0 0 -120.978 -543.996 0z"/>
   <glyph unicode="G" horiz-adv-x="777" d="M406.009 263.004l0 120.99 311.003 0 0 -285.681c-30.1885,-29.4799 -74.0303,-55.322 -131.525,-77.8098 -57.3062,-22.3461 -115.51,-33.4955 -174.47,-33.4955 -74.8335,0 -140.029,15.6375 -195.682,47.1489 -55.5109,31.5113 -97.3213,76.676 -125.337,135.163 -28.0153,58.5345 -41.9993,122.36 -41.9993,191.194 0,74.6445 15.6848,140.974 46.8182,199.131 31.3696,58.0148 77.0067,102.707 137.195,133.699 45.9678,23.8106 102.991,35.6687 171.304,35.6687 88.8647,0 158.36,-18.6611 208.343,-56.0306 49.8417,-37.464 82.0145,-88.9592 96.3292,-154.958l-143.998 -27.0232c-9.96835,35.1491 -29.0074,63.0226 -56.8338,83.479 -27.6374,20.3619 -62.314,30.5192 -103.983,30.5192 -63.0226,0 -113.195,-19.9839 -150.328,-60.188 -37.1805,-40.1568 -55.8416,-99.6362 -55.8416,-178.485 0,-85.1798 18.8501,-149.006 56.503,-191.477 37.8419,-42.519 87.164,-63.8258 148.344,-63.8258 30.1413,0 60.4715,5.99991 90.8017,17.8107 30.5192,11.8108 56.503,26.3145 78.3295,43.3222l0 90.849 -164.974 0z"/>
   <glyph unicode="H" horiz-adv-x="722" d="M72.991 0l0 716.02 144.99 0 0 -282.043 281.996 0 0 282.043 145.037 0 0 -716.02 -145.037 0 0 312.987 -281.996 0 0 -312.987 -144.99 0z"/>
   <glyph unicode="I" horiz-adv-x="277" d="M67.9832 0l0 716.02 145.037 0 0 -716.02 -145.037 0z"/>
   <glyph unicode="M" horiz-adv-x="833" d="M71.0049 0l0 715.981 217.149 0 128.348 -488.316 126.82 488.316 217.688 0 0 -715.981 -134.01 0 -0.494338 563.68 -141.156 -563.68 -139.358 0 -140.482 563.68 -0.494338 -563.68 -134.01 0z"/>
   <glyph unicode="N" horiz-adv-x="722" d="M72.991 0l0 716.02 140.36 0 294.657 -479.851 0 479.851 133.982 0 0 -716.02 -144.801 0 -290.169 470.686 0 -470.686 -134.029 0z"/>
   <glyph unicode="P" horiz-adv-x="666" d="M72.9822 0l0 715.981 231.844 0c87.8573,0 145.021,-3.46036 171.67,-10.6507 40.9851,-10.8305 75.3191,-34.1542 103.002,-70.151 27.6829,-35.9968 41.4794,-82.5094 41.4794,-139.493 0,-44.041 -7.99928,-80.8467 -23.8181,-110.866 -15.9986,-29.9748 -36.1765,-53.4783 -60.6687,-70.6453 -24.4922,-16.9872 -49.4787,-28.357 -74.8247,-33.8397 -34.5138,-6.83085 -84.352,-10.3361 -149.649,-10.3361l-94.014 0 0 -269.998 -145.021 0zm145.021 595.003l0 -203.982 78.8244 0c56.8488,0 94.6881,3.81988 113.832,11.3248 19.1893,7.50494 34.1542,19.1443 44.9847,35.3227 10.8754,15.9986 16.3581,34.5138 16.3581,55.8152 0,26.1999 -7.6847,47.6811 -22.8294,64.6683 -15.3245,17.0322 -34.5138,27.6829 -58.0173,31.9971 -17.167,3.19072 -51.6358,4.8535 -103.676,4.8535l-69.4769 0z"/>
   <glyph unicode="R" horiz-adv-x="722" d="M72.9822 0l0 715.981 304.512 0c76.4875,0 132.168,-6.47133 166.861,-19.3241 34.8283,-12.8078 62.4663,-35.6372 83.3184,-68.4882 20.8071,-32.9858 31.323,-70.5105 31.323,-112.844 0,-53.8379 -15.684,-98.1485 -47.1418,-133.157 -31.5028,-35.0081 -78.5098,-56.9836 -141.201,-66.1514 31.1882,-18.2006 56.8488,-38.1988 77.0268,-59.9946 20.3128,-21.8407 47.6362,-60.6687 81.9702,-116.349l87.3629 -139.673 -172.838 0 -103.676 156.345c-37.1652,55.5006 -62.6461,90.5087 -76.4875,104.979 -13.6617,14.3358 -28.1772,24.3574 -43.5017,29.6603 -15.5042,5.34783 -39.8616,7.99928 -73.162,7.99928l-29.3457 0 0 -298.984 -145.021 0zm145.021 412.997l107.002 0c69.3421,0 112.484,3.01097 129.831,8.85314 17.167,5.79723 30.8287,15.9986 40.4907,30.3344 9.84181,14.3358 14.6953,32.3117 14.6953,53.8379 0,24.1327 -6.51627,43.4568 -19.369,58.4667 -12.8078,14.8301 -30.9635,24.1776 -54.3322,27.9975 -11.8192,1.66277 -47.007,2.51663 -105.474,2.51663l-112.844 0 0 -182.006z"/>
   <glyph unicode="T" horiz-adv-x="610" d="M234.001 0l0 595.003 -211.981 0 0 120.978 567.994 0 0 -120.978 -210.992 0 0 -595.003 -145.021 0z"/>
   <glyph unicode="U" horiz-adv-x="722" d="M72.9822 715.981l145.021 0 0 -387.965c0,-61.7023 1.84253,-101.519 5.34783,-119.675 6.15675,-29.3457 20.6274,-53.0289 43.6365,-70.6903 23.1889,-17.7962 54.6917,-26.6493 94.6881,-26.6493 40.4907,0 71.1397,8.31386 91.812,25.1663 20.6723,16.8524 33.0307,37.345 37.1652,61.8371 4.1794,24.4922 6.33651,64.9829 6.33651,121.652l0 396.324 145.021 0 0 -376.461c0,-86.0147 -3.86482,-146.863 -11.6843,-182.366 -7.81952,-35.5024 -22.1553,-65.4773 -43.1422,-89.9694 -20.8521,-24.5371 -48.8495,-43.8612 -83.8576,-58.3318 -35.1429,-14.5155 -80.8467,-21.8407 -137.336,-21.8407 -68.1736,0 -119.809,7.81952 -154.997,23.6383 -35.1429,15.8637 -63.0056,36.3563 -83.4981,61.7023 -20.4925,25.3011 -33.8397,51.6358 -40.311,79.4985 -9.52723,40.9851 -14.201,101.654 -14.201,181.826l0 382.303z"/>
  </font>
  <style type="text/css">
   <![CDATA[
    @font-face { font-family:"Source Sans Pro";font-variant:normal;font-weight:bold;src:url("#FontID1") format(svg)}
    @font-face { font-family:"Arial";font-variant:normal;font-style:normal;font-weight:bold;src:url("#FontID0") format(svg)}
    .str2 {stroke:#FFCC00;stroke-width:4.08;stroke-miterlimit:22.9256;stroke-opacity:0.400000}
    .str6 {stroke:#FFCC00;stroke-width:5.14;stroke-miterlimit:22.9256;stroke-opacity:0.400000}
    .str1 {stroke:#3A2E10;stroke-width:16.33;stroke-miterlimit:22.9256}
    .str4 {stroke:#FF3030;stroke-width:2.72;stroke-miterlimit:22.9256}
    .str3 {stroke:#FFCC00;stroke-width:2.72;stroke-miterlimit:22.9256}
    .str0 {stroke:black;stroke-width:10.89;stroke-miterlimit:22.9256}
    .str5 {stroke:#FFCC00;stroke-width:8.17;stroke-linecap:round;stroke-miterlimit:22.9256}
    .fil1 {fill:none;fill-rule:nonzero}
    .fil2 {fill:none;fill-rule:nonzero}
    .fil8 {fill:#8A7322}
    .fil6 {fill:#E8D8A0}
    .fil7 {fill:#FFCC00}
    .fil4 {fill:#FFF212;fill-rule:nonzero}
    .fil5 {fill:#ED3237;fill-rule:nonzero}
    .fil9 {fill:#0A1118;fill-rule:nonzero;fill-opacity:0.400000}
    .fil3 {fill:url(#id0);fill-rule:nonzero}
    .fil0 {fill:url(#id1);fill-rule:nonzero}
    .fnt3 {font-weight:bold;font-size:21.78px;font-family:'Arial'}
    .fnt2 {font-weight:bold;font-size:21.78px;font-family:'Source Sans Pro'}
    .fnt4 {font-weight:bold;font-size:21.78px;font-family:'Source Sans Pro'}
    .fnt1 {font-weight:bold;font-size:22.89px;font-family:'Arial'}
    .fnt0 {font-weight:bold;font-size:27.84px;font-family:'Arial'}
   ]]>
  </style>
  <radialGradient id="id0" gradientUnits="objectBoundingBox" cx="50%" cy="50%" r="50%" fx="50%" fy="50%">
   <stop offset="0" style="stop-opacity:0.54902; stop-color:#FFCC00"/>
   <stop offset="0.6" style="stop-opacity:0.14902; stop-color:#FFCC00"/>
   <stop offset="1" style="stop-opacity:0; stop-color:#FFCC00"/>
  </radialGradient>
  <radialGradient id="id1" gradientUnits="userSpaceOnUse" gradientTransform="matrix(1.1 -0 -0 1.1 -29 -29)" cx="288.55" cy="288.55" r="283.11" fx="288.55" fy="288.55">
   <stop offset="0" style="stop-opacity:1; stop-color:#121A24"/>
   <stop offset="0.701961" style="stop-opacity:1; stop-color:#080C12"/>
   <stop offset="1" style="stop-opacity:1; stop-color:#020305"/>
  </radialGradient>
 </defs>
 <g id="Camada_x0020_1">
  <metadata id="CorelCorpID_0Corel-Layer"/>
  <g id="gauge_temp_bg.svg">
   <circle class="fil0 str0" cx="288.55" cy="288.55" r="283.11"/>
   <path class="fil1 str1" d="M76.21 412.56c-21.51,-37.23 -32.85,-79.58 -32.85,-122.6 0,-135.39 109.8,-245.19 245.19,-245.19 135.39,0 245.19,109.8 245.19,245.19 0,0 0,0 0,0"/>
   <path class="fil2 str2" d="M61.95 424.91c-22.95,-39.73 -35.06,-84.93 -35.06,-130.83 0,-144.49 117.17,-261.66 261.66,-261.66 144.49,0 261.66,117.17 261.66,261.66 0,0 0,0 0,0"/>
   <circle class="fil3" cx="288.55" cy="288.55" r="108.89" text-anchor="middle" dominant-baseline="middle"/>
   <g>
    <path class="fil4 str3" d="M288.55 288.55l17.8 -210.96c14.62,2.19 29.9,5.85 43.45,9.79 24.6,7.17 42.4,17.28 42.4,17.28l-103.64 183.88z"/>
    <path class="fil5 str4" d="M288.55 288.55l103.64 -183.88c97.37,49.6 114.44,179.89 103.97,180.08l-207.61 3.8z"/>
    <g>
     <line class="fil1 str5" x1="136.77" y1="376.33" x2="112.67" y2= "390.26" />
     <text x="68.44" y="404.18"  class="fil6 fnt0">-30</text>
     <line class="fil1 str5" x1="113.15" y1="288.16" x2="85.31" y2= "288.16"  text-anchor="middle" dominant-baseline="middle"/>
     <text x="49.72" y="288.16"  class="fil6 fnt0">0</text>
     <line class="fil1 str5" x1="176.15" y1="153.07" x2="158.24" y2= "131.75"  text-anchor="middle" dominant-baseline="middle"/>
     <text x="124.86" y="110.43"  class="fil6 fnt0">50</text>
     <line class="fil1 str5" x1="320.13" y1="114.49" x2="324.95" y2= "87.08"  text-anchor="middle" dominant-baseline="middle"/>
     <text x="306.55" y="59.66"  class="fil6 fnt0">100</text>
     <line class="fil1 str5" x1="442.22" y1="199.99" x2="466.33" y2= "186.07"  text-anchor="middle" dominant-baseline="middle"/>
     <text x="467.21" y="172.15"  class="fil6 fnt0">150</text>
     <line class="fil1 str5" x1="465.84" y1="288.16" x2="493.68" y2= "288.16"  text-anchor="middle" dominant-baseline="middle"/>
     <text x="498.3" y="288.16"  class="fil6 fnt0">180</text>
    </g>
    <text x="193.21" y="458.53"  class="fil7 fnt1">T</text>
    <text x="210.17" y="458.53"  class="fil7 fnt1">E</text>
    <text x="228.65" y="458.53"  class="fil7 fnt1">M</text>
    <text x="250.7" y="458.53"  class="fil7 fnt1">P</text>
    <text x="268.94" y="458.53"  class="fil7 fnt1">E</text>
    <text x="287.42" y="458.53"  class="fil7 fnt1">R</text>
    <text x="306.93" y="458.53"  class="fil7 fnt1">A</text>
    <text x="324.74" y="458.53"  class="fil7 fnt1">T</text>
    <text x="341.93" y="458.53"  class="fil7 fnt1">U</text>
    <text x="361.44" y="458.53"  class="fil7 fnt1">R</text>
    <text x="380.95" y="458.53"  class="fil7 fnt1">A</text>
    <g transform="matrix(1 0 0 1 -2544.6 -4298)">
     <text x="2737.77" y="4790.92"  class="fil8 fnt2">◀</text>
     <text x="2759.48" y="4790.92"  class="fil8 fnt3"> </text>
     <text x="2768.36" y="4790.92"  class="fil8 fnt3">B</text>
     <text x="2786.7" y="4790.92"  class="fil8 fnt3">A</text>
     <text x="2803.64" y="4790.92"  class="fil8 fnt3">T</text>
     <text x="2818.36" y="4790.92"  class="fil8 fnt3">-</text>
     <text x="2828.44" y="4790.92"  class="fil8 fnt3">T</text>
     <text x="2844.36" y="4790.92"  class="fil8 fnt3">E</text>
     <text x="2861.72" y="4790.92"  class="fil8 fnt3">C</text>
     <text x="2880.06" y="4790.92"  class="fil8 fnt3">H</text>
    </g>
    <g transform="matrix(1 0 0 1 -2533.26 -4262.64)">
     <text x="2717.18" y="4790.92"  class="fil8 fnt3">E</text>
     <text x="2734.32" y="4790.92"  class="fil8 fnt3">N</text>
     <text x="2752.88" y="4790.92"  class="fil8 fnt3">G</text>
     <text x="2772.43" y="4790.92"  class="fil8 fnt3">I</text>
     <text x="2781.31" y="4790.92"  class="fil8 fnt3">N</text>
     <text x="2799.65" y="4790.92"  class="fil8 fnt3">E</text>
     <text x="2817.01" y="4790.92"  class="fil8 fnt3"> </text>
     <text x="2825.67" y="4790.92"  class="fil8 fnt3">T</text>
     <text x="2841.81" y="4790.92"  class="fil8 fnt3">E</text>
     <text x="2858.95" y="4790.92"  class="fil8 fnt3">M</text>
     <text x="2879.92" y="4790.92"  class="fil8 fnt3">P</text>
     <text x="2896.66" y="4790.92"  class="fil8 fnt3"> </text>
     <text x="2905.55" y="4790.92"  class="fil8 fnt4">▶</text>
    </g>
   </g>
   <path class="fil9 str6" d="M288.55 333.94c-14.34,-14.33 -38.23,-9.56 -76.45,-23.89 14.34,-14.34 9.56,-38.23 -19.11,-57.34 28.67,0 47.78,14.34 66.89,-4.78 9.56,14.34 19.11,9.56 28.67,-4.78 9.56,14.33 19.11,19.11 28.67,4.78 19.11,19.11 38.23,4.78 66.89,4.78 -28.67,19.11 -33.45,43 -19.11,57.34 -38.23,14.34 -62.12,9.56 -76.45,23.89z"/>
  </g>
 </g>
</svg>
)rawliteral";

const char GAUGE_PRESS_BG_SVG[] PROGMEM = R"rawliteral(<svg xmlns="http://www.w3.org/2000/svg" xml:space="preserve" width="56.0916mm" height="56.0916mm" version="1.1" style="shape-rendering:geometricPrecision; text-rendering:geometricPrecision; image-rendering:optimizeQuality; fill-rule:evenodd; clip-rule:evenodd"
viewBox="0 0 435.97 435.97"
 xmlns:xlink="http://www.w3.org/1999/xlink"
 xmlns:xodm="http://www.corel.com/coreldraw/odm/2003">
 <defs>
  <font id="FontID1" horiz-adv-x="573" font-variant="normal" style="fill-rule:nonzero" font-style="normal" font-weight="700">
	<font-face 
		font-family="Source Sans Pro">
		<font-face-src>
			<font-face-name name="Source Sans Pro Bold"/>
		</font-face-src>
	</font-face>
   <missing-glyph><path d="M0 0z"/></missing-glyph>
   <glyph unicode="-" horiz-adv-x="331" d="M43.0163 201.002l0 103.999 246.005 0 0 -103.999 -246.005 0z"/>
   <glyph unicode="A" horiz-adv-x="572" d="M229.982 330.008l-15.98 -59.9896 138.982 0 -14.9866 59.9896c-9.32884,32.6509 -18.3122,67.3318 -26.9932,103.999 -8.68101,36.6675 -17.3188,71.9962 -25.9998,105.986l-4.01658 0c-7.98998,-34.6808 -16.1527,-70.1391 -24.4882,-106.504 -8.33549,-36.322 -17.1461,-70.8301 -26.5181,-103.481zm-235.985 -330.008l203.982 651.982 177.032 0 203.982 -651.982 -155.999 0 -39.9931 155.006 -199.015 0 -39.9931 -155.006 -149.996 0z"/>
   <glyph unicode="B" horiz-adv-x="604" d="M77.0061 0l0 651.982 222.985 0c33.9898,0 65.5178,-2.46178 94.4977,-7.47171 29.0231,-5.00993 54.3319,-13.8637 76.0128,-26.5181 21.6809,-12.6544 38.6542,-29.3254 51.0063,-50.013 12.3089,-20.6444 18.4849,-46.6442 18.4849,-77.9995 0,-14.6411 -1.9867,-29.3254 -6.00328,-43.9665 -3.9734,-14.6843 -9.67435,-28.332 -16.9733,-41.0296 -7.34214,-12.6544 -16.1959,-23.9699 -26.5181,-33.9898 -10.3222,-9.97668 -22.156,-17.3188 -35.5014,-21.9832l0 -4.01658c16.671,-3.9734 32.0031,-10.1494 45.9964,-18.4849 13.9933,-8.33549 26.3453,-18.8304 37.013,-31.528 10.6677,-12.6544 19.0032,-27.4683 25.0065,-44.4848 6.00328,-17.0165 8.98333,-36.4948 8.98333,-58.478 0,-33.342 -6.47836,-62.0195 -19.4783,-86.0327 -12.9999,-23.9699 -31.0098,-43.8369 -54.0295,-59.4714 -22.9766,-15.6776 -49.8402,-27.3387 -80.5044,-35.0263 -30.6642,-7.64447 -63.6607,-11.4883 -98.9894,-11.4883l-241.988 0zm146.972 390.991l69.0162 0c36.0197,0 62.1923,6.99663 78.5178,20.9899 16.3255,14.0365 24.4882,32.6941 24.4882,56.0162 0,25.352 -8.33549,43.3186 -25.0065,53.9864 -16.671,10.6677 -42.3253,16.0231 -77.0061,16.0231l-70.0095 0 0 -147.016zm0 -276.972l83.0094 0c80.6772,0 121.016,29.3254 121.016,87.9762 0,28.6776 -10.0199,49.1924 -30.0164,61.5013 -19.9965,12.3521 -50.3153,18.4849 -90.9994,18.4849l-83.0094 0 0 -167.962z"/>
   <glyph unicode="C" horiz-adv-x="582" d="M349.011 -12.0066c-41.332,0 -80.3317,7.16939 -116.999,21.5082 -36.6675,14.3388 -68.8434,35.5014 -96.5276,63.488 -27.641,28.0297 -49.4947,62.8401 -65.4746,104.518 -16.0231,41.6775 -24.0131,89.8333 -24.0131,144.511 0,53.9864 8.33549,102.142 25.0065,144.467 16.671,42.3685 38.9997,78.1722 66.9863,107.498 28.0297,29.3686 60.6807,51.6973 97.996,67.0295 37.3586,15.3321 76.6606,22.9766 118.036,22.9766 41.9798,0 79.6407,-8.50825 112.983,-25.4816 33.342,-17.0165 60.983,-36.8403 83.0094,-59.5146l-77.9995 -93.9794c-16.671,15.3321 -34.1626,27.8138 -52.5179,37.4881 -18.3122,9.67435 -39.1725,14.5115 -62.4946,14.5115 -21.9832,0 -42.8436,-4.83718 -62.4946,-14.5115 -19.6942,-9.67435 -36.6675,-23.4949 -51.0063,-41.5047 -14.3388,-18.0098 -25.6543,-39.9931 -33.9898,-65.9929 -8.33549,-25.9998 -12.5248,-55.3252 -12.5248,-88.0193 0,-67.3318 14.3388,-119.504 43.0163,-156.474 28.6776,-37.013 66.9863,-55.498 115.013,-55.498 26.6477,0 50.488,5.83053 71.4779,17.4916 20.9899,11.6611 39.518,26.1726 55.498,43.4914l77.9995 -91.9927c-54.6774,-64.0062 -124.989,-96.0093 -210.979,-96.0093z"/>
   <glyph unicode="E" horiz-adv-x="547" d="M77.0061 0l0 651.982 408.007 0 0 -123.996 -261.035 0 0 -130.992 222.035 0 0 -123.003 -222.035 0 0 -149.996 271.011 0 0 -123.996 -417.984 0z"/>
   <glyph unicode="H" horiz-adv-x="674" d="M77.0061 0l0 651.982 146.972 0 0 -250.972 225.015 0 0 250.972 148.009 0 0 -651.982 -148.009 0 0 272.005 -225.015 0 0 -272.005 -146.972 0z"/>
   <glyph unicode="T" horiz-adv-x="556" d="M203.982 0l0 527.987 -178.976 0 0 123.996 506.003 0 0 -123.996 -179.019 0 0 -527.987 -148.009 0z"/>
   <glyph unicode="▶" horiz-adv-x="876" d="M45.0229 -58.0148l0 783.011 1.98422 0 804.979 -389.002 0 -4.01568 -804.979 -389.994 -1.98422 0z"/>
   <glyph unicode="◀" horiz-adv-x="877" d="M832.015 724.982l0 -782.998 -2.03647 0 -804.973 390.007 0 4.02557 804.973 388.965 2.03647 0z"/>
  </font>
  <font id="FontID0" horiz-adv-x="722" font-variant="normal" style="fill-rule:nonzero" font-style="normal" font-weight="700">
	<font-face 
		font-family="Arial">
		<font-face-src>
			<font-face-name name="Arial Negrito"/>
		</font-face-src>
	</font-face>
   <missing-glyph><path d="M0 0z"/></missing-glyph>
   <glyph unicode="-" horiz-adv-x="333" d="M55.9854 190.993l0 137.01 270.015 0 0 -137.01 -270.015 0z"/>
   <glyph unicode="." horiz-adv-x="277" d="M71.0092 0l0 137.01 136.976 0 0 -137.01 -136.976 0z"/>
   <glyph unicode="0" horiz-adv-x="556" d="M274.504 720.004c69.3168,0 123.506,-24.8325 162.672,-74.4975 46.4875,-58.6793 69.8349,-156.179 69.8349,-292.188 0,-135.836 -23.5201,-233.474 -70.353,-292.809 -38.8202,-49.0088 -92.8369,-73.4959 -162.154,-73.4959 -69.6622,0 -125.855,26.8357 -168.509,80.4725 -42.6539,53.6713 -63.9981,149.513 -63.9981,287.18 0,135.18 23.4855,232.507 70.3184,291.842 38.8547,49.0088 92.8369,73.4959 162.188,73.4959zm0 -114.008c-16.3363,0 -31.0147,-5.31878 -43.6555,-15.9909 -12.848,-10.4994 -22.8639,-29.4951 -29.8404,-57.0215 -9.32514,-35.4701 -14.0222,-95.4963 -14.0222,-179.664 0,-84.3061 4.17904,-142.157 12.5026,-173.655 8.35809,-31.4982 18.8575,-52.4971 31.4982,-62.9965 12.6753,-10.4994 27.181,-15.68 43.5173,-15.68 16.3363,0 30.9802,5.35332 43.8281,16.0254 12.6753,10.4994 22.6566,29.4951 29.6677,56.9869 9.32514,35.1592 13.9877,95.0128 13.9877,179.319 0,84.168 -4.14451,142.018 -12.5026,173.517 -8.32355,31.4982 -18.823,52.6698 -31.4982,63.1692 -12.6407,10.6721 -27.1465,15.9909 -43.4828,15.9909z"/>
   <glyph unicode="1" horiz-adv-x="556" d="M393.003 0l-137.01 0 0 518.339c-49.9758,-47.0056 -109,-81.854 -177.005,-104.338l0 124.991c35.85,11.6737 74.6702,33.8468 116.53,66.6575 41.9977,32.6725 70.6638,70.8365 86.3093,114.354l111.176 0 0 -720.004z"/>
   <glyph unicode="2" horiz-adv-x="556" d="M506.01 126.995l0 -126.995 -481.004 0c5.14609,48.318 20.8261,94.0112 46.8329,137.183 26.0068,43.1374 77.5022,100.47 154.176,171.997 61.8222,57.6432 99.6408,96.8087 113.663,117.324 18.823,28.3208 28.3208,56.3307 28.3208,84.168 0,30.6693 -8.15086,54.1549 -24.6598,70.6638 -16.3363,16.5089 -38.9929,24.6598 -67.8317,24.6598 -28.6662,0 -51.3228,-8.66892 -68.3498,-26.0068 -16.8198,-17.1652 -26.4903,-45.8313 -29.1497,-85.9985l-137.01 13.6769c8.1854,75.6718 33.6741,129.999 76.6733,162.983 42.9992,32.8452 96.6706,49.3541 161.152,49.3541 70.6638,0 126.166,-19.1683 166.678,-57.3323 40.3398,-38.164 60.5098,-85.6876 60.5098,-142.502 0,-32.3271 -5.83684,-62.9965 -17.3378,-92.3534 -11.501,-29.1497 -29.8404,-59.6463 -55.0183,-91.6626 -16.4744,-21.3097 -46.6602,-51.8063 -89.9703,-91.6626 -43.5173,-39.8218 -71.0092,-66.1739 -82.5102,-79.3327 -11.6737,-12.9861 -20.9988,-25.8341 -28.1826,-38.164l273.019 0z"/>
   <glyph unicode="5" horiz-adv-x="556" d="M44.0008 183.843l137.01 14.1604c3.97182,-31.0147 15.4728,-55.6745 34.6412,-73.8413 19.341,-18.1667 41.5141,-27.1465 66.692,-27.1465 28.6662,0 52.9806,11.6392 72.8051,35.1592 19.8591,23.4855 29.8404,58.8174 29.8404,105.996 0,44.3462 -9.80866,77.5022 -29.6677,99.6753 -19.6519,22.1386 -45.3133,33.156 -76.9842,33.156 -39.5109,0 -75.0155,-17.3378 -106.341,-52.0135l-111.004 16.0254 70.0076 371.969 362.99 0 0 -127.996 -258.824 0 -21.655 -121.814c30.4966,15.162 61.8222,22.8293 93.8385,22.8293 60.9933,0 112.661,-22.1731 154.97,-66.6575 42.5157,-44.3462 63.6872,-102.024 63.6872,-172.999 0,-59.1973 -17.1652,-111.833 -51.3573,-158.355 -46.6602,-63.3419 -111.314,-94.9782 -193.997,-94.9782 -66.1394,0 -120.156,17.8214 -161.808,53.326 -41.8595,35.6773 -66.692,83.3391 -74.8429,143.503z"/>
   <glyph unicode="B" horiz-adv-x="722" d="M72.9789 715.993l286.343 0c56.8463,0 98.9979,-2.33676 126.859,-7.01029 27.9962,-4.80834 52.8019,-14.6497 74.8214,-29.4792 21.9746,-15.0092 40.3092,-34.8268 55.0038,-59.6773 14.6497,-24.8056 21.9746,-52.8468 21.9746,-83.6741 0,-33.2989 -8.98755,-64.1711 -26.9627,-91.9876 -18.02,-27.9962 -42.5111,-48.8473 -73.3384,-62.823 43.4998,-12.8522 76.7986,-34.5122 100.301,-65.1598 23.3676,-30.6925 35.0065,-66.6876 35.0065,-108.165 0,-32.6698 -7.50461,-64.3509 -22.6486,-95.1782 -15.189,-30.8273 -35.8603,-55.4981 -62.1939,-74.0125 -26.1538,-18.3346 -58.464,-29.6589 -96.9757,-33.9729 -23.9968,-2.51651 -82.1912,-4.17921 -174.359,-4.85328l-243.832 0 0 715.993zm145.014 -118.995l0 -165.011 94.6839 0c56.1722,0 91.1787,0.853817 104.84,2.51651 24.6708,2.83108 44.1289,11.3243 58.3292,25.4797 14.1554,14.2003 21.1657,32.6698 21.1657,55.6779 0,22.1543 -6.15647,39.9946 -18.3346,53.8354 -12.1781,13.8408 -30.333,22.1543 -54.3298,24.9854 -14.3351,1.6627 -55.4981,2.51651 -123.354,2.51651l-83 0zm0 -284.007l0 -191.974 134.004 0c51.993,0 85.1571,1.48295 99.1776,4.31403 21.4802,3.99946 39.006,13.4813 52.4873,28.6703 13.5263,15.3238 20.3568,35.5008 20.3568,60.9805 0,21.7049 -5.16784,39.8598 -15.6833,54.869 -10.3357,14.9643 -25.5246,25.974 -45.1625,32.8046 -19.6827,6.83054 -62.5084,10.3357 -128.342,10.3357l-116.838 0z"/>
   <glyph unicode="O" horiz-adv-x="777" d="M43.0054 353.66c0,73.0239 11.0098,134.319 32.8495,184.02 16.1327,36.4895 38.4667,69.339 66.6427,98.3238 28.176,28.9849 59.0033,50.51 92.4819,64.6654 44.6681,18.8289 96.1668,28.3108 154.541,28.3108 105.649,0 189.997,-32.8046 253.494,-98.3238 63.3173,-65.6541 94.9984,-156.833 94.9984,-273.671 0,-115.805 -31.5014,-206.489 -94.3244,-271.829 -62.8679,-65.4743 -147.036,-98.1441 -252.191,-98.1441 -106.502,0 -191.345,32.49 -254.168,97.6498 -62.823,65.0249 -94.3244,154.676 -94.3244,268.997zm149.014 4.85328c0,-81.2025 18.8289,-142.677 56.307,-184.694 37.523,-41.8371 85.1571,-62.823 142.857,-62.823 57.8349,0 105.154,20.8511 142.318,62.3287 36.9838,41.6573 55.4981,103.986 55.4981,187.166 0,82.1912 -18.02,143.531 -54.15,183.84 -35.9951,40.4889 -83.8539,60.666 -143.666,60.666 -59.6773,0 -107.851,-20.4916 -144.34,-61.34 -36.4895,-40.9832 -54.8241,-102.638 -54.8241,-185.144z"/>
   <glyph unicode="R" horiz-adv-x="722" d="M72.9789 0l0 715.993 304.543 0c76.4841,0 132.162,-6.47104 166.809,-19.3232 34.8268,-12.8522 62.5084,-35.6806 83.3146,-68.4851 20.8511,-33.0293 31.3666,-70.5073 31.3666,-112.839 0,-53.8354 -15.6833,-98.189 -47.1846,-133.196 -31.5014,-34.9616 -78.5063,-56.9811 -141.15,-66.1484 31.1419,-18.1549 56.8013,-38.1522 76.9784,-59.9919 20.3568,-21.8398 47.679,-60.666 82.0114,-116.344l87.3141 -139.667 -172.831 0 -103.671 156.338c-37.1635,55.4981 -62.6432,90.5047 -76.4841,104.975 -13.6611,14.3351 -28.176,24.3563 -43.4998,29.7039 -15.5035,5.30266 -39.8149,7.99892 -73.1587,7.99892l-29.3444 0 0 -299.016 -145.014 0zm145.014 412.978l106.997 0c69.339,0 112.524,3.01083 129.825,8.85274 17.1662,5.84191 30.8722,15.9978 40.5339,30.333 9.79643,14.3351 14.6497,32.3552 14.6497,53.8354 0,24.1765 -6.51598,43.4998 -19.3232,58.509 -12.8522,14.8295 -31.0071,24.1765 -54.3298,27.9962 -11.8636,1.6627 -47.0049,2.51651 -105.514,2.51651l-112.839 0 0 -182.043z"/>
   <glyph unicode="S" horiz-adv-x="667" d="M35.0065 232.014l141.015 13.9756c8.49324,-47.1397 25.4797,-81.6519 51.3189,-103.806 25.8392,-22.1993 60.666,-33.1641 104.48,-33.1641 46.3308,0 81.3373,9.79643 104.84,29.4792 23.5024,19.6827 35.3211,42.6909 35.3211,68.9795 0,17.0314 -4.98809,31.3666 -14.8295,43.3649 -9.79643,11.8186 -27.1424,22.1543 -51.8132,30.8273 -16.8517,5.97672 -55.1836,16.3124 -115.175,31.3216 -77.1581,19.3232 -131.308,43.0054 -162.495,71.1814 -43.8143,39.5003 -65.6541,87.6736 -65.6541,144.475 0,36.5344 10.3357,70.8668 30.8273,102.683 20.6714,31.8159 50.3303,55.9925 89.1565,72.6644 38.8262,16.6719 85.5166,24.9854 140.52,24.9854 89.4711,0 156.968,-19.8176 202.13,-59.6324 45.3422,-39.8598 69.0244,-92.8414 71.3612,-159.349l-145.014 -4.98809c-6.15647,36.9838 -19.3232,63.4971 -39.5003,79.6747 -20.1771,16.1327 -50.51,24.3113 -90.8192,24.3113 -41.6573,0 -74.3271,-8.67299 -97.8295,-26.019 -15.189,-11.1446 -22.8284,-25.974 -22.8284,-44.6232 0,-17.0314 7.1451,-31.5014 21.4802,-43.4998 18.1549,-15.5035 62.3287,-31.5014 132.521,-48.1733 70.1478,-16.5371 121.961,-33.8381 155.485,-51.4987 33.6584,-17.8403 59.9919,-42.0168 79.0006,-72.8441 19.0087,-30.8273 28.4905,-68.8447 28.4905,-114.007 0,-40.9832 -11.3243,-79.4949 -34.1527,-115.31 -22.8284,-35.8603 -55.0038,-62.5084 -96.6611,-79.8544 -41.7022,-17.4808 -93.6952,-26.1538 -155.844,-26.1538 -90.5047,0 -160.023,20.8062 -208.511,62.6432 -48.4878,41.6573 -77.3379,102.503 -86.8198,182.357z"/>
   <glyph unicode="T" horiz-adv-x="610" d="M233.991 0l0 595.021 -211.971 0 0 120.972 567.968 0 0 -120.972 -210.983 0 0 -595.021 -145.014 0z"/>
   <glyph unicode="U" horiz-adv-x="722" d="M72.9789 715.993l145.014 0 0 -387.993c0,-61.6546 1.84245,-101.514 5.34759,-119.669 6.15647,-29.3444 20.6714,-52.9816 43.6795,-70.6871 23.1429,-17.7954 54.6443,-26.6481 94.6389,-26.6481 40.4889,0 71.1814,8.35842 91.8528,25.1651 20.6714,16.8517 32.9843,37.3433 37.1635,61.8344 4.17921,24.4911 6.33622,65.0249 6.33622,121.691l0 396.306 144.969 0 0 -376.489c0,-86.0109 -3.81971,-146.857 -11.6389,-182.357 -7.86411,-35.5008 -22.1543,-65.4743 -43.1852,-89.9654 -20.8062,-24.536 -48.8024,-43.8593 -83.8089,-58.3292 -35.1863,-14.5149 -80.843,-21.8398 -137.33,-21.8398 -68.1706,0 -119.849,7.81917 -155.035,23.6373 -35.1413,15.863 -63.0027,36.3546 -83.4944,61.6995 -20.4916,25.3 -33.8381,51.6335 -40.3092,79.4949 -9.52681,40.9832 -14.2003,101.649 -14.2003,181.818l0 382.33z"/>
  </font>
  <style type="text/css">
   <![CDATA[
    @font-face { font-family:"Source Sans Pro";font-variant:normal;font-style:normal;font-weight:bold;src:url("#FontID1") format(svg)}
    @font-face { font-family:"Arial";font-variant:normal;font-style:normal;font-weight:bold;src:url("#FontID0") format(svg)}
    .str2 {stroke:#FFCC00;stroke-width:3.08;stroke-miterlimit:22.9256;stroke-opacity:0.400000}
    .str6 {stroke:#FFCC00;stroke-width:3.89;stroke-miterlimit:22.9256;stroke-opacity:0.400000}
    .str1 {stroke:#3A2E10;stroke-width:12.34;stroke-miterlimit:22.9256}
    .str4 {stroke:#FF3030;stroke-width:2.06;stroke-miterlimit:22.9256}
    .str3 {stroke:#FFCC00;stroke-width:2.06;stroke-miterlimit:22.9256}
    .str0 {stroke:black;stroke-width:8.23;stroke-miterlimit:22.9256}
    .str5 {stroke:#FFCC00;stroke-width:6.17;stroke-linecap:round;stroke-miterlimit:22.9256}
    .fil1 {fill:none;fill-rule:nonzero}
    .fil2 {fill:none;fill-rule:nonzero}
    .fil8 {fill:#8A7322}
    .fil5 {fill:#E8D8A0}
    .fil7 {fill:#FFCC00}
    .fil3 {fill:#FFF212;fill-rule:nonzero}
    .fil4 {fill:#ED3237;fill-rule:nonzero}
    .fil9 {fill:#0A1118;fill-rule:nonzero;fill-opacity:0.400000}
    .fil0 {fill:url(#id0);fill-rule:nonzero}
    .fil6 {fill:url(#id1);fill-rule:nonzero}
    .fnt3 {font-weight:bold;font-size:16.41px;font-family:'Arial'}
    .fnt2 {font-weight:bold;font-size:16.41px;font-family:'Source Sans Pro'}
    .fnt5 {font-weight:bold;font-size:16.45px;font-family:'Arial'}
    .fnt6 {font-weight:bold;font-size:16.45px;font-family:'Source Sans Pro'}
    .fnt1 {font-weight:bold;font-size:17.3px;font-family:'Arial'}
    .fnt4 {font-weight:bold;font-size:18px;font-family:'Source Sans Pro'}
    .fnt0 {font-weight:bold;font-size:22.5px;font-family:'Arial'}
   ]]>
  </style>
  <radialGradient id="id0" gradientUnits="userSpaceOnUse" gradientTransform="matrix(1.1 -0 -0 1.1 -22 -22)" cx="217.99" cy="217.99" r="213.87" fx="217.99" fy="217.99">
   <stop offset="0" style="stop-opacity:1; stop-color:#121A24"/>
   <stop offset="0.701961" style="stop-opacity:1; stop-color:#080C12"/>
   <stop offset="1" style="stop-opacity:1; stop-color:#020305"/>
  </radialGradient>
  <radialGradient id="id1" gradientUnits="userSpaceOnUse" gradientTransform="matrix(0.999995 2.96705E-06 -2.96703E-06 0.99999 0 0)" cx="217.99" cy="217.99" r="75.02" fx="217.99" fy="217.99">
   <stop offset="0" style="stop-opacity:0.54902; stop-color:#FFCC00"/>
   <stop offset="0.6" style="stop-opacity:0.14902; stop-color:#FFCC00"/>
   <stop offset="1" style="stop-opacity:0; stop-color:#FFCC00"/>
  </radialGradient>
 </defs>
 <g id="Camada_x0020_1">
  <metadata id="CorelCorpID_0Corel-Layer"/>
  <circle class="fil0 str0" cx="217.99" cy="217.99" r="213.87"/>
  <path class="fil1 str1" d="M57.57 305.11c-16.25,-28.12 -24.82,-60.12 -24.82,-92.62 0,-102.28 82.95,-185.23 185.23,-185.23 102.28,0 185.23,82.95 185.23,185.23 0,0 0,0 0,0"/>
  <path class="fil2 str2" d="M46.8 314.44c-17.34,-30.01 -26.48,-64.16 -26.48,-98.84 0,-109.15 88.52,-197.67 197.67,-197.67 109.15,0 197.67,88.52 197.67,197.67 0,0 0,0 0,0"/>
  <g id="gauge_press_bg.svg">
   <path class="fil3 str3" d="M217.99 217.99l0 -165.03c17.3,0 34.55,2.74 50.99,8.08l-50.99 156.95z"/>
   <path class="fil4 str4" d="M217.99 217.99l50.99 -156.95c67.98,22.09 114.04,85.47 114.04,156.96l-165.03 -0.01z"/>
   <g>
    <line class="fil1 str5" x1="117.2" y1="318.77" x2="101.28" y2= "334.69" />
    <g transform="matrix(1 2.96706E-06 -2.96706E-06 1 -2719.19 -3268.73)">
     <text x="2785.19" y="3619.32"  class="fil5 fnt0">-0.5</text>
    </g>
    <line class="fil1 str5" x1="75.46" y1="217.99" x2="52.95" y2= "217.99"  text-anchor="middle" dominant-baseline="middle"/>
    <g transform="matrix(1 2.96706E-06 -2.96706E-06 1 -2774.12 -3401.34)">
     <text x="2798.32" y="3619.32"  class="fil5 fnt0">0</text>
    </g>
    <line class="fil1 str5" x1="117.2" y1="117.2" x2="101.28" y2= "101.28"  text-anchor="middle" dominant-baseline="middle"/>
    <g transform="matrix(1 2.96706E-06 -2.96706E-06 1 -2719.19 -3533.95)">
     <text x="2788.94" y="3619.32"  class="fil5 fnt0">0.5</text>
    </g>
    <line class="fil1 str5" x1="217.99" y1="75.46" x2="217.99" y2= "52.95"  text-anchor="middle" dominant-baseline="middle"/>
    <g transform="matrix(1 2.96706E-06 -2.96706E-06 1 -2586.58 -3588.88)">
     <text x="2798.32" y="3619.32"  class="fil5 fnt0">1</text>
    </g>
    <line class="fil1 str5" x1="318.77" y1="117.2" x2="334.69" y2= "101.28"  text-anchor="middle" dominant-baseline="middle"/>
    <g transform="matrix(1 2.96706E-06 -2.96706E-06 1 -2453.97 -3533.95)">
     <text x="2788.94" y="3619.32"  class="fil5 fnt0">1.5</text>
    </g>
    <line class="fil1 str5" x1="360.52" y1="217.99" x2="383.02" y2= "217.99"  text-anchor="middle" dominant-baseline="middle"/>
    <g transform="matrix(1 2.96706E-06 -2.96706E-06 1 -2399.04 -3401.34)">
     <text x="2798.32" y="3619.32"  class="fil5 fnt0">2</text>
    </g>
   </g>
   <path class="fil6" d="M293 217.99c0,-41.43 -33.59,-75.02 -75.02,-75.02 -41.43,0 -75.02,33.58 -75.02,75.02 -0,41.43 33.58,75.02 75.01,75.02 41.43,0 75.02,-33.58 75.02,-75.02z" text-anchor="middle" dominant-baseline="middle"/>
   <g transform="matrix(1 2.96706E-06 -2.96706E-06 1 -2600.34 -3275.7)">
    <text x="2740.68" y="3619.32"  class="fil7 fnt1">T</text>
    <text x="2753.5" y="3619.32"  class="fil7 fnt1">U</text>
    <text x="2768.41" y="3619.32"  class="fil7 fnt1">R</text>
    <text x="2783.15" y="3619.32"  class="fil7 fnt1">B</text>
    <text x="2797.89" y="3619.32"  class="fil7 fnt1">O</text>
    <text x="2820.81" y="3619.32"  class="fil7 fnt1">B</text>
    <text x="2835.55" y="3619.32"  class="fil7 fnt1">O</text>
    <text x="2851.43" y="3619.32"  class="fil7 fnt1">O</text>
    <text x="2867.13" y="3619.32"  class="fil7 fnt1">S</text>
    <text x="2880.92" y="3619.32"  class="fil7 fnt1">T</text>
    <text x="2813.76" y="3619.32"  class="fil7 fnt1"> </text>
   </g>
   <g transform="matrix(1.03697 2.96706E-06 -3.07677E-06 1 -2703.1 -3250.55)">
    <text x="2757.01" y="3619.32"  class="fil8 fnt2">◀</text>
    <text x="2773.38" y="3619.32"  class="fil8 fnt3"> </text>
    <text x="2780.47" y="3619.32"  class="fil8 fnt4">B</text>
    <text x="2793.51" y="3619.32"  class="fil8 fnt4">A</text>
    <text x="2804.85" y="3619.32"  class="fil8 fnt4">T</text>
    <text x="2815.86" y="3619.32"  class="fil8 fnt4">-</text>
    <text x="2823.17" y="3619.32"  class="fil8 fnt4">T</text>
    <text x="2835.34" y="3619.32"  class="fil8 fnt4">E</text>
    <text x="2847.54" y="3619.32"  class="fil8 fnt4">C</text>
    <text x="2860.17" y="3619.32"  class="fil8 fnt4">H</text>
   </g>
   <g transform="matrix(1 2.96706E-06 -2.96706E-06 1 -2592.34 -3218.25)">
    <text x="2756.84" y="3619.32"  class="fil8 fnt5">TURBO BOOST</text>
    <text x="2878.4" y="3619.32"  class="fil8 fnt5"> </text>
    <text x="2885.11" y="3619.32"  class="fil8 fnt6">▶</text>
   </g>
  </g>
  <path class="fil9 str6" d="M217.99 245.72c-10.83,-10.83 -28.88,-7.22 -57.76,-18.05 10.83,-10.83 7.22,-28.88 -14.44,-43.32 21.66,0 36.1,10.83 50.54,-3.61 7.22,10.83 14.44,7.22 21.66,-3.61 7.22,10.83 14.44,14.44 21.66,3.61 14.44,14.44 28.88,3.61 50.54,3.61 -21.66,14.44 -25.27,32.49 -14.44,43.32 -28.88,10.83 -46.93,7.22 -57.76,18.05z"/>
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
