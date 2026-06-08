# OrbitalGuard — Estação Terrestre de Monitoramento Climático (IoT)

**Global Solution FIAP 2026/1 — Disruptive Architectures: IoT, IOB & Generative IA**
Tema: Economia Espacial · Turma: **2TDSPW**

Repositório: https://github.com/joaolacerdaconsorte/orbitalguard-iot
Simulação Wokwi: `[INSERIR LINK DO PROJETO WOKWI]`

---

## 1. Descrição do Projeto

O **OrbitalGuard** é uma plataforma de monitoramento climático e prevenção de
desastres que combina dados de **satélite**, **IA** e **sensores terrestres (IoT)**.
Este módulo implementa a **Estação Terrestre de Monitoramento Climático**: um
dispositivo de campo, instalado em uma região de risco, que mede continuamente as
condições ambientais, classifica o **risco climático localmente** (edge computing)
e envia a telemetria em tempo real para a nuvem da plataforma via **MQTT**.

Cada estação está vinculada a uma **região** do domínio OrbitalGuard (a mesma
entidade `Regiao` das APIs Java e .NET e do banco Oracle), fechando o ciclo
**sensor → nuvem → satélite → IA → alerta ao cidadão**.

---

## 2. Arquitetura de Hardware e Conexões

### Componentes Utilizados
* **ESP32 DevKit v4** — microcontrolador com Wi-Fi/Bluetooth (processamento de borda).
* **Sensor DHT22** — temperatura e umidade do ar *(entrada 1)*.
* **Sensor BMP280** — pressão atmosférica e temperatura, via I²C *(entrada 2)*.
* **Display LCD 16×2 I²C** — exibição local das leituras e do nível de risco.
* **3 LEDs (verde / amarelo / vermelho)** — sinalização visual do risco *(saída 1)*.
* **Buzzer** — alarme sonoro em situação crítica *(saída 2)*.
* **3 resistores de 1 kΩ** — limitadores de corrente dos LEDs.

### Esquema de Conexão (Pinagem)
| Componente | Pino do componente | Pino do ESP32 |
|---|---|---|
| DHT22 | VCC / GND / SDA | 3V3 / GND / **GPIO 4** |
| BMP280 (I²C 0x76) | VCC / GND / SDA / SCL | 3V3 / GND / **GPIO 21** / **GPIO 22** |
| LCD I²C (0x27) | VCC / GND / SDA / SCL | 3V3 / GND / **GPIO 21** / **GPIO 22** |
| LED Verde | Anodo → R 1kΩ / Catodo | **GPIO 25** / GND |
| LED Amarelo | Anodo → R 1kΩ / Catodo | **GPIO 26** / GND |
| LED Vermelho | Anodo → R 1kΩ / Catodo | **GPIO 27** / GND |
| Buzzer | + / − | **GPIO 14** / GND |

> Os sensores **BMP280** e o **display LCD** compartilham o mesmo barramento **I²C**
> (SDA = GPIO 21, SCL = GPIO 22), em endereços distintos (0x76 e 0x27).

---

## 3. Lógica de Classificação de Risco (Edge Computing)

A estação faz uma leitura a cada **5 segundos** e classifica o risco combinando
**três fatores ambientais**. Cada fator ultrapassado conta como um ponto:

| Fator | Condição de risco | Significado |
|---|---|---|
| Temperatura | `≥ 38,0 °C` | calor extremo / onda de calor |
| Umidade | `≤ 30 %` | ar muito seco → risco de queimada |
| Pressão | `≤ 1000 hPa` | queda de pressão → tempestade / ciclone |

Resultado:

1. **NORMAL** (0 fatores) → LED **verde**, buzzer desligado, status `"NORMAL"`.
2. **ALERTA** (1 fator) → LED **amarelo**, buzzer desligado, publica também no tópico de alerta.
3. **CRÍTICO** (2+ fatores) → LED **vermelho** + **buzzer acionado**, publica alerta crítico.

A mesma leitura é exibida no **LCD** (linha 1: temperatura e umidade; linha 2:
pressão e nível de risco).

---

## 4. Comunicação MQTT e Nuvem

O dispositivo conecta-se ao Wi-Fi (`Wokwi-GUEST`) e ao broker público
**HiveMQ** (`broker.hivemq.com`, porta `1883`). São utilizados **3 tópicos**:

