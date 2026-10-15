  ==========================================================
  NODE A - SERVIDOR WEBSOCKET (VERSAO COMPLETA - SENSORES + AUDIO + WEB)
  ==========================================================
  Wemos D1 Mini (ESP8266)

  Funcao:
   - Cria uma rede WiFi propria (modo Access Point)
   - Le sensor de temperatura (termopar tipo K via modulo MAX6675) e
     sensor de pressao (PS10, saida analogica 1-5V, 0-10bar), enviando
    le os sensores a cada 500ms e transmite por mudanca relevante
    ou heartbeat periodico
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
