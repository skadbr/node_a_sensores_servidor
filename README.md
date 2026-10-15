# Node A - Servidor de sensores

Firmware para **Wemos D1 mini / ESP8266** que lê sensores de temperatura e pressão, cria uma rede Wi-Fi própria e serve um painel web. Também recebe comandos por WebSocket para reproduzir áudios em um DFPlayer Mini.

## Funcionalidades

- Leitura de termopar tipo K por módulo MAX6675, com filtro para suavizar oscilações.
- Leitura do sensor de pressão PS10 (0 a 10 bar), com média de amostras, tara automática no boot e ajuste de calibração.
- Access Point Wi-Fi próprio para conectar o Node B e/ou um navegador simultaneamente.
- Envio de leituras por WebSocket ao detectar mudança relevante ou a cada 2 segundos como heartbeat.
- Reprodução de arquivos MP3 por comandos recebidos via WebSocket.
- Detecção da quantidade de áudios na pasta `01` do cartão microSD, com tentativas não bloqueantes e cache durante a execução.
- Painel web com indicadores de temperatura e pressão, seleção de áudio e estado da conexão.

## Hardware

- Wemos D1 mini com ESP8266
- Módulo MAX6675 e termopar tipo K
- Sensor de pressão PS10 com saída analógica de 1 a 5 V (0 a 10 bar)
- Dois resistores para o divisor de tensão do PS10: 10 kΩ e 15 kΩ
- DFPlayer Mini, cartão microSD e alto-falante compatível
- Fonte de alimentação adequada aos módulos

### Ligações

Os números abaixo são os GPIOs reais usados pelo código. Os rótulos D correspondentes são os da placa Wemos D1 mini.

| Módulo / sinal | Wemos D1 mini | Observação |
| --- | --- | --- |
| MAX6675 SCK | GPIO0 (D3) | |
| MAX6675 CS | GPIO2 (D4) | |
| MAX6675 SO | GPIO14 (D5) | |
| MAX6675 VCC / GND | 3V3 / GND | GND comum |
| PS10 VCC / GND | 5V / GND | Pode usar fonte externa; compartilhar o GND com o Wemos |
| PS10 sinal | A0, através do divisor | **Nunca ligue o sinal de 1 a 5 V diretamente ao A0** |
| DFPlayer RX | GPIO4 (D2) | Recebe do TX do Wemos; recomenda-se resistor de 1 kΩ em série |
| DFPlayer TX | GPIO5 (D1) | Envia ao RX do Wemos |
| DFPlayer VCC / GND | 5V / GND | GND comum |
| Alto-falante | SPK_1 / SPK_2 do DFPlayer | |

Para o divisor do sinal do PS10, conecte o sinal do sensor a um resistor de **10 kΩ**, o outro lado desse resistor ao ponto A0, e um resistor de **15 kΩ** entre A0 e GND. Essa divisão reduz 5 V para aproximadamente 3 V. O A0 do Wemos D1 mini aceita até cerca de 3,2 V; exceder esse limite pode danificar a placa.

O ESP8266 tem uma UART de hardware usada pelo Serial Monitor e gravação. Por isso, o DFPlayer usa `SoftwareSerial`.

## Preparação e gravação

1. Instale o Arduino IDE e o suporte a placas ESP8266. Se necessário, adicione esta URL em **Preferências > URLs adicionais para Gerenciadores de Placas**:
   `http://arduino.esp8266.com/stable/package_esp8266com_index.json`
2. Instale as bibliotecas `WebSockets` (Links2004), `ArduinoJson`, `MAX6675` e `DFRobotDFPlayerMini` pelo Gerenciador de Bibliotecas. `ESP8266WiFi`, `ESP8266WebServer` e `SoftwareSerial` acompanham o core ESP8266.
3. Abra `node_a_sensores_servidor.ino` no Arduino IDE e selecione a placa ESP8266 correspondente à sua instalação, por exemplo **LOLIN(WEMOS) D1 R1**.
4. Compile e grave o firmware. Abra o Serial Monitor a **115200 baud** para acompanhar inicialização, leituras e mensagens de diagnóstico.