| Tópico | Quando publica | Conteúdo |
|---|---|---|
| `orbitalguard/estacao/telemetria` | a cada 5 s | leitura completa dos sensores + risco |
| `orbitalguard/estacao/alerta` | quando risco ≠ NORMAL | nível, fatores e causas do alerta |
| `orbitalguard/estacao/status` | ao conectar (retida) | heartbeat `ONLINE` da estação |

### Exemplo — `orbitalguard/estacao/telemetria`
```json
{
  "estacao": "EST-SP-001",
  "regiao_id": 1,
  "regiao": "Litoral Norte-SP",
  "temperatura": 39.2,
  "umidade": 24.0,
  "pressao_hpa": 998.0,
  "risco": "CRITICO",
  "timestamp": 125000
}
```

### Exemplo — `orbitalguard/estacao/alerta`
```json
{
  "estacao": "EST-SP-001",
  "regiao_id": 1,
  "regiao": "Litoral Norte-SP",
  "nivel": "CRITICO",
  "fatores": 3,
  "causa": "calor_extremo;ar_seco;queda_pressao;",
  "timestamp": 125000
}
```

Esse payload é consumido por **dois assinantes**: a **API OrbitalGuard** (que persiste o
alerta na entidade `Alerta` ligada à `Regiao` e dispara as notificações ao cidadão) e a
**dashboard web** desta entrega, que **exibe os dados em tempo real** (ver seção 5).

---

## 5. Dashboard de Visualização (consumo e apresentação dos dados)

O diretório [`dashboard/`](dashboard/index.html) contém uma **interface web** que **assina os
mesmos tópicos MQTT** da estação (via WebSocket) e **apresenta os dados recebidos em tempo
real** — fechando o ciclo *sensor → MQTT → visualização*:

- **Cartões** de temperatura, umidade e pressão atualizados a cada leitura;
- **Banner de risco** colorido (NORMAL / ALERTA / CRÍTICO);
- **Gráfico** de evolução da temperatura (últimas leituras);
- **Registro** dos tópicos `/alerta` e `/status` recebidos;
- **Indicador de conexão** com o broker e botão **"Simular leitura"** para demonstrar a
  apresentação mesmo sem o dispositivo no ar.

Tecnicamente é um `index.html` autossuficiente que usa **MQTT.js** (WebSocket) e **Chart.js**
(via CDN), conectando-se a `ws://broker.hivemq.com:8000/mqtt` e assinando
`orbitalguard/estacao/#`. **Basta abrir o arquivo no navegador** — nenhuma instalação.

## 6. Como Executar (fluxo completo)

1. Acesse **https://wokwi.com**, crie um projeto **ESP32** e cole [`sketch.ino`](sketch.ino)
   e [`diagram.json`](diagram.json); confirme as bibliotecas de [`libraries.txt`](libraries.txt).
2. Clique em **Start the simulation** e abra o **Serial Monitor** para ver a telemetria;
   ajuste os valores do **DHT22**/**BMP280** para forçar os estados **ALERTA** e **CRÍTICO**
   (observe LEDs, buzzer e LCD).
3. Abra [`dashboard/index.html`](dashboard/index.html) no navegador: ela conecta ao broker e
   passa a **exibir, em tempo real, os dados que a estação publica** — temperatura, umidade,
   pressão, nível de risco, gráfico e log de alertas.
4. Sem o Wokwi no ar, use **"Simular leitura"** na dashboard para demonstrar a apresentação.

---

## 7. Arquivos do Projeto

* **Código-fonte (firmware):** [`sketch.ino`](sketch.ino)
* **Conexões do hardware:** [`diagram.json`](diagram.json)
* **Bibliotecas:** [`libraries.txt`](libraries.txt)
* **Projeto Wokwi:** [`wokwi-project.txt`](wokwi-project.txt)
* **Dashboard de visualização:** [`dashboard/index.html`](dashboard/index.html)

---

## 8. Equipe (Turma: 2TDSPW)

| Nome | RM |
|---|---|
| João Vitor Lacerda | 565565 |
| Kauan Vieira de Lima | 565403 |
| Murillo Fernandes Carapia | 564969 |
