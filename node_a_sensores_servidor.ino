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
  <font id="FontID1" horiz-adv-x="573" font-variant="normal" style="fill-rule:nonzero" font-style="normal" font-weight="700">
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
    @font-face { font-family:"Source Sans Pro";font-variant:normal;font-style:normal;font-weight:bold;src:url("#FontID1") format(svg)}
    @font-face { font-family:"Arial";font-variant:normal;font-style:normal;font-weight:bold;src:url("#FontID0") format(svg)}
    .str2 {stroke:#FFCC00;stroke-width:4.08;stroke-miterlimit:22.9256;stroke-opacity:0.400000}
    .str7 {stroke:#FFCC00;stroke-width:5.14;stroke-miterlimit:22.9256;stroke-opacity:0.400000}
    .str3 {stroke:#756E6E;stroke-width:4.08;stroke-miterlimit:22.9256;stroke-opacity:0.800000}
    .str1 {stroke:#3A2E10;stroke-width:16.33;stroke-miterlimit:22.9256}
    .str5 {stroke:#FF3030;stroke-width:2.72;stroke-miterlimit:22.9256}
    .str4 {stroke:#FFCC00;stroke-width:2.72;stroke-miterlimit:22.9256}
    .str0 {stroke:black;stroke-width:10.89;stroke-miterlimit:22.9256}
    .str6 {stroke:#FFCC00;stroke-width:8.17;stroke-linecap:round;stroke-miterlimit:22.9256}
    .fil1 {fill:none;fill-rule:nonzero}
    .fil2 {fill:none;fill-rule:nonzero}
    .fil4 {fill:none;fill-rule:nonzero}
    .fil9 {fill:#8A7322}
    .fil7 {fill:#E8D8A0}
    .fil8 {fill:#FFCC00}
    .fil5 {fill:#FFF212;fill-rule:nonzero}
    .fil6 {fill:#ED3237;fill-rule:nonzero}
    .fil10 {fill:#0A1118;fill-rule:nonzero;fill-opacity:0.400000}
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
    <path class="fil4 str3" d="M563.11 284.17c0,-151.85 -123.09,-274.94 -274.94,-274.94 -151.85,0 -274.94,123.09 -274.94,274.94 0,151.85 123.09,274.94 274.94,274.94 151.85,0 274.94,-123.09 274.94,-274.94z"/>
    <path class="fil5 str4" d="M288.55 288.55l17.8 -210.96c14.62,2.19 29.9,5.85 43.45,9.79 24.6,7.17 42.4,17.28 42.4,17.28l-103.64 183.88z"/>
    <path class="fil6 str5" d="M288.55 288.55l103.64 -183.88c97.37,49.6 114.44,179.89 103.97,180.08l-207.61 3.8z"/>
    <g>
     <line class="fil1 str6" x1="136.77" y1="376.33" x2="112.67" y2= "390.26" />
     <text x="68.44" y="404.18"  class="fil7 fnt0">-30</text>
     <line class="fil1 str6" x1="113.15" y1="288.16" x2="85.31" y2= "288.16"  text-anchor="middle" dominant-baseline="middle"/>
     <text x="49.72" y="288.16"  class="fil7 fnt0">0</text>
     <line class="fil1 str6" x1="176.15" y1="153.07" x2="158.24" y2= "131.75"  text-anchor="middle" dominant-baseline="middle"/>
     <text x="124.86" y="110.43"  class="fil7 fnt0">50</text>
     <line class="fil1 str6" x1="320.13" y1="114.49" x2="324.95" y2= "87.08"  text-anchor="middle" dominant-baseline="middle"/>
     <text x="306.55" y="59.66"  class="fil7 fnt0">100</text>
     <line class="fil1 str6" x1="442.22" y1="199.99" x2="466.33" y2= "186.07"  text-anchor="middle" dominant-baseline="middle"/>
     <text x="467.21" y="172.15"  class="fil7 fnt0">150</text>
     <line class="fil1 str6" x1="465.84" y1="288.16" x2="493.68" y2= "288.16"  text-anchor="middle" dominant-baseline="middle"/>
     <text x="498.3" y="288.16"  class="fil7 fnt0">180</text>
    </g>
    <text x="193.21" y="458.53"  class="fil8 fnt1">T</text>
    <text x="210.17" y="458.53"  class="fil8 fnt1">E</text>
    <text x="228.65" y="458.53"  class="fil8 fnt1">M</text>
    <text x="250.7" y="458.53"  class="fil8 fnt1">P</text>
    <text x="268.94" y="458.53"  class="fil8 fnt1">E</text>
    <text x="287.42" y="458.53"  class="fil8 fnt1">R</text>
    <text x="306.93" y="458.53"  class="fil8 fnt1">A</text>
    <text x="324.74" y="458.53"  class="fil8 fnt1">T</text>
    <text x="341.93" y="458.53"  class="fil8 fnt1">U</text>
    <text x="361.44" y="458.53"  class="fil8 fnt1">R</text>
    <text x="380.95" y="458.53"  class="fil8 fnt1">A</text>
    <g transform="matrix(1 0 0 1 -2544.6 -4298)">
     <text x="2737.77" y="4790.92"  class="fil9 fnt2">◀</text>
     <text x="2759.48" y="4790.92"  class="fil9 fnt3"> </text>
     <text x="2768.36" y="4790.92"  class="fil9 fnt3">B</text>
     <text x="2786.7" y="4790.92"  class="fil9 fnt3">A</text>
     <text x="2803.64" y="4790.92"  class="fil9 fnt3">T</text>
     <text x="2818.36" y="4790.92"  class="fil9 fnt3">-</text>
     <text x="2828.44" y="4790.92"  class="fil9 fnt3">T</text>
     <text x="2844.36" y="4790.92"  class="fil9 fnt3">E</text>
     <text x="2861.72" y="4790.92"  class="fil9 fnt3">C</text>
     <text x="2880.06" y="4790.92"  class="fil9 fnt3">H</text>
    </g>
    <g transform="matrix(1 0 0 1 -2533.26 -4262.64)">
     <text x="2717.18" y="4790.92"  class="fil9 fnt3">E</text>
     <text x="2734.32" y="4790.92"  class="fil9 fnt3">N</text>
     <text x="2752.88" y="4790.92"  class="fil9 fnt3">G</text>
     <text x="2772.43" y="4790.92"  class="fil9 fnt3">I</text>
     <text x="2781.31" y="4790.92"  class="fil9 fnt3">N</text>
     <text x="2799.65" y="4790.92"  class="fil9 fnt3">E</text>
     <text x="2817.01" y="4790.92"  class="fil9 fnt3"> </text>
     <text x="2825.67" y="4790.92"  class="fil9 fnt3">T</text>
     <text x="2841.81" y="4790.92"  class="fil9 fnt3">E</text>
     <text x="2858.95" y="4790.92"  class="fil9 fnt3">M</text>
     <text x="2879.92" y="4790.92"  class="fil9 fnt3">P</text>
     <text x="2896.66" y="4790.92"  class="fil9 fnt3"> </text>
     <text x="2905.55" y="4790.92"  class="fil9 fnt4">▶</text>
    </g>
   </g>
   <path class="fil10 str7" d="M288.55 333.94c-14.34,-14.33 -38.23,-9.56 -76.45,-23.89 14.34,-14.34 9.56,-38.23 -19.11,-57.34 28.67,0 47.78,14.34 66.89,-4.78 9.56,14.34 19.11,9.56 28.67,-4.78 9.56,14.33 19.11,19.11 28.67,4.78 19.11,19.11 38.23,4.78 66.89,4.78 -28.67,19.11 -33.45,43 -19.11,57.34 -38.23,14.34 -62.12,9.56 -76.45,23.89z"/>
  </g>
 </g>
</svg>
)rawliteral";

const char GAUGE_PRESS_BG_SVG[] PROGMEM = R"rawliteral(<svg xmlns="http://www.w3.org/2000/svg" xml:space="preserve" width="56.0916mm" height="56.0916mm" version="1.1" style="shape-rendering:geometricPrecision; text-rendering:geometricPrecision; image-rendering:optimizeQuality; fill-rule:evenodd; clip-rule:evenodd"
viewBox="0 0 435.97 435.97"
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
   <glyph unicode="▶" horiz-adv-x="877" d="M45.0189 -58.0221l0 783.039 1.96861 0 805.004 -389.007 0 -3.98902 -805.004 -390.043 -1.96861 0z"/>
   <glyph unicode="◀" horiz-adv-x="877" d="M831.995 725.017l0 -783.039 -2.02041 0 -804.953 390.043 0 3.98902 804.953 389.007 2.02041 0z"/>
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
   <glyph unicode="A" horiz-adv-x="722" d="M718.023 0l-156.193 0 -61.8039 162.98 -287.209 0 -59.4726 -162.98 -153.344 0 277.988 716.003 153.189 0 286.847 -716.003zm-265.037 283.997l-99.4664 264.985 -96.6689 -264.985 196.135 0z"/>
   <glyph unicode="B" horiz-adv-x="722" d="M72.9777 716.002l286.342 0c56.8581,0 99.0133,-2.34467 126.856,-6.98515 27.9894,-4.83587 52.8038,-14.703 74.8339,-29.5037 21.9812,-14.9961 40.2989,-34.8281 55.002,-59.6913 14.6542,-24.8144 21.9812,-52.8038 21.9812,-83.6753 0,-33.3138 -8.98789,-64.1364 -27.0125,-91.9793 -17.9758,-27.9894 -42.4971,-48.8472 -73.3197,-62.8175 43.5229,-12.8468 76.8367,-34.535 100.332,-65.1622 23.349,-30.676 35.0234,-66.6764 35.0234,-108.197 0,-32.6788 -7.52247,-64.3318 -22.6651,-95.1544 -15.1915,-30.8226 -35.8538,-55.4904 -62.1825,-74.0035 -26.1821,-18.3177 -58.519,-29.6503 -97.0106,-33.9977 -23.984,-2.49121 -82.161,-4.15201 -174.336,-4.83587l-243.845 0 0 716.002zm145.027 -118.992l0 -165.006 94.6659 0c56.1743,0 91.1489,0.830403 104.826,2.49121 24.6678,2.83314 44.1579,11.3326 58.3236,25.4982 14.1657,14.1657 21.1997,32.6788 21.1997,55.6858 0,22.1766 -6.2036,40.0059 -18.3665,53.8296 -12.163,13.8238 -30.3341,22.1766 -54.3181,25.0098 -14.3122,1.66081 -55.4904,2.49121 -123.339,2.49121l-82.9914 0zm0 -283.998l0 -192.018 133.988 0c52.0223,0 85.1895,1.51426 99.1598,4.3474 21.4928,4.00547 39.0289,13.4818 52.5107,28.6733 13.4818,15.338 20.3204,35.4631 20.3204,60.9613 0,21.6882 -5.12896,39.8593 -15.6311,54.8554 -10.3556,14.9961 -25.4982,25.9867 -45.1837,32.8253 -19.6854,6.83861 -62.5244,10.3556 -128.322,10.3556l-116.843 0z"/>
   <glyph unicode="C" horiz-adv-x="722" d="M531.006 264l139.978 -42.9985c-21.4992,-78.8478 -57.2968,-137.336 -107.341,-175.672 -49.9922,-38.1806 -113.298,-57.3486 -190.126,-57.3486 -95.1666,0 -173.341,32.5338 -234.679,97.4978 -61.1822,65.0158 -91.851,153.862 -91.851,266.694 0,119.152 30.8242,211.832 92.3691,277.832 61.493,66.0001 142.465,99.0002 242.812,99.0002 87.6548,0 158.835,-26.0063 213.49,-77.8635 32.6892,-30.617 56.986,-74.6516 73.3565,-132.156l-143.035 -33.9844c-8.49609,36.9891 -26.1617,66.3109 -52.9969,87.8102 -26.9906,21.4992 -59.628,32.1712 -97.964,32.1712 -53.2042,0 -96.3581,-18.9608 -129.358,-57.1414 -33.1555,-38.0252 -49.6814,-99.6736 -49.6814,-184.997 0,-90.3487 16.3705,-154.846 48.8525,-193.182 32.6892,-38.4914 75.0142,-57.6594 127.182,-57.6594 38.4914,0 71.6469,12.1743 99.311,36.6782 27.6641,24.504 47.661,62.84 59.6798,115.319z"/>
   <glyph unicode="E" horiz-adv-x="666" d="M72.9938 0l0 716.003 530.021 0 0 -121.017 -385.018 0 0 -158.991 358.027 0 0 -121.017 -358.027 0 0 -193.959 399.005 0 0 -121.017 -544.009 0z"/>
   <glyph unicode="H" horiz-adv-x="722" d="M72.9938 0l0 716.003 145.003 0 0 -281.977 282.029 0 0 281.977 144.952 0 0 -716.003 -144.952 0 0 313.008 -282.029 0 0 -313.008 -145.003 0z"/>
   <glyph unicode="O" horiz-adv-x="777" d="M42.9855 353.654c0,73.0266 10.9906,134.33 32.8253,184.007 16.1684,36.4889 38.5404,69.363 66.6764,98.3294 28.1848,29.0152 59.0074,50.508 92.5166,64.6737 44.6463,18.855 96.1801,28.3314 154.504,28.3314 105.657,0 190.016,-32.8253 253.468,-98.3294 63.3548,-65.6506 95.0078,-156.848 95.0078,-273.642 0,-115.866 -31.5064,-206.526 -94.324,-271.835 -62.8175,-65.5041 -146.981,-98.1829 -252.149,-98.1829 -106.487,0 -191.335,32.4834 -254.201,97.6456 -62.8175,65.0156 -94.324,154.699 -94.324,269.002zm149.033 4.83587c0,-81.1352 18.8062,-142.634 56.3208,-184.642 37.5147,-41.8621 85.1407,-62.8664 142.829,-62.8664 57.8351,0 105.168,20.8578 142.341,62.329 36.9773,41.6667 55.4904,104.045 55.4904,187.182 0,82.161 -17.9758,143.513 -54.1716,183.861 -36.0004,40.4943 -83.8218,60.6682 -143.66,60.6682 -59.6913,0 -107.855,-20.5158 -144.343,-61.3521 -36.4889,-40.9828 -54.8066,-102.677 -54.8066,-185.18z"/>
   <glyph unicode="R" horiz-adv-x="722" d="M72.9777 0l0 716.002 304.513 0c76.4947,0 132.181,-6.49668 166.862,-19.3435 34.8281,-12.8468 62.4756,-35.6585 83.3333,-68.4838 20.8089,-33.0207 31.3111,-70.4865 31.3111,-112.837 0,-53.8296 -15.68,-98.1829 -47.1864,-133.157 -31.4576,-35.0234 -78.4975,-57.0047 -141.168,-66.188 31.2134,-18.1712 56.8581,-38.1497 77.032,-59.9844 20.3204,-21.8347 47.6749,-60.6682 82.0145,-116.354l87.29 -139.654 -172.821 0 -103.654 156.311c-37.1727,55.5393 -62.671,90.5139 -76.4947,105.021 -13.6772,14.3122 -28.1848,24.3259 -43.5229,29.6503 -15.4846,5.37319 -39.8105,8.01094 -73.1731,8.01094l-29.3083 0 0 -298.994 -145.027 0zm145.027 413.003l106.975 0c69.363,0 112.495,2.97968 129.836,8.84134 17.1942,5.81282 30.8714,15.973 40.4943,30.3341 9.86714,14.3122 14.703,32.3369 14.703,53.8296 0,24.1794 -6.49668,43.474 -19.3435,58.4701 -12.8468,14.8496 -31.018,24.1794 -54.3181,28.0383 -11.8699,1.66081 -47.0399,2.49121 -105.51,2.49121l-112.837 0 0 -182.005z"/>
   <glyph unicode="S" horiz-adv-x="667" d="M35.0234 232.024l140.973 13.9703c8.49941,-47.1376 25.4982,-81.6725 51.3384,-103.849 25.8402,-22.1278 60.6682,-33.1673 104.484,-33.1673 46.356,0 81.3306,9.86714 104.826,29.5037 23.5444,19.6854 35.3654,42.6925 35.3654,69.0211 0,16.9988 -5.03126,31.3111 -14.8496,43.3275 -9.81829,11.821 -27.159,22.1766 -51.8269,30.8226 -16.8523,6.00821 -55.1485,16.3638 -115.182,31.3599 -77.1297,19.3435 -131.301,42.9855 -162.466,71.1704 -43.8648,39.4685 -65.6995,87.6319 -65.6995,144.49 0,36.4889 10.3556,70.8284 30.8226,102.677 20.7112,31.7995 50.3615,55.9789 89.195,72.6358 38.8335,16.6569 85.4826,25.0098 140.485,25.0098 89.4881,0 156.995,-19.832 202.179,-59.6424 45.3302,-39.8593 69.0211,-92.8585 71.3169,-159.34l-144.979 -5.03126c-6.15475,37.0262 -19.3435,63.5014 -39.5174,79.6698 -20.1739,16.1684 -50.508,24.3259 -90.807,24.3259 -41.6667,0 -74.3454,-8.64596 -97.841,-25.9867 -15.1915,-11.1372 -22.8605,-25.9867 -22.8605,-44.6463 0,-16.9988 7.18054,-31.5064 21.5416,-43.5229 18.1223,-15.4846 62.329,-31.5064 132.474,-48.1633 70.1934,-16.5104 122.02,-33.8023 155.481,-51.485 33.7046,-17.8292 60.0332,-42.0086 79.0348,-72.8312 19.0016,-30.8226 28.4779,-68.8257 28.4779,-114.009 0,-40.9828 -11.3326,-79.5233 -34.1442,-115.328 -22.8605,-35.8538 -55.002,-62.5244 -96.6686,-79.8163 -41.6667,-17.5361 -93.6889,-26.1821 -155.823,-26.1821 -90.5139,0 -160.023,20.8089 -208.529,62.671 -48.5053,41.6667 -77.3251,102.481 -86.8015,182.347z"/>
   <glyph unicode="T" horiz-adv-x="610" d="M233.978 0l0 595.008 -211.997 0 0 120.995 567.995 0 0 -120.995 -210.971 0 0 -595.008 -145.027 0z"/>
   <glyph unicode="U" horiz-adv-x="722" d="M72.9777 716.002l145.027 0 0 -387.993c0,-61.694 1.80735,-101.504 5.32435,-119.676 6.15475,-29.3572 20.6624,-52.9992 43.6694,-70.6819 23.1536,-17.8292 54.66,-26.6706 94.6659,-26.6706 40.4943,0 71.1704,8.35287 91.8327,25.2052 20.6624,16.8034 33.0207,37.3193 37.1727,61.7917 4.15201,24.5213 6.35014,65.0156 6.35014,121.678l0 396.346 144.979 0 0 -376.514c0,-85.9711 -3.81008,-146.835 -11.6745,-182.298 -7.81555,-35.5119 -22.1766,-65.5041 -43.1809,-90.0254 -20.8089,-24.5213 -48.7984,-43.8159 -83.8218,-58.3236 -35.17,-14.5076 -80.8421,-21.8347 -137.309,-21.8347 -68.1907,0 -119.822,7.81555 -154.992,23.642 -35.17,15.8753 -63.0129,36.3423 -83.5287,61.694 -20.467,25.3029 -33.8023,51.6803 -40.3478,79.4744 -9.47636,41.0317 -14.1657,101.7 -14.1657,181.858l0 382.327z"/>
  </font>
  <style type="text/css">
   <![CDATA[
    @font-face { font-family:"Source Sans Pro";font-variant:normal;font-weight:bold;src:url("#FontID1") format(svg)}
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
    .fnt3 {font-weight:bold;font-size:15px;font-family:'Arial'}
    .fnt2 {font-weight:bold;font-size:15px;font-family:'Source Sans Pro'}
    .fnt4 {font-weight:bold;font-size:15px;font-family:'Source Sans Pro'}
    .fnt1 {font-weight:bold;font-size:15.91px;font-family:'Arial'}
    .fnt0 {font-weight:bold;font-size:22.5px;font-family:'Arial'}
   ]]>
  </style>
  <radialGradient id="id0" gradientUnits="userSpaceOnUse" gradientTransform="matrix(1.1 -0 -0 1.1 -22 -22)" cx="217.99" cy="217.99" r="213.87" fx="217.99" fy="217.99">
   <stop offset="0" style="stop-opacity:1; stop-color:#121A24"/>
   <stop offset="0.701961" style="stop-opacity:1; stop-color:#080C12"/>
   <stop offset="1" style="stop-opacity:1; stop-color:#020305"/>
  </radialGradient>
  <radialGradient id="id1" gradientUnits="userSpaceOnUse" gradientTransform="matrix(1 -0 -0 1 0 0)" cx="217.99" cy="217.99" r="82.26" fx="217.99" fy="217.99">
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
    <text x="65.99" y="350.59"  class="fil5 fnt0">-0.5</text>
    <line class="fil1 str5" x1="75.46" y1="217.99" x2="52.95" y2= "217.99"  text-anchor="middle" dominant-baseline="middle"/>
    <text x="24.19" y="217.99"  class="fil5 fnt0">0</text>
    <line class="fil1 str5" x1="117.2" y1="117.2" x2="101.28" y2= "101.28"  text-anchor="middle" dominant-baseline="middle"/>
    <text x="69.73" y="85.38"  class="fil5 fnt0">0.5</text>
    <line class="fil1 str5" x1="217.99" y1="75.46" x2="217.99" y2= "52.95"  text-anchor="middle" dominant-baseline="middle"/>
    <text x="211.73" y="30.45"  class="fil5 fnt0">1</text>
    <line class="fil1 str5" x1="318.77" y1="117.2" x2="334.69" y2= "101.28"  text-anchor="middle" dominant-baseline="middle"/>
    <text x="334.95" y="85.38"  class="fil5 fnt0">1.5</text>
    <line class="fil1 str5" x1="360.52" y1="217.99" x2="383.02" y2= "217.99"  text-anchor="middle" dominant-baseline="middle"/>
    <text x="399.27" y="217.99"  class="fil5 fnt0">2</text>
   </g>
   <circle class="fil6" cx="217.99" cy="217.99" r="75.02" text-anchor="middle" dominant-baseline="middle"/>
   <text x="148.61" y="371.65"  class="fil7 fnt1">T</text>
   <text x="160.4" y="371.65"  class="fil7 fnt1">U</text>
   <text x="174.12" y="371.65"  class="fil7 fnt1">R</text>
   <text x="187.68" y="371.65"  class="fil7 fnt1">B</text>
   <text x="201.24" y="371.65"  class="fil7 fnt1">O</text>
   <text x="215.84" y="371.65"  class="fil7 fnt1"> </text>
   <text x="222.33" y="371.65"  class="fil7 fnt1">B</text>
   <text x="235.89" y="371.65"  class="fil7 fnt1">O</text>
   <text x="250.49" y="371.65"  class="fil7 fnt1">O</text>
   <text x="264.94" y="371.65"  class="fil7 fnt1">S</text>
   <text x="277.62" y="371.65"  class="fil7 fnt1">T</text>
   <text x="167.47" y="393.8"  class="fil8 fnt2">◀</text>
   <text x="182.43" y="393.8"  class="fil8 fnt3"> </text>
   <text x="188.55" y="393.8"  class="fil8 fnt3">B</text>
   <text x="201.18" y="393.8"  class="fil8 fnt3">A</text>
   <text x="212.85" y="393.8"  class="fil8 fnt3">T</text>
   <text x="222.99" y="393.8"  class="fil8 fnt3">-</text>
   <text x="229.94" y="393.8"  class="fil8 fnt3">T</text>
   <text x="240.9" y="393.8"  class="fil8 fnt3">E</text>
   <text x="252.86" y="393.8"  class="fil8 fnt3">C</text>
   <text x="265.49" y="393.8"  class="fil8 fnt3">H</text>
   <text x="166.75" y="416.62"  class="fil8 fnt3">B</text>
   <text x="179.38" y="416.62"  class="fil8 fnt3">O</text>
   <text x="193" y="416.62"  class="fil8 fnt3">O</text>
   <text x="206.47" y="416.62"  class="fil8 fnt3">S</text>
   <text x="218.43" y="416.62"  class="fil8 fnt3">T</text>
   <text x="229.4" y="416.62"  class="fil8 fnt3"> </text>
   <text x="273.57" y="416.62"  class="fil8 fnt3"> </text>
   <text x="237.32" y="416.62"  class="fil8 fnt3">BAR</text>
   <text x="279.69" y="416.62"  class="fil8 fnt4">▶</text>
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
