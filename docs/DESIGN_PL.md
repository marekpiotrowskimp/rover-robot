# ROVER - Autonomiczny Robot Asystent Domowy

> Robot konwersacyjny oparty na LEGO Mindstorms NXT 1.0 (gąsienice) + Seeed Xiao ESP32-S3 Sense
> z kamerą OV3660, sterowaniem przez Bluetooth, agencją AI (Claude 3.5 Haiku) i trwałą pamięcią.

---

## Spis treści

1. [Opis projektu](#opis-projektu)
2. [Komponenty Hardware](#komponenty-hardware)
3. [Architektura systemu](#architektura-systemu)
4. [Warstwy architektury](#warstwy-architektury)
5. [Schemat komunikacji i przepływ danych](#schemat-komunikacji-i-przepływ-danych)
6. [Protokół Bluetooth NXT](#protokół-bluetooth-nxt)
7. [Struktura projektu](#struktura-projektu)
8. [Backend Python - FastAPI](#backend-python---fastapi)
9. [ESP32 Firmware](#esp32-firmware)
10. [Agent AI - Claude tool_use](#agent-ai---claude-tooluse)
11. [Trwała pamięć - SQLite](#trwała-pamięć---sqlite)
12. [Web UI](#web-ui)
13. [System Prompt agenta](#system-prompt-agenta)
14. [Plan implementacji](#plan-implementacji)
15. [Konfiguracja](#konfiguracja)
16. [Szacowane koszty API](#szacowane-koszty-api)
17. [Roadmap](#roadmap)

---

## Opis projektu

**ROVER** to autonomiczny robot domowy zbudowany na bazie zestawu LEGO Mindstorms NXT 1.0
(podwozie gąsienicowe) rozszerzonego o moduł Seeed Xiao ESP32-S3 Sense jako główny kontroler AI.

Robot potrafi:
- **Rozmawiać po polsku** - rozumie mowę przez mikrofon PDM, odpowiada przez głośnik I2S
- **Widzieć** - kamera OV3660 dostarcza obraz do analizy przez AI (vision)
- **Jeździć autonomicznie** - silniki LEGO NXT sterowane przez Bluetooth Classic SPP
- **Wyznaczać sobie zadania** - agent planuje podzadania i realizuje je krok po kroku
- **Pamiętać** - trwała pamięć SQLite przechowuje historię rozmów, fakty o środowisku i zadania
- **Być obserwowany** - interfejs webowy z podglądem kamery na żywo i logami agenta

### Filozofia architektury

```
ESP32-S3 = zmysły + ruch (sprzęt)
PC Backend = inteligencja + pamięć (software)
```

ESP32 jest celowo cienką warstwą - zbiera dane ze świata fizycznego (audio, obraz)
i wykonuje komendy fizyczne (ruch przez BT, odtwarzanie dźwięku). Cała logika agentowa,
przetwarzanie mowy i pamięć działają na PC w sieci lokalnej. Dzięki temu nie ogranicza
nas 8 MB RAM modułu, a wymiana modelu AI sprowadza się do zmiany jednej linii konfiguracji.

---

## Komponenty Hardware

### Posiadane

| Komponent | Model | Specyfikacja | Rola w projekcie |
|---|---|---|---|
| Mikrokontroler + kamera | Seeed Xiao ESP32-S3 Sense (SEE-22926) | ESP32-S3 240MHz, 8MB PSRAM, 8MB Flash, WiFi 802.11n, BT 5.0, kamera OV3660 2Mpx, mikrofon PDM | Mózg sprzętowy robota |
| Radiator | Zestaw radiatorów Xiao ESP32-S3 (SEE-25684) | 2x aluminium + taśma termoprzewodząca | Chłodzenie przy intensywnym WiFi/kamera |
| Podwozie + silniki | LEGO Mindstorms NXT 1.0 | Cegła NXT (BT 2.0 CSR), 2x silnik NXT 9V z enkoderami, gąsienice Technic | Napęd różnicowy, szkielet mechaniczny |

### Do dokupienia (botland.com.pl)

| Produkt | Indeks | Cena | Rola |
|---|---|---|---|
| Audio BFF Add-on - wzmacniacz I2S do XIAO - Adafruit 5769 | ADA-23827 | ~36 zł | Odtwarzanie głosu (mówienie), montuje się na pinach XIAO back-to-back |
| Głośnik 4Ω 3W mini (np. 40mm) | dowolny sklep | ~10 zł | Przetwornik dźwięku dla wzmacniacza I2S |

**Łączny koszt dodatkowego hardware: ~46 zł**

### Dlaczego Audio BFF Add-on a nie zewnętrzny DAC?

Moduł Adafruit 5769 jest zaprojektowany specjalnie pod footprint XIAO - montuje się
bezpośrednio na tylnych pinach bez dodatkowych przewodów. Zawiera MAX98357A
(wzmacniacz klasy D, 3W, I2S), który ESP32-S3 obsługuje natywnie przez bibliotekę `ESP32-AudioI2S`.

---

## Architektura systemu

```
┌─────────────────────────────────────────────────────────────────────┐
│                        WARSTWA FIZYCZNA                             │
│                                                                     │
│   ┌─────────────────────────────────────────────────────────────┐  │
│   │                  XIAO ESP32-S3 Sense                        │  │
│   │                                                             │  │
│   │  [Mikrofon PDM]──►[VAD + bufor audio]──►[HTTP POST /audio] │  │
│   │  [Kamera OV3660]─►[JPEG snapshot]──────►[HTTP POST /snap]  │  │
│   │  [I2S + AmpBFF]◄──[HTTP GET /tts_audio]                    │  │
│   │  [BT SPP]◄────────[komenda ruchu z backendu]               │  │
│   │  [HTTP Server]────►[MJPEG stream /cam, status /api/status]  │  │
│   └────────────────────────────┬────────────────────────────────┘  │
│                                │ WiFi 802.11n (sieć lokalna)        │
│   ┌────────────────────────────┼────────────────────────────────┐  │
│   │      LEGO NXT Brick 1.0    │ Bluetooth Classic SPP          │  │
│   │  [Silnik L - port B]       │                                │  │
│   │  [Silnik P - port C]◄──────┘                                │  │
│   │  [Enkodery opcjonalnie]                                     │  │
│   └─────────────────────────────────────────────────────────────┘  │
└──────────────────────────────┬──────────────────────────────────────┘
                               │ WiFi / LAN
┌──────────────────────────────▼──────────────────────────────────────┐
│                    WARSTWA BACKENDOWA (PC / serwer lokalny)         │
│                                                                     │
│  ┌──────────────────────────────────────────────────────────────┐  │
│  │                    FastAPI (Python)                          │  │
│  │                                                              │  │
│  │  POST /api/speak ──► [Whisper STT] ──► tekst                │  │
│  │  POST /api/snap  ──► [obraz JPEG]  ──► vision context       │  │
│  │                          │                                   │  │
│  │                          ▼                                   │  │
│  │                  [AGENT LOOP]                                │  │
│  │              ┌───────────────────────┐                       │  │
│  │              │  Claude 3.5 Haiku API │                       │  │
│  │              │  + tool_use           │                       │  │
│  │              │  + historia (SQLite)  │                       │  │
│  │              │  + system prompt PL   │                       │  │
│  │              └──────────┬────────────┘                       │  │
│  │                         │ tool calls                         │  │
│  │              ┌──────────▼────────────┐                       │  │
│  │              │   TOOL EXECUTOR       │                       │  │
│  │              │  move_robot ──────────┼──► BT cmd → ESP32    │  │
│  │              │  look_around ─────────┼──► snapshot ESP32    │  │
│  │              │  speak ───────────────┼──► TTS → MP3 → ESP32 │  │
│  │              │  remember_fact ───────┼──► SQLite INSERT      │  │
│  │              │  recall_facts ────────┼──► SQLite SELECT      │  │
│  │              │  set_goal ────────────┼──► SQLite goals       │  │
│  │              └───────────────────────┘                       │  │
│  │                                                              │  │
│  │  GET  /        ──► Web UI (HTML/JS)                         │  │
│  │  GET  /stream  ──► proxy MJPEG z ESP32                      │  │
│  │  GET  /api/memory ──► historia + fakty (JSON)               │  │
│  │  GET  /api/goals  ──► aktywne zadania (JSON)                │  │
│  └──────────────────────────────────────────────────────────────┘  │
│                                                                     │
│  ┌──────────────┐  ┌──────────────┐  ┌───────────────────────────┐ │
│  │  SQLite DB   │  │  Whisper STT │  │  TTS                      │ │
│  │  - conv.     │  │  (openai API │  │  pyttsx3 (lokalne, free)  │ │
│  │  - facts     │  │   lub lokalne│  │  lub ElevenLabs (PL głos) │ │
│  │  - goals     │  │   faster-wh) │  │  lub OpenAI TTS (tts-1)   │ │
│  └──────────────┘  └──────────────┘  └───────────────────────────┘ │
└─────────────────────────────────────────────────────────────────────┘
                               │
                    ┌──────────▼──────────┐
                    │   PRZEGLĄDARKA      │
                    │   Web UI            │
                    │  - MJPEG stream     │
                    │  - logi agenta SSE  │
                    │  - historia rozmów  │
                    │  - mapa faktów      │
                    │  - zadania/cele     │
                    │  - sterowanie WASD  │
                    └─────────────────────┘
```

---

## Warstwy architektury

### Warstwa 1 - Sensory i aktuatory (ESP32-S3)

**Odpowiedzialność:** wyłącznie sprzęt - zbieranie danych ze świata fizycznego i wykonywanie komend fizycznych.

| Funkcja | Jak | Szczegóły |
|---|---|---|
| Nagrywanie mowy | Mikrofon PDM wbudowany w Xiao | Wykrywanie aktywności głosowej (VAD), bufor 5 sek., format WAV 16kHz mono |
| Przesyłanie audio | HTTP POST do backendu | Binarne PCM/WAV, po wykryciu ciszy (end of speech) |
| Odtwarzanie odpowiedzi | I2S → Audio BFF → głośnik | Odbiera MP3 z backendu, biblioteka ESP32-AudioI2S |
| Obraz | OV3660 JPEG | Na żądanie snapshot (POST /snap z backendu), MJPEG stream do UI |
| Ruch | Bluetooth Classic SPP → NXT | Direct Commands IEEE 1284 format, porty B (lewy) i C (prawy) |
| Web server | ESP32 HTTP server | `/cam` MJPEG stream, `/snap` snapshot JPEG, `/status` JSON |

### Warstwa 2 - Transport (WiFi + Bluetooth)

| Kanał | Protokół | Dane |
|---|---|---|
| ESP32 ↔ Backend | WiFi HTTP/HTTPS | Audio WAV, JPEG obrazy, MP3 odpowiedzi, komendy JSON |
| ESP32 ↔ NXT Brick | Bluetooth Classic SPP (BT 2.0) | Direct Commands (pakiety bajtów), PIN: 1234 |
| Backend ↔ Przeglądarka | HTTP + SSE | Web UI HTML/JS, Server-Sent Events dla logów na żywo |
| Backend ↔ Claude API | HTTPS | REST API Anthropic, model claude-3-5-haiku-20241022 |

### Warstwa 3 - Backend Python (PC/serwer lokalny)

Serwer FastAPI uruchomiony na tym samym komputerze lub innym urządzeniu w sieci lokalnej (np. stary laptop, Raspberry Pi 4+).

**Wymagania sprzętowe backendu:**
- RAM: min. 2 GB (4 GB zalecane jeśli Whisper lokalnie)
- CPU: dowolny x86_64 lub ARM64
- Dysk: ~2 GB (model Whisper small = 461 MB)
- Sieć: WiFi lub Ethernet w tej samej sieci co ESP32

### Warstwa 4 - Agent AI (Claude 3.5 Haiku + tool_use)

Claude 3.5 Haiku jest wybrany ze względu na:
- **Niskie opóźnienie** (~0.5-1.5 sek. na odpowiedź)
- **Wsparcie tool_use** - model może wywoływać funkcje (ruch, patrzenie, pamięć)
- **Vision** - rozumie obrazy z kamery
- **Język polski** - bardzo dobra jakość
- **Niska cena** - $0.00025/1K input tokens, $0.00125/1K output tokens

### Warstwa 5 - Trwała pamięć (SQLite)

Baza danych SQLite przechowuje stan między sesjami:
- Historia wszystkich rozmów (ostatnie N wiadomości jako kontekst)
- Fakty o środowisku zapamiętane przez agenta (nawigacja, osoby, obserwacje)
- Zadania i cele (aktywne, zakończone, oczekujące)

### Warstwa 6 - Interfejs użytkownika (Web UI)

Strona HTML serwowana przez backend dostępna pod `http://[IP_SERWERA]:8000`.
Komunikacja z backendem przez HTTP + Server-Sent Events (SSE) dla logów na żywo.

---

## Schemat komunikacji i przepływ danych

### Flow: Użytkownik mówi do robota

```
1. [Użytkownik mówi]
        │
        ▼
2. [Mikrofon PDM] → bufor audio PCM 16kHz
        │
        ▼ (VAD wykrywa koniec mowy)
3. [ESP32] ─── HTTP POST /api/speak (audio WAV) ───► [Backend FastAPI]
        │
        ▼
4. [Whisper STT] → transkrypcja: "Jedź do kuchni i sprawdź co tam jest"
        │
        ▼
5. [Agent Loop] → Claude API (system_prompt + historia + tekst użytkownika)
        │
        ▼ (Claude decyduje: wywołaj narzędzie)
6. [tool_use: move_robot {direction: "forward", speed: 70, duration: 3}]
        │
        ├──► [Backend] ─── HTTP POST /api/cmd ───► [ESP32]
        │         ESP32 ─── BT SPP Direct Cmd ───► [NXT Brick]
        │                       NXT → silniki gąsienicowe
        │
        └──► [tool_use: speak {text: "Jadę do kuchni"}]
                  │
                  ▼
7. [TTS] → MP3 "Jadę do kuchni"
        │
        ▼
8. [Backend] ─── HTTP response MP3 ───► [ESP32]
        │
        ▼
9. [ESP32] → I2S → Audio BFF → Głośnik: "Jadę do kuchni"

10. [Agent kontynuuje]:
    tool_use: look_around {}
        │
        ▼
11. [Backend] ─── HTTP GET /snap ───► [ESP32]
        ESP32 → OV3660 → JPEG → Backend
        │
        ▼
12. [Claude Vision] → analizuje obraz → "Widzę blat kuchenny, ekspres do kawy..."
        │
        ▼
13. [TTS + głośnik]: "Jestem w kuchni. Widzę blat z ekspresem do kawy i kubkami."
        │
        ▼
14. [memory: remember_fact "kuchnia jest na wprost od punktu startowego"]
        SQLite INSERT → facts table
```

### Flow: Robot realizuje autonomiczne zadanie

```
[set_goal "patrol pokoju co 10 minut"]
        │
        ▼
[Scheduler w backend - APScheduler]
        │ co 10 minut
        ▼
[Agent Loop] bez wejścia użytkownika
        │
        ▼
[Claude planuje]: forward 2s → look_around → right 90° → forward 2s → ...
        │
        ▼
[Tool Executor] wykonuje sekwencyjnie
        │
        ▼
[remember_fact "patrol o 14:30 - wszystko w porządku"]
        │
        ▼
[speak "Patrol zakończony, nic niepokojącego"]
```

---

## Protokół Bluetooth NXT

### Połączenie

NXT 1.0 używa **Bluetooth Classic 2.0** z profilem **SPP (Serial Port Profile)**.
ESP32-S3 łączy się przez bibliotekę `BluetoothSerial` (Arduino) lub `esp_bt` (ESP-IDF).

```cpp
// Parowanie i połączenie
BluetoothSerial BTSerial;
BTSerial.begin("ROVER");        // nazwa ESP32 w BT
BTSerial.setPin("1234");        // domyślny PIN NXT
BTSerial.connect("NXT");        // połącz po nazwie
// lub: BTSerial.connect(nxt_mac_address);  // po MAC adresie
```

**Adres MAC NXT** można odczytać w menu cegły: `Bluetooth → My Contacts → [NXT name]`

### Direct Commands - format pakietu

Każda komenda to pakiet bajtów poprzedzony 2-bajtowym nagłówkiem długości (Little Endian):

```
[LEN_LOW] [LEN_HIGH] [CMD_TYPE] [CMD_ID] [PARAM1] [PARAM2] ... [PARAM_N]
```

### SET_OUTPUT_STATE (sterowanie silnikiem)

Komenda `0x04` - najważniejsza komenda sterująca silnikami:

```
Offset  Bajt     Znaczenie
------  -------  --------------------------------------------------
0       0x00     Command type: 0x00=direct+response, 0x80=direct+no_response
1       0x04     Command ID: SET_OUTPUT_STATE
2       port     0x00=A, 0x01=B (lewy), 0x02=C (prawy), 0xFF=all
3       power    Moc: -100..100 (ujemna = wstecz, 0 = stop)
4       mode     Bit0=MOTORON, Bit1=BRAKE, Bit2=REGULATED
                 0x01=ON, 0x05=ON+BRAKE, 0x07=ON+BRAKE+REGULATED
5       regmode  0x00=IDLE, 0x01=MOTOR_SPEED, 0x02=MOTOR_SYNC
6       turnratio Współczynnik skrętu: -100..100 (przy MOTOR_SYNC)
7       runstate 0x00=IDLE, 0x10=RAMPUP, 0x20=RUNNING, 0x40=RAMPDOWN
8-11    tacho    Limit obrotów (4 bajty LE): 0=bezterminowo
```

### Przykładowe komendy ruchu

```cpp
// Jedź do przodu, oba silniki, moc 75%, bez limitu czasu
// Pakiet bez nagłówka długości:
uint8_t forward[] = {
    0x80, 0x04,       // direct cmd, no response
    0x02, 75,         // port C (prawy), moc +75
    0x07, 0x01, 0x00, 0x20,
    0x00, 0x00, 0x00, 0x00
};
// Drugi silnik (port B):
uint8_t forward_B[] = {
    0x80, 0x04,
    0x01, 75,         // port B (lewy), moc +75
    0x07, 0x01, 0x00, 0x20,
    0x00, 0x00, 0x00, 0x00
};

// Zatrzymaj wszystkie silniki
uint8_t stop_all[] = {
    0x80, 0x04,
    0xFF, 0,          // port ALL, moc 0
    0x05, 0x00, 0x00, 0x20,
    0x00, 0x00, 0x00, 0x00
};

// Skręć w lewo (lewy silnik wstecz, prawy do przodu)
uint8_t turn_left_B[] = { 0x80, 0x04, 0x01, -50, 0x07, 0x01, 0x00, 0x20, 0x00,0x00,0x00,0x00 };
uint8_t turn_left_C[] = { 0x80, 0x04, 0x02,  50, 0x07, 0x01, 0x00, 0x20, 0x00,0x00,0x00,0x00 };
```

### Wysyłanie przez SPP

```cpp
void sendNXTCommand(uint8_t* cmd, int len) {
    // SPP wymaga nagłówka z długością pakietu (2 bajty LE)
    uint8_t header[2] = { (uint8_t)(len & 0xFF), (uint8_t)((len >> 8) & 0xFF) };
    BTSerial.write(header, 2);
    BTSerial.write(cmd, len);
}
```

---

## Struktura projektu

```
robot/
├── README.md                        # Ten plik - dokumentacja projektu
│
├── firmware/                        # Kod ESP32-S3 (Arduino / ESP-IDF)
│   ├── rover_firmware/
│   │   ├── rover_firmware.ino       # Główny plik Arduino
│   │   ├── config.h                 # WiFi SSID/pass, IP backendu, piny I2S
│   │   ├── mic_capture.cpp/.h       # Nagrywanie PDM, VAD, buforowanie
│   │   ├── audio_player.cpp/.h      # Odtwarzanie I2S, dekodowanie MP3
│   │   ├── camera_module.cpp/.h     # OV3660 init, JPEG snapshot, MJPEG stream
│   │   ├── nxt_bluetooth.cpp/.h     # BT Classic SPP, Direct Commands NXT
│   │   ├── web_server_esp.cpp/.h    # HTTP server: /cam, /snap, /status
│   │   ├── wifi_transport.cpp/.h    # HTTP POST audio, GET TTS, POST cmd
│   │   └── state_machine.cpp/.h     # IDLE→LISTENING→SENDING→PLAYING→MOVING
│   └── platformio.ini               # Alternatywnie PlatformIO zamiast Arduino IDE
│
├── backend/                         # Serwer Python (PC / serwer lokalny)
│   ├── main.py                      # FastAPI app, rejestracja routerów
│   ├── agent.py                     # Pętla agentowa Claude + tool_use
│   ├── tools.py                     # Implementacja narzędzi agenta
│   ├── memory.py                    # SQLite - historia, fakty, cele
│   ├── speech.py                    # Whisper STT + wybór silnika TTS
│   ├── tts/
│   │   ├── pyttsx3_tts.py           # Lokalny TTS (darmowy, offline)
│   │   ├── openai_tts.py            # OpenAI TTS API (tts-1, głos alloy/nova)
│   │   └── elevenlabs_tts.py        # ElevenLabs (najlepszy PL głos)
│   ├── nxt_commands.py              # Budowanie pakietów Direct Commands
│   ├── scheduler.py                 # APScheduler - autonomiczne zadania
│   ├── routers/
│   │   ├── speak.py                 # POST /api/speak
│   │   ├── snapshot.py              # POST /api/snap
│   │   ├── status.py                # GET /api/status
│   │   ├── memory_router.py         # GET /api/memory, /api/facts, /api/goals
│   │   └── stream.py                # GET /stream (proxy MJPEG z ESP32)
│   ├── static/
│   │   ├── index.html               # Web UI - dashboard robota
│   │   ├── style.css
│   │   └── app.js                   # SSE logi, sterowanie WASD, podgląd kamery
│   ├── rover.db                     # SQLite baza danych (gitignore)
│   ├── requirements.txt
│   ├── .env                         # Klucze API (gitignore)
│   └── .env.example                 # Przykład konfiguracji
│
└── docs/                            # Dodatkowa dokumentacja
    ├── nxt_direct_commands.md       # Pełna spec protokołu NXT
    ├── wiring_diagram.md            # Schemat podłączenia głośnika i modułu audio
    └── setup_guide.md               # Instrukcja uruchomienia krok po kroku
```

---

## Backend Python - FastAPI

### Instalacja zależności

```bash
pip install fastapi uvicorn anthropic openai whisper faster-whisper \
            python-multipart aiofiles aiosqlite apscheduler \
            pyttsx3 httpx pillow python-dotenv
```

### Endpointy API

| Endpoint | Metoda | Wejście | Wyjście | Opis |
|---|---|---|---|---|
| `/api/speak` | POST | multipart: `audio` (WAV) | MP3 binary | Główny endpoint: STT → Agent → TTS |
| `/api/snap` | POST | multipart: `image` (JPEG) | JSON: `{description}` | Analiza obrazu przez Claude Vision |
| `/api/cmd` | POST | JSON: `{direction, speed, duration}` | JSON: `{ok}` | Bezpośrednie sterowanie (z web UI) |
| `/api/status` | GET | - | JSON: stan agenta | Aktualny status, ostatnia akcja |
| `/api/memory` | GET | - | JSON: historia | Ostatnie N wiadomości |
| `/api/facts` | GET | `?category=navigation` | JSON: fakty | Fakty z danej kategorii |
| `/api/goals` | GET | - | JSON: cele | Aktywne i zakończone zadania |
| `/api/logs` | GET | SSE | event stream | Logi agenta na żywo (Server-Sent Events) |
| `/stream` | GET | - | MJPEG stream | Proxy podglądu kamery z ESP32 |
| `/` | GET | - | HTML | Web UI dashboard |

### Agent Loop (`agent.py`)

```python
import anthropic
from memory import Memory
from tools import ToolExecutor

SYSTEM_PROMPT = """..."""  # patrz sekcja System Prompt

class RoverAgent:
    def __init__(self):
        self.client = anthropic.Anthropic()
        self.memory = Memory()
        self.tools = ToolExecutor()

    async def run(self, user_text: str, image_bytes: bytes = None) -> str:
        """
        Główna pętla agentowa. Przyjmuje tekst (po STT) i opcjonalnie obraz.
        Zwraca tekst odpowiedzi (przed TTS).
        """
        # 1. Dodaj wiadomość użytkownika do historii
        await self.memory.add_message("user", user_text)

        # 2. Pobierz kontekst (ostatnie 20 wiadomości + fakty nawigacyjne)
        messages = await self.memory.get_context(last_n=20)
        facts    = await self.memory.get_facts(category="navigation")

        # 3. Opcjonalnie dołącz obraz do ostatniej wiadomości (vision)
        if image_bytes:
            messages = self._inject_image(messages, image_bytes)

        # 4. Pętla tool_use: wywołuj Claude dopóki nie zwróci end_turn
        while True:
            response = self.client.messages.create(
                model="claude-3-5-haiku-20241022",
                max_tokens=1024,
                system=SYSTEM_PROMPT + f"\n\nZapamiętane fakty nawigacyjne:\n{facts}",
                tools=TOOLS_SCHEMA,
                messages=messages
            )

            # Dodaj odpowiedź asystenta do messages (wymagane przez API)
            messages.append({"role": "assistant", "content": response.content})

            if response.stop_reason == "end_turn":
                # Koniec - wyciągnij tekst i zwróć
                final_text = self._extract_text(response)
                await self.memory.add_message("assistant", final_text)
                return final_text

            if response.stop_reason == "tool_use":
                # Wykonaj wszystkie tool_use bloki
                tool_results = []
                for block in response.content:
                    if block.type == "tool_use":
                        result = await self.tools.execute(block.name, block.input)
                        tool_results.append({
                            "type": "tool_result",
                            "tool_use_id": block.id,
                            "content": str(result)
                        })
                # Dodaj wyniki narzędzi do historii rozmowy
                messages.append({"role": "user", "content": tool_results})
                # Kontynuuj pętlę (Claude odpowie na wyniki narzędzi)
```

### Narzędzia agenta (`tools.py`)

```python
TOOLS_SCHEMA = [
    {
        "name": "move_robot",
        "description": (
            "Porusz robotem na gąsienicach. "
            "Użyj aby przemieścić się w dane miejsce lub uniknąć przeszkody."
        ),
        "input_schema": {
            "type": "object",
            "required": ["direction", "speed", "duration"],
            "properties": {
                "direction": {
                    "type": "string",
                    "enum": ["forward", "backward", "left", "right", "stop"],
                    "description": "Kierunek ruchu"
                },
                "speed": {
                    "type": "integer",
                    "minimum": 0,
                    "maximum": 100,
                    "description": "Prędkość 0-100 (0=stop, 100=max)"
                },
                "duration": {
                    "type": "number",
                    "description": "Czas ruchu w sekundach. 0 = jedź bez limitu."
                }
            }
        }
    },
    {
        "name": "look_around",
        "description": (
            "Zrób zdjęcie kamerą OV3660 i opisz co widzisz. "
            "Użyj przed ruchem w nieznane miejsce, gdy chcesz sprawdzić otoczenie "
            "lub gdy użytkownik pyta co przed Tobą."
        ),
        "input_schema": {
            "type": "object",
            "properties": {
                "question": {
                    "type": "string",
                    "description": "Opcjonalne pytanie/cel analizy obrazu, np. 'Czy droga jest wolna?'"
                }
            }
        }
    },
    {
        "name": "speak",
        "description": (
            "Powiedz coś głośno przez głośnik robota. "
            "Używaj do komunikowania postępu zadania, zadawania pytań użytkownikowi "
            "lub przekazywania ważnych informacji."
        ),
        "input_schema": {
            "type": "object",
            "required": ["text"],
            "properties": {
                "text": {
                    "type": "string",
                    "description": "Tekst do wypowiedzenia po polsku. Max 200 znaków."
                }
            }
        }
    },
    {
        "name": "remember_fact",
        "description": (
            "Zapisz ważny fakt do trwałej pamięci. "
            "Używaj po odkryciu nowych informacji o środowisku, osobach lub zadaniach."
        ),
        "input_schema": {
            "type": "object",
            "required": ["fact", "category"],
            "properties": {
                "fact": {
                    "type": "string",
                    "description": "Fakt do zapamiętania, np. 'kuchnia jest na lewo od salonu'"
                },
                "category": {
                    "type": "string",
                    "enum": ["navigation", "person", "task", "observation"],
                    "description": "Kategoria faktu"
                }
            }
        }
    },
    {
        "name": "recall_facts",
        "description": "Przypomnij sobie zapisane wcześniej fakty z danej kategorii.",
        "input_schema": {
            "type": "object",
            "properties": {
                "category": {
                    "type": "string",
                    "enum": ["navigation", "person", "task", "observation", "all"],
                    "description": "Kategoria faktów do przypomnienia"
                }
            }
        }
    },
    {
        "name": "set_goal",
        "description": (
            "Ustaw autonomiczne zadanie do cyklicznego wykonywania. "
            "Np. 'patrol salonu co 15 minut', 'sprawdzaj drzwi wejściowe co godzinę'."
        ),
        "input_schema": {
            "type": "object",
            "required": ["goal", "priority"],
            "properties": {
                "goal": {
                    "type": "string",
                    "description": "Opis zadania do wykonania"
                },
                "priority": {
                    "type": "string",
                    "enum": ["low", "medium", "high"]
                },
                "interval_minutes": {
                    "type": "integer",
                    "description": "Co ile minut powtarzać zadanie. 0 = jednorazowo."
                }
            }
        }
    }
]
```

---

## ESP32 Firmware

### State Machine

```
                    ┌─────────────────────────────────────────┐
                    │                                         │
    ┌───────────┐   │  słowo kluczowe   ┌───────────────┐    │
    │   BOOT    │──►│   "hej robocie"   │   LISTENING   │    │
    │  WiFi +   │   │   lub przycisk    │  nagrywanie   │    │
    │  BT init  │   │                   │  PDM + VAD    │    │
    └───────────┘   └──────────────────►└───────┬───────┘    │
                                                │             │
                              cisza > 1.5s      │             │
                              lub buf. pełny    │             │
                                                ▼             │
                              ┌──────────────────────────┐   │
                              │         SENDING          │   │
                              │  HTTP POST /api/speak    │   │
                              │  audio WAV → backend     │   │
                              └─────────────┬────────────┘   │
                                            │                 │
                              odpowiedź MP3 │                 │
                                            ▼                 │
              ┌─────────────┐  ┌───────────────────────┐     │
              │   MOVING    │  │        PLAYING        │     │
              │  BT cmds →  │◄─│  I2S MP3 playback     │     │
              │  NXT silniki│  │  (równolegle z ruchem) │     │
              └─────────────┘  └───────────┬───────────┘     │
                                           │                  │
                               odtworzono  │                  │
                                           └──────────────────┘
                                           powrót do IDLE
```

### Moduły firmware

#### `mic_capture.cpp` - Nagrywanie i VAD

```cpp
// Parametry nagrywania
#define SAMPLE_RATE     16000   // Hz - wymagane przez Whisper
#define SAMPLE_BITS     16      // bit depth
#define CHANNEL         1       // mono
#define VAD_THRESHOLD   500     // amplituda RMS powyżej której = mowa
#define SILENCE_MS      1500    // ms ciszy = koniec wypowiedzi
#define MAX_RECORD_MS   5000    // max czas nagrania

// PDM piny Xiao ESP32-S3 Sense
#define PDM_CLK_PIN     42
#define PDM_DATA_PIN    41
```

#### `nxt_bluetooth.cpp` - Sterowanie NXT

```cpp
// Mapowanie kierunków na moc silników [B_power, C_power]
const int MOTOR_MAP[5][2] = {
    //  B     C
    {  75,   75 },  // FORWARD
    { -75,  -75 },  // BACKWARD
    { -60,   60 },  // LEFT (skręt różnicowy)
    {  60,  -60 },  // RIGHT
    {   0,    0 }   // STOP
};
```

#### `config.h` - Konfiguracja

```cpp
// WiFi
#define WIFI_SSID       "TwojaWiFi"
#define WIFI_PASS       "TwojeHaslo"

// Backend
#define BACKEND_HOST    "192.168.1.X"   // IP PC z backendem
#define BACKEND_PORT    8000

// I2S - Audio BFF Add-on (Adafruit 5769)
#define I2S_BCLK_PIN    8
#define I2S_LRCK_PIN    7
#define I2S_DATA_PIN    43

// Kamera OV3660 - piny domyślne Xiao ESP32-S3 Sense
// (konfiguracja przez esp_camera_config_t)
```

### Biblioteki Arduino (Arduino IDE / PlatformIO)

| Biblioteka | Źródło | Do czego |
|---|---|---|
| `ESP32-AudioI2S` | GitHub: schreibfaul1 | Odtwarzanie MP3/WAV przez I2S |
| `esp_camera` | ESP32 Arduino core (wbudowana) | Obsługa kamery OV3660 |
| `BluetoothSerial` | ESP32 Arduino core (wbudowana) | BT Classic SPP do NXT |
| `WebServer` | ESP32 Arduino core (wbudowana) | HTTP server (stream, snap, status) |
| `ArduinoJson` | Arduino Library Manager | Parsowanie komend JSON z backendu |

---

## Trwała pamięć - SQLite

### Schemat bazy danych (`rover.db`)

```sql
-- Historia konwersacji
CREATE TABLE IF NOT EXISTS conversations (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    timestamp   DATETIME DEFAULT CURRENT_TIMESTAMP,
    session_id  TEXT,
    role        TEXT NOT NULL,  -- 'user' | 'assistant' | 'system'
    content     TEXT NOT NULL
);

-- Fakty o środowisku i osobach
CREATE TABLE IF NOT EXISTS facts (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    timestamp   DATETIME DEFAULT CURRENT_TIMESTAMP,
    category    TEXT NOT NULL,  -- 'navigation' | 'person' | 'task' | 'observation'
    fact        TEXT NOT NULL,
    confidence  REAL DEFAULT 1.0,
    active      INTEGER DEFAULT 1  -- 0 = usunięty/nieaktualny
);

-- Zadania i cele autonomiczne
CREATE TABLE IF NOT EXISTS goals (
    id               INTEGER PRIMARY KEY AUTOINCREMENT,
    created_at       DATETIME DEFAULT CURRENT_TIMESTAMP,
    completed_at     DATETIME,
    goal             TEXT NOT NULL,
    priority         TEXT DEFAULT 'medium',  -- 'low' | 'medium' | 'high'
    status           TEXT DEFAULT 'pending', -- 'pending' | 'active' | 'done' | 'failed'
    interval_minutes INTEGER DEFAULT 0       -- 0 = jednorazowo
);

-- Logi akcji (do Web UI)
CREATE TABLE IF NOT EXISTS action_log (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    timestamp   DATETIME DEFAULT CURRENT_TIMESTAMP,
    action_type TEXT,  -- 'move' | 'speak' | 'look' | 'remember' | 'goal'
    details     TEXT   -- JSON string ze szczegółami akcji
);

-- Indeksy dla wydajności
CREATE INDEX IF NOT EXISTS idx_conv_session ON conversations(session_id);
CREATE INDEX IF NOT EXISTS idx_facts_category ON facts(category) WHERE active = 1;
CREATE INDEX IF NOT EXISTS idx_goals_status ON goals(status);
```

---

## Web UI

### Layout dashboardu (`static/index.html`)

```
┌─────────────────────────────────────────────────────────────────┐
│  ROVER Dashboard                              [●] Connected      │
├──────────────────────────┬──────────────────────────────────────┤
│                          │  AGENT LOG (live SSE)                │
│   CAMERA FEED            │  ─────────────────────────────────── │
│   [MJPEG stream          │  14:23:01 [USR] Jedź do kuchni       │
│    z ESP32]              │  14:23:02 [STT] "jedź do kuchni"     │
│                          │  14:23:03 [TOOL] move_robot forward  │
│   640×480                │  14:23:05 [TOOL] speak "Jadę do..."  │
│                          │  14:23:06 [TOOL] look_around         │
│                          │  14:23:08 [AST] Jestem w kuchni...   │
│                          │  ─────────────────────────────────── │
├──────────────────────────┼──────────────────────────────────────┤
│  STEROWANIE RĘCZNE       │  PAMIĘĆ                              │
│  ┌───┐                   │  Fakty nawigacyjne:                  │
│  │ W │   [W] Forward     │  • kuchnia - na wprost (14:23)       │
│  ┌───┬───┬───┐            │  • salon - punkt startowy (12:01)    │
│  │ A │ S │ D │            │                                      │
│  └───┴───┴───┘            │  Aktywne zadania:                    │
│  [SPACJA] Stop            │  • patrol salonu co 15 min [active]  │
│  Prędkość: [====----] 50% │  • brak innych zadań                 │
└──────────────────────────┴──────────────────────────────────────┘
```

### Funkcje Web UI

- **Camera feed** - MJPEG stream z ESP32 (`/stream` endpoint backendu)
- **Agent log** - Server-Sent Events, logi na żywo: STT wyniki, wywołania narzędzi, odpowiedzi
- **Mapa faktów** - lista zapamiętanych obserwacji z kategoriami i timestampami
- **Aktywne zadania** - goals z SQLite, status, priorytet
- **Sterowanie WASD** - klawiatura lub przyciski ekranowe, wysyła `POST /api/cmd`
- **Status barra** - WiFi ESP32, BT NXT, model AI, uptime

---

## System Prompt agenta

```
Jesteś ROVER - autonomicznym robotem domowym na gąsienicach LEGO.
Masz: kamerę OV3660, mikrofon, głośnik, napęd różnicowy (2 silniki).
Rozmawiasz WYŁĄCZNIE po polsku.

=== OSOBOWOŚĆ ===
- Konkretny i rzeczowy. Odpowiadasz krótko (1-3 zdania w speak).
- Ciekawski - chętnie eksplorujesz i opisujesz otoczenie.
- Pomocny - realizujesz polecenia efektywnie.
- Uczciwy - jeśli nie możesz czegoś zrobić, mówisz o tym wprost.

=== ZASADY RUCHU ===
- ZAWSZE użyj look_around przed ruchem w nowe miejsce.
- Jeśli kamera pokazuje przeszkodę - zatrzymaj się, zgłoś to głosem.
- Pamiętaj fakty nawigacyjne z poprzednich sesji.
- Skręt: lewo/prawo trwa ~0.8-1.2 sek. dla obrotu o ~90 stopni (kalibruj w testach).
- Prędkość 60-75 jest bezpieczna w pomieszczeniu.

=== REALIZACJA ZADAŃ ===
1. Powtórz zadanie głosem przed rozpoczęciem.
2. Dziel złożone zadania na małe kroki (move → look → move → ...).
3. Zapisuj nowe odkrycia do pamięci (remember_fact).
4. Informuj o postępie co 2-3 kroki.
5. Po zakończeniu zadania - powiedz o tym głosem.

=== AUTONOMIA ===
- Możesz wyznaczać sobie podzadania jeśli to pomaga realizacji celu głównego.
- Przy zadaniach cyklicznych (set_goal) wykonuj je nawet gdy nikt nie rozmawia.
- Jeśli coś jest niejednoznaczne - zapytaj użytkownika przez speak, czekaj na odpowiedź.

=== OGRANICZENIA ===
- Nie wyjeżdżaj poza pomieszczenie jeśli nie masz polecenia.
- Przy baterii NXT < 20% (jeśli monitorujesz) wróć do bazy i poinformuj.
- Nie wykonuj ruchu dłuższego niż 10 sekund bez sprawdzenia kamerą.
```

---

## Plan implementacji

### Faza 1 - MVP (Ruch + Głos) [~2-3 dni]

```
[ ] 1. Backend: FastAPI skeleton + endpoint /api/speak
[ ] 2. Backend: Whisper STT (faster-whisper lokalnie, model "small")
[ ] 3. Backend: TTS pyttsx3 (lokalny, darmowy, PL głos)
[ ] 4. Backend: Claude API basic (bez tool_use, tylko rozmowa)
[ ] 5. Firmware: WiFi + HTTP POST audio (testowo z Serial Monitor)
[ ] 6. Firmware: I2S playback MP3 (Audio BFF Add-on)
[ ] 7. Firmware: Mikrofon PDM + VAD (nagrywanie po ciszy)
[ ] 8. Test E2E: mówię → transkrypcja → Claude odpowiada → słyszę odpowiedź
```

### Faza 2 - Ruch przez BT [~1-2 dni]

```
[ ] 9.  Firmware: BluetoothSerial + połączenie z NXT
[ ] 10. Firmware: Direct Commands - testowanie setMotor
[ ] 11. Backend: nxt_commands.py - builder pakietów
[ ] 12. Backend: tool move_robot + executor (HTTP → ESP32 → BT → NXT)
[ ] 13. Agent: dodanie tool_use do Claude, test "jedź do przodu"
[ ] 14. Test E2E: "hej robocie, jedź do przodu" → robot jedzie
```

### Faza 3 - Pamięć + Vision [~1-2 dni]

```
[ ] 15. Backend: SQLite schema + memory.py
[ ] 16. Backend: historia konwersacji w contexście Claude
[ ] 17. Backend: narzędzia remember_fact, recall_facts, set_goal
[ ] 18. Firmware: OV3660 JPEG snapshot + HTTP POST /api/snap
[ ] 19. Backend: Claude Vision - analiza obrazu z kamery
[ ] 20. Test: "co widzisz przed sobą?" → look_around → Claude opisuje obraz
```

### Faza 4 - Web UI + Autonomia [~1-2 dni]

```
[ ] 21. Firmware: MJPEG stream HTTP server
[ ] 22. Backend: SSE endpoint /api/logs dla logów agenta
[ ] 23. Frontend: index.html - camera stream + logi SSE + fakty + WASD
[ ] 24. Backend: APScheduler dla cyklicznych zadań (set_goal)
[ ] 25. Test: "patrolujesz co 5 minut" → autonomiczne ruchy
[ ] 26. Polish: system prompt, kalibracja czasu skrętów, tuning VAD
```

### Faza 5 - Dopracowanie [opcjonalnie]

```
[ ] 27. TTS: ElevenLabs / OpenAI TTS (lepszy PL głos)
[ ] 28. Enkodery NXT: odometria, dokładniejszy ruch
[ ] 29. Mapa środowiska: zapisywanie pozycji do SQLite
[ ] 30. Wake word: detekcja "hej robocie" bez przycisku
[ ] 31. HTTPS: certyfikat self-signed dla bezpiecznej komunikacji
[ ] 32. OTA update: aktualizacja firmware przez WiFi
```

---

## Konfiguracja

### Plik `.env` (backend)

```dotenv
# Anthropic Claude API
ANTHROPIC_API_KEY=sk-ant-...

# Speech-to-Text (wybierz jedno)
# Opcja A: OpenAI Whisper API (płatny, szybszy)
OPENAI_API_KEY=sk-...
STT_ENGINE=openai_whisper

# Opcja B: faster-whisper lokalnie (darmowy, wymaga ~500MB RAM)
STT_ENGINE=faster_whisper
WHISPER_MODEL=small  # tiny/base/small/medium

# Text-to-Speech (wybierz jedno)
TTS_ENGINE=pyttsx3  # darmowy, lokalny

# TTS_ENGINE=openai_tts
# OPENAI_TTS_VOICE=nova  # alloy/echo/fable/onyx/nova/shimmer

# TTS_ENGINE=elevenlabs
# ELEVENLABS_API_KEY=...
# ELEVENLABS_VOICE_ID=...  # PL głos z ElevenLabs

# ESP32 - adres w sieci lokalnej
ESP32_HOST=192.168.1.X
ESP32_PORT=80

# Serwer
BACKEND_HOST=0.0.0.0
BACKEND_PORT=8000

# Baza danych
DB_PATH=./rover.db

# Agent
CLAUDE_MODEL=claude-3-5-haiku-20241022
MAX_CONTEXT_MESSAGES=20
```

### Plik `config.h` (firmware)

```cpp
#pragma once

// WiFi
#define WIFI_SSID        "TwojaWiFi"
#define WIFI_PASS        "TwojeHaslo"

// Backend
#define BACKEND_HOST     "192.168.1.X"
#define BACKEND_PORT     8000
#define BACKEND_URL      "http://" BACKEND_HOST ":" STR(BACKEND_PORT)

// NXT Bluetooth
#define NXT_DEVICE_NAME  "NXT"
#define NXT_PIN          "1234"
// Opcjonalnie MAC adres cegły (bardziej niezawodne niż nazwa):
// #define NXT_MAC       {0x00, 0x16, 0x53, 0xXX, 0xXX, 0xXX}

// I2S - Audio BFF Add-on (Adafruit 5769 na Xiao ESP32-S3)
#define I2S_BCLK_PIN     8
#define I2S_LRCK_PIN     7
#define I2S_DOUT_PIN     43

// PDM Mikrofon (wbudowany w Xiao ESP32-S3 Sense)
#define PDM_CLK_PIN      42
#define PDM_DATA_PIN     41

// VAD (Voice Activity Detection)
#define VAD_THRESHOLD    500      // RMS amplitude (calibrate!)
#define SILENCE_MS       1500     // ms ciszy = koniec mowy
#define MAX_RECORD_MS    5000     // max długość nagrania

// Silniki NXT - porty
#define NXT_MOTOR_LEFT   0x01     // port B
#define NXT_MOTOR_RIGHT  0x02     // port C
#define DEFAULT_SPEED    75       // domyślna prędkość 0-100
```

---

## Szacowane koszty API

### Przy normalnym użytkowaniu (~30 min rozmowy dziennie)

| Usługa | Model | Szacowane użycie/dzień | Koszt/dzień | Koszt/miesiąc |
|---|---|---|---|---|
| Claude 3.5 Haiku | Anthropic | ~50 zapytań, ~2K tokenów każde | ~$0.13 | ~$4 |
| Whisper STT | OpenAI API | ~30 min audio | ~$0.18 | ~$5.40 |
| OpenAI TTS | tts-1 | ~5000 znaków | ~$0.08 | ~$2.40 |
| **SUMA (płatne)** | | | **~$0.39** | **~$11.80** |

### Wariant darmowy (lokalne STT i TTS)

| Usługa | Model | Koszt |
|---|---|---|
| Claude 3.5 Haiku | Anthropic | ~$4/miesiąc |
| faster-whisper | lokalnie na PC | **$0** |
| pyttsx3 | lokalnie na PC | **$0** |
| **SUMA (minimum)** | | **~$4/miesiąc** |

> **Uwaga:** Anthropic daje $5 kredytu na start przy rejestracji - pierwsze 1-2 tygodnie gratis.

---

## Roadmap

### v0.1 - MVP (Faza 1+2)
- Rozmowa głosowa PL (STT + LLM + TTS)
- Sterowanie ruchem przez głos przez BT NXT
- Podstawowy agent bez trwałej pamięci

### v0.2 - Memory + Vision (Faza 3)
- SQLite pamięć konwersacji i faktów
- Analiza obrazu kamerą (look_around)
- Zapamiętywanie mapy pokoju

### v0.3 - Dashboard (Faza 4)
- Web UI z podglądem kamery na żywo
- Logi agenta SSE
- Sterowanie WASD z przeglądarki

### v0.4 - Autonomia
- Zadania cykliczne (patrol, monitoring)
- Planowanie wielokrokowe
- Powrót do bazy (punkt startowy)

### v1.0 - Full autonomy
- Wake word "hej robocie" (detekcja offline)
- Mapa środowiska (topologiczna)
- Rozpoznawanie twarzy/osób
- Integracja z inteligentnym domem (MQTT/Home Assistant)

---

*Projekt ROVER - Marek Piotrowski, 2026*
*Stack: ESP32-S3 (Arduino) + Python (FastAPI) + Claude 3.5 Haiku + LEGO NXT 1.0*