## Wi-Fi e painel

Ao iniciar, o Node A cria uma rede Wi-Fi:

- **SSID:** `ESP32_Sensores`
- **Senha:** `12345678`
- **Endereço do painel:** [http://192.168.4.1/](http://192.168.4.1/)
- **WebSocket:** `ws://192.168.4.1:81/`

Conecte o Node B ou o navegador à rede do Node A. O servidor HTTP do painel usa a porta 80 e o WebSocket usa a porta 81. Ambos podem permanecer conectados ao mesmo tempo.

> A senha está definida no firmware e é adequada apenas para uma rede de teste/local. Altere `AP_SSID` e `AP_PASS` no código se precisar de outras credenciais; a senha do Access Point deve ter pelo menos 8 caracteres.

## Cartão microSD e áudios

1. Formate o cartão como FAT32.
2. Crie na raiz uma pasta chamada `01`.
3. Coloque os arquivos dentro da pasta com nomes sequenciais de três dígitos: `001.mp3`, `002.mp3`, `003.mp3` e assim por diante, sem saltos.
4. Reinicie o Node A após alterar a lista de arquivos.

O comando `som1` reproduz `01/001.mp3`, `som2` reproduz `01/002.mp3`, e assim sucessivamente. A contagem é consultada na primeira conexão de cliente após o boot. As tentativas são feitas sem bloquear o loop principal, e a quantidade encontrada é reutilizada até o próximo reinício.

## Protocolo WebSocket

As mensagens são JSON em texto. Ao conectar, o cliente recebe a quantidade de áudios e, quando já houver leitura disponível, a última temperatura e pressão.

Exemplo de leituras enviadas pelo Node A:

```json
{"temp":25.4,"press":0.8,"heartbeat":false}
```

`heartbeat` será `true` quando a mensagem for enviada apenas para manter a atualização periódica, sem mudança relevante nos valores exibidos. A temperatura é enviada em °C e a pressão em bar.

Exemplo de comando para reproduzir o primeiro áudio:

```json
{"event":"som1"}
```

Outras mensagens relacionadas à leitura do cartão podem incluir `num_audios`, `tentativa` ou `erro`.

## Calibração e inicialização

- **Tara da pressão:** no boot, o firmware calcula a referência de zero usando 20 leituras. Ligue o Node A com o sensor sem pressão injetada; caso contrário, essa pressão será tomada como zero.
- **Calibração elétrica:** `TENSAO_REF_PLACA` começa em 3,2 V e pode variar entre placas. Compare a tensão calculada no Serial Monitor com a tensão medida no fio de sinal do PS10 e ajuste essa constante se necessário.
- **Ajuste residual:** `GANHO_RESIDUAL_PRESSAO` e `OFFSET_RESIDUAL_PRESSAO` aplicam a correção fina definida no código. Recalibre esses valores se os testes com seu sensor indicarem diferença.
- **DFPlayer:** o volume configurado na inicialização é 20, numa escala de 0 a 30. Se o módulo não for detectado, confira alimentação, GND comum, comunicação serial e cartão SD.

## Parâmetros principais no firmware

- `AP_SSID` e `AP_PASS`: credenciais do Access Point.
- `INTERVALO_ENVIO`: intervalo de leitura dos sensores, atualmente 500 ms.
- `INTERVALO_HEARTBEAT`: intervalo máximo sem envio, atualmente 2000 ms.
- `VARIACAO_TEMP_ENVIO` e `VARIACAO_PRESSAO_ENVIO`: limites de mudança que provocam envio.
- `FATOR_FILTRO_TEMPERATURA`: suavização da leitura do MAX6675.
- `TENSAO_REF_PLACA`, `GANHO_RESIDUAL_PRESSAO` e `OFFSET_RESIDUAL_PRESSAO`: calibração do PS10.
