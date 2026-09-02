/*
  ==========================================================
  NODE A - SERVIDOR OTIMIZADO PARA MÚLTIPLOS CLIENTES
  ==========================================================
*/

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WebSocketsServer.h>
#include <ArduinoJson.h>
#include <max6675.h>
#include <SoftwareSerial.h>
#include <DFRobotDFPlayerMini.h>

const char* AP_SSID = "ESP32_Sensores"; 
const char* AP_PASS = "12345678";       

int numAudios = 0;

#define PINO_MAX6675_SCK  0    // D3
#define PINO_MAX6675_CS   2    // D4
#define PINO_MAX6675_SO   14   // D5
#define PINO_PRESSAO_ADC  A0   
#define PINO_DFPLAYER_RX  5    // D1
#define PINO_DFPLAYER_TX  4    // D2

ESP8266WebServer server(80);
WebSocketsServer webSocket(81);
MAX6675 termopar(PINO_MAX6675_SCK, PINO_MAX6675_CS, PINO_MAX6675_SO);
SoftwareSerial serialDFPlayer(PINO_DFPLAYER_RX, PINO_DFPLAYER_TX);
DFRobotDFPlayerMini dfPlayer;

float ultimaTemperaturaValida = 0;
float temperaturaFiltrada = NAN; 
const float FATOR_FILTRO_TEMPERATURA = 0.15;

float offsetPressaoBar = 0.0;
const float TENSAO_REF_PLACA = 3.2;
const float FATOR_DIVISOR_EXTERNO = 25.0 / 15.0;
const float TENSAO_SENSOR_MIN = 1.0;
const float TENSAO_SENSOR_MAX = 5.0;
const float PRESSAO_MIN_BAR   = 0.0;
const float PRESSAO_MAX_BAR   = 10.0;

unsigned long ultimoEnvio = 0;
const unsigned long INTERVALO_ENVIO = 500;

const float GANHO_RESIDUAL_PRESSAO  = 1.25;
const float OFFSET_RESIDUAL_PRESSAO = -0.1;

// HTML armazenado em PROGMEM (Flash)
const char HTML_WEB_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="pt-BR">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Node B - Cliente Web</title>
  <style>
    * { box-sizing: border-box; margin: 0; padding: 0; font-family: sans-serif; user-select: none; }
    body { background-color: #121212; color: #fff; display: flex; justify-content: center; align-items: center; min-height: 100vh; padding: 15px; }
    .nextion-screen { width: 100%; max-width: 600px; background: #1e1e1e; border: 2px solid #333; border-radius: 12px; padding: 16px; }
    .page { display: none; flex-direction: column; align-items: center; width: 100%; }
    .page.active { display: flex; }
    #page0 h2 { margin-bottom: 15px; color: #00e676; font-size: 1.2rem; }
    .status-card { background: #2a2a2a; padding: 15px; border-radius: 8px; border-left: 4px solid #00e676; font-size: 1rem; }
    .gauges-container { display: flex; justify-content: space-around; width: 100%; flex-wrap: wrap; gap: 10px; margin-bottom: 20px; }
    .gauge-card { background: #282828; border-radius: 8px; padding: 10px; display: flex; flex-direction: column; align-items: center; width: 140px; }
    .gauge-title { font-size: 0.9rem; color: #aaa; }
    .gauge-value { font-size: 1.2rem; font-weight: bold; color: #00bcd4; margin-top: 5px; }
    .status-display { width: 100%; background: #000; color: #ffeb3b; font-family: monospace; font-size: 1.1rem; text-align: center; padding: 12px; border-radius: 6px; border: 1px solid #444; margin-bottom: 15px; min-height: 50px; display: flex; align-items: center; justify-content: center; }
    .encoder-controls { display: flex; align-items: center; justify-content: center; gap: 10px; width: 100%; }
    .btn { background: #333; color: #fff; border: none; padding: 12px 18px; font-size: 1rem; font-weight: bold; border-radius: 6px; cursor: pointer; }
    .btn-action { background: #00c853; color: #000; }
  </style>
</head>
<body>
  <div class="nextion-screen">
    <div id="page0" class="page active">
      <h2>Conectando...</h2>
      <div class="status-card"><span id="staConexao">Aguardando Node A...</span></div>
    </div>
    <div id="page1" class="page">
      <div class="gauges-container">
        <div class="gauge-card">
          <div class="gauge-title">Temp (°C)</div>
          <canvas id="gaugeTemp" width="120" height="120"></canvas>
          <div class="gauge-value"><span id="txtTemp">--</span> °C</div>
        </div>
        <div class="gauge-card">
          <div class="gauge-title">Pressão (bar)</div>
          <canvas id="gaugePressure" width="120" height="120"></canvas>
          <div class="gauge-value"><span id="txtBar">--</span> bar</div>
        </div>
      </div>
      <div id="nomeSom" class="status-display">Aguardando...</div>
      <div class="encoder-controls">
        <button class="btn" onclick="girarEncoder(-1)">&#9664;</button>
        <button class="btn btn-action" onclick="pressionarBotaoEncoder()">TOCAR</button>
        <button class="btn" onclick="girarEncoder(1)">&#9654;</button>
      </div>
    </div>
  </div>
  <script>
    const WS_URL = `ws://${window.location.hostname}:81/`;
    let ws = null, wsConectado = false, telaPrincipalAtiva = false;
    let numAudiosDisponiveis = 0, somSelecionado = 0;
    let temperaturaAtual = 0, pressaoAtual = 0, dadosRecebidos = false;
    let mensagemStatus = "", statusTimestamp = 0; const STATUS_DURACAO = 1500;
    let audioCtx = null;

    function tocarTom(freq) {
      try {
        if (!audioCtx) audioCtx = new (window.AudioContext || window.webkitAudioContext)();
        if (audioCtx.state === 'suspended') audioCtx.resume();
        const osc = audioCtx.createOscillator(), gain = audioCtx.createGain();
        osc.type = 'square'; osc.frequency.setValueAtTime(freq, audioCtx.currentTime);
        gain.gain.setValueAtTime(0.1, audioCtx.currentTime);
        gain.gain.exponentialRampToValueAtTime(0.00001, audioCtx.currentTime + 0.1);
        osc.connect(gain); gain.connect(audioCtx.destination);
        osc.start(); osc.stop(audioCtx.currentTime + 0.1);
      } catch (e) {}
    }

    function conectarWebSocket() {
      ws = new WebSocket(WS_URL);
      ws.onopen = () => { wsConectado = true; atualizarTela(); };
      ws.onclose = () => { wsConectado = false; atualizarTela(); setTimeout(conectarWebSocket, 3000); };
      ws.onmessage = (event) => {
        try {
          const doc = JSON.parse(event.data);
          if (doc.tentativa !== undefined) { mensagemStatus = "Tentativa " + doc.tentativa; statusTimestamp = Date.now(); atualizarNomeSom(); }
          if (doc.erro !== undefined) { mensagemStatus = String(doc.erro); statusTimestamp = Date.now(); atualizarNomeSom(); }
          if (doc.num_audios !== undefined) {
            let novo = parseInt(doc.num_audios);
            if (novo !== numAudiosDisponiveis) { numAudiosDisponiveis = novo; if (somSelecionado >= numAudiosDisponiveis) somSelecionado = 0; atualizarNomeSom(); }
          }
          if (doc.temp !== undefined || doc.press !== undefined) {
            temperaturaAtual = parseFloat(doc.temp || 0.0); pressaoAtual = parseFloat(doc.press || 0.0);
            dadosRecebidos = true; atualizarSensores();
          }
        } catch (e) {}
      };
    }

    function girarEncoder(dir) {
      if (numAudiosDisponiveis <= 0) return;
      somSelecionado += dir;
      if (somSelecionado >= numAudiosDisponiveis) somSelecionado = 0;
      if (somSelecionado < 0) somSelecionado = numAudiosDisponiveis - 1;
      tocarTom(dir > 0 ? 2300 : 2000); atualizarNomeSom();
    }

    function pressionarBotaoEncoder() {
      if (!wsConectado || numAudiosDisponiveis <= 0) return;
      ws.send(JSON.stringify({ event: "som" + (somSelecionado + 1) }));
      mensagemStatus = "Som " + (somSelecionado + 1) + " enviado"; statusTimestamp = Date.now(); atualizarNomeSom();
    }

    function atualizarNomeSom() {
      const el = document.getElementById("nomeSom");
      if (mensagemStatus !== "" && (Date.now() - statusTimestamp) < STATUS_DURACAO) el.innerText = mensagemStatus;
      else if (!wsConectado) el.innerText = "Node A offline...";
      else if (numAudiosDisponiveis > 0) el.innerText = `Som ${somSelecionado + 1} de ${numAudiosDisponiveis}`;
      else el.innerText = "Aguardando lista...";
    }

    function atualizarSensores() {
      if (dadosRecebidos) {
        document.getElementById("txtTemp").innerText = Math.round(temperaturaAtual);
        document.getElementById("txtBar").innerText = pressaoAtual.toFixed(1);
        desenharGauge("gaugeTemp", temperaturaAtual, -30, 180, 330, 180);
        desenharGauge("gaugePressure", pressaoAtual, -0.5, 2.0, 315, 180);
      }
    }

    function atualizarTela() {
      if (!telaPrincipalAtiva) {
        if (wsConectado) {
          telaPrincipalAtiva = true;
          document.getElementById("page0").classList.remove("active");
          document.getElementById("page1").classList.add("active");
          atualizarSensores(); atualizarNomeSom();
        }
      } else { atualizarSensores(); atualizarNomeSom(); }
    }

    function calcularAnguloGauge(val, vMin, vMax, aIni, aFin) {
      if (val < vMin) val = vMin; if (val > vMax) val = vMax;
      let prop = (val - vMin) / (vMax - vMin), sweep = aFin - aIni;
      if (sweep <= 0) sweep += 360;
      return (aIni + prop * sweep) % 360;
    }

    function desenharGauge(id, val, vMin, vMax, aIni, aFin) {
      const cv = document.getElementById(id); if (!cv) return;
      const ctx = cv.getContext("2d"), cx = cv.width / 2, cy = cv.height / 2, r = 45;
      ctx.clearRect(0, 0, cv.width, cv.height);
      ctx.beginPath(); ctx.arc(cx, cy, r + 5, 0, 2 * Math.PI); ctx.fillStyle = "#1a1a1a"; ctx.fill();
      ctx.strokeStyle = "#444"; ctx.lineWidth = 2; ctx.stroke();
      let angRad = (calcularAnguloGauge(val, vMin, vMax, aIni, aFin) * Math.PI) / 180;
      ctx.beginPath(); ctx.moveTo(cx, cy); ctx.lineTo(cx + r * Math.cos(angRad), cy + r * Math.sin(angRad));
      ctx.strokeStyle = "#ff3d00"; ctx.lineWidth = 3; ctx.stroke();
      ctx.beginPath(); ctx.arc(cx, cy, 4, 0, 2 * Math.PI); ctx.fillStyle = "#fff"; ctx.fill();
    }

    setInterval(() => { if (mensagemStatus !== "" && (Date.now() - statusTimestamp) >= STATUS_DURACAO) { mensagemStatus = ""; atualizarNomeSom(); } }, 300);
    window.addEventListener("load", conectarWebSocket);
  </script>
</body>
</html>
)rawliteral";

void tocarAudio(int numero) {
  dfPlayer.playFolder(1, numero);
  Serial.printf("[DFPLAYER] Tocando som%d.\n", numero);
}

float lerTemperatura() {
  float leitura = termopar.readCelsius();
  if (isnan(leitura)) return ultimaTemperaturaValida;
  ultimaTemperaturaValida = leitura;
  if (isnan(temperaturaFiltrada)) temperaturaFiltrada = leitura;
  else temperaturaFiltrada += FATOR_FILTRO_TEMPERATURA * (leitura - temperaturaFiltrada);
  return temperaturaFiltrada;
}

float lerPressaoBruta() {
  const int NUM_AMOSTRAS = 10;
  long soma = 0;
  for (int i = 0; i < NUM_AMOSTRAS; i++) {
    soma += analogRead(PINO_PRESSAO_ADC);
    delay(2);
  }
  float leituraADC = soma / (float)NUM_AMOSTRAS;
  float tensaoNoA0 = (leituraADC / 1023.0) * TENSAO_REF_PLACA;
  float tensaoSensor = tensaoNoA0 * FATOR_DIVISOR_EXTERNO;
  return (tensaoSensor - TENSAO_SENSOR_MIN) / (TENSAO_SENSOR_MAX - TENSAO_SENSOR_MIN) * (PRESSAO_MAX_BAR - PRESSAO_MIN_BAR);
}

float lerPressao() { return lerPressaoBruta() - offsetPressaoBar; }

float lerPressaoCorrigida() {
  float pressaoTarada = lerPressao();
  return (pressaoTarada - OFFSET_RESIDUAL_PRESSAO) / GANHO_RESIDUAL_PRESSAO;
}

void calibrarZeroPressao() {
  const int AMOSTRAS_CALIBRAGEM = 20;
  float soma = 0;
  for (int i = 0; i < AMOSTRAS_CALIBRAGEM; i++) {
    soma += lerPressaoBruta();
    delay(50);
  }
  offsetPressaoBar = soma / AMOSTRAS_CALIBRAGEM;
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
}

void enviarQuantidadeAudios(uint8_t num) {
  JsonDocument doc;
  doc["num_audios"] = numAudios;
  String saida;
  serializeJson(doc, saida);
  webSocket.sendTXT(num, saida);
}

void lerQuantidadeAudios(uint8_t num) {
    if (numAudios > 0) return;
    delay(200);

    const int MAX_TENTATIVAS_LEITURA_SD = 15;
    for (int tentativa = 1; tentativa <= MAX_TENTATIVAS_LEITURA_SD; tentativa++) {
      numAudios = dfPlayer.readFileCountsInFolder(1);
      if (numAudios > 0) break;

      JsonDocument doc;
      doc["tentativa"] = tentativa;
      String saida;
      serializeJson(doc, saida);
      webSocket.sendTXT(num, saida);
      delay(500);
    }

    if (numAudios <= 0) {
      numAudios = 0;
      JsonDocument doc;
      doc["erro"] = "Falha ao ler SD";
      String saida;
      serializeJson(doc, saida);
      webSocket.sendTXT(num, saida);
    }
}

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
      JsonDocument doc;
      DeserializationError err = deserializeJson(doc, msg);
      if (!err) {
        const char* evento = doc["event"];
        if (evento != nullptr) {
          String ev = String(evento);
          if (ev.startsWith("som")) {
            int numero = ev.substring(3).toInt();
            if (numero >= 1 && numero <= numAudios) {
              tocarAudio(numero);
            }
          }
        }
      }
      break;
    }
    default: break;
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);
  calibrarZeroPressao();

  serialDFPlayer.begin(9600);
  delay(1000);
  if (dfPlayer.begin(serialDFPlayer, false, true)) {
    dfPlayer.volume(20);
  }

  WiFi.softAP(AP_SSID, AP_PASS);

  // OTIMIZAÇÃO: Servidor HTTP usa envio P_PROGMEM para economizar RAM 
  // e adiciona os cabeçalhos de controle de conexao
  server.on("/", []() {
    server.sendHeader("Connection", "close");
    server.send_P(200, "text/html", HTML_WEB_PAGE);
  });
  
  server.begin();

  webSocket.begin();
  webSocket.onEvent(onWebSocketEvent);
}

void loop() {
  server.handleClient(); 
  webSocket.loop();       

  if (millis() - ultimoEnvio >= INTERVALO_ENVIO) {
    ultimoEnvio = millis();
    enviarDadosSensores();
  }
}