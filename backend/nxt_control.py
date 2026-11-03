"""
NXT Brick control — macOS, dwustrategiczny:

Strategia 1 (preferowana): /dev/cu.RoboC przez pyserial/os
  - Otwieramy IOBluetoothDevice.openConnection() żeby zestawić BT
  - Następnie otwieramy /dev/cu.RoboC z O_NONBLOCK (unika blokowania)
  - fcntl usuwa O_NONBLOCK po udanym open() dla normalnego zapisu
  - Proste, niezawodne, nie wymaga RFCOMM delegate/RunLoop

Strategia 2 (fallback): IOBluetooth openRFCOMMChannelSync
  - Wątek BT z NSRunLoop + delegate
  - Używana jeśli /dev/cu.RoboC nie istnieje lub się nie otworzy

Motor port mapping:
  Port B = left track  (port value = 0x01)
  Port C = right track (port value = 0x02)

SET_OUTPUT_STATE format (12 bajtów, zgodnie z LEGO NXT Communication Protocol):
  [0x80, 0x04, port, power_s8, mode, regulation_mode, turn_ratio, run_state, tacho_limit_u32_LE]
"""

import errno
import fcntl
import logging
import os
import queue as _queue_module
import struct
import termios
import threading
import time

logger = logging.getLogger(__name__)

# MAC adres NXT (sparowany jako RoboC+)
NXT_MAC = "00:16:53:11:D2:BE"
RFCOMM_CHANNEL_ID = 1

# /dev node tworzony przez macOS dla SPP (nazwa bez '+')
NXT_DEV = "/dev/cu.RoboC"

DEFAULT_SPEED = 90   # 90% mocy domyślnie
TURN_SPEED = 90

# Mnożnik prędkości: slow=0.75, normal=1.0 (domyślny)
# Zmieniany przez set_speed_mode()
_speed_multiplier: float = 1.0

# NXT port values (nxt-python Port enum: A=0, B=1, C=2)
_PORT_B = 0x01   # left track
_PORT_C = 0x02   # right track

_ERR_ALREADY_EXISTS = -536870212   # kBluetoothErrorCodeAlreadyExists

# --- Globalne połączenie ---
_fd: int | None = None              # fd do /dev/cu.RoboC (strategia 1)
_channel = None                     # IOBluetoothRFCOMMChannel (strategia 2)
_device = None                      # IOBluetoothDevice (obie strategie)
_lock = threading.Lock()
_stop_timer: threading.Timer | None = None
_keepalive_timer: threading.Timer | None = None

# Odbiór danych (strategia 1 - wątek czytający)
_recv_buf = bytearray()
_recv_lock = threading.Lock()
_recv_event = threading.Event()
_reader_thread: threading.Thread | None = None
_reader_stop = threading.Event()

# Wątek BT z RunLoop (strategia 2)
_bt_thread: threading.Thread | None = None
_bt_runloop = None
_bt_stop = threading.Event()
_bt_task_queue = _queue_module.Queue()
_delegate = None

try:
    import objc
    from Foundation import NSObject, NSRunLoop, NSDate
    from IOBluetooth import IOBluetoothDevice
    IOBLUETOOTH_AVAILABLE = True
except ImportError:
    IOBLUETOOTH_AVAILABLE = False
    logger.warning("[NXT] pyobjc-framework-IOBluetooth not available — stub mode")


# ===========================================================================
# Strategia 1: /dev/cu.RoboC przez os.open() + O_NONBLOCK
# ===========================================================================

def _open_serial_nonblock(dev_path: str, timeout: float = 10.0) -> int | None:
    """
    Otwórz /dev/cu.RoboC bez blokowania.

    Trick: O_NONBLOCK przy open() nie blokuje nawet gdy BT jeszcze się łączy.
    Po otwarciu usuwamy O_NONBLOCK (fcntl) żeby zapis był blokujący.

    Zwraca fd (int) lub None przy błędzie.
    """
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            fd = os.open(dev_path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        except OSError as e:
            if e.errno in (errno.ENOENT, errno.EBUSY, errno.ENXIO):
                logger.debug(f"[NXT/serial] open {dev_path} errno={e.errno}: {e.strerror}, retry...")
                time.sleep(0.5)
                continue
            logger.warning(f"[NXT/serial] open {dev_path} failed: {e}")
            return None

        # Ustawienia terminala: raw, baud 115200 (SPP ignoruje baud, ale musi być ustawiony)
        # tcgetattr zwraca: [iflag, oflag, cflag, lflag, ispeed, ospeed, cc]
        # indeksy:              0      1      2      3      4       5      6
        try:
            attrs = termios.tcgetattr(fd)
            # iflag: wyłącz przetwarzanie wejścia
            attrs[0] &= ~(termios.IGNBRK | termios.BRKINT | termios.PARMRK |
                          termios.ISTRIP | termios.INLCR | termios.IGNCR |
                          termios.ICRNL | termios.IXON)
            # oflag: wyłącz przetwarzanie wyjścia
            attrs[1] &= ~termios.OPOST
            # cflag: 8 bitów, bez parzystości, wyłącz przetwarzanie rozmiaru
            attrs[2] &= ~(termios.CSIZE | termios.PARENB)
            attrs[2] |= termios.CS8
            # lflag: raw (wyłącz echo, canonical, sygnały)
            attrs[3] &= ~(termios.ECHO | termios.ECHONL | termios.ICANON |
                          termios.ISIG | termios.IEXTEN)
            # ispeed / ospeed (indeksy 4 i 5)
            attrs[4] = termios.B115200
            attrs[5] = termios.B115200
            # cc: VMIN=0 (non-blocking read), VTIME=1 (100ms timeout)
            attrs[6][termios.VMIN] = 0
            attrs[6][termios.VTIME] = 1
            termios.tcsetattr(fd, termios.TCSANOW, attrs)
        except Exception as e:
            logger.debug(f"[NXT/serial] tcsetattr warning: {e}")

        # Usuń O_NONBLOCK — teraz write() będzie blokować do wysłania
        flags = fcntl.fcntl(fd, fcntl.F_GETFL)
        fcntl.fcntl(fd, fcntl.F_SETFL, flags & ~os.O_NONBLOCK)

        logger.info(f"[NXT/serial] Otwarto {dev_path} fd={fd}")
        return fd

    logger.warning(f"[NXT/serial] Timeout otwierania {dev_path}")
    return None


def _reader_thread_func(fd: int):
    """Wątek czytający dane z /dev/cu.RoboC — konsumuje odpowiedzi NXT żeby nie blokowały bufora."""
    logger.debug("[NXT/serial] reader thread start")
    while not _reader_stop.is_set():
        try:
            data = os.read(fd, 256)
            if data:
                # Konsumuj i odrzucaj — all commands are no-response (0x80)
                logger.debug(f"[NXT/serial] recv (discarded) {len(data)} bytes: {data.hex()}")
            else:
                # VMIN=0, VTIME=1: brak danych → krótka pauza
                time.sleep(0.02)
        except OSError as e:
            if e.errno in (errno.EAGAIN, errno.EWOULDBLOCK):
                time.sleep(0.02)
            elif e.errno == errno.EIO:
                logger.warning("[NXT/serial] EIO — BT rozłączony")
                break
            else:
                logger.debug(f"[NXT/serial] read error: {e}")
                time.sleep(0.1)
        except Exception as e:
            logger.debug(f"[NXT/serial] reader error: {e}")
            time.sleep(0.1)
    logger.debug("[NXT/serial] reader thread stop")


def _connect_serial() -> bool:
    """
    Strategia 1: openConnection() przez IOBluetooth, potem /dev/cu.RoboC.
    Zwraca True jeśli sukces.
    """
    global _fd, _device, _reader_thread

    if not IOBLUETOOTH_AVAILABLE:
        return False

    logger.info(f"[NXT/serial] Próba połączenia przez {NXT_DEV}")

    # Pobierz urządzenie (już po _force_bt_reset w connect(), więc powinno być odłączone)
    _device = IOBluetoothDevice.deviceWithAddressString_(NXT_MAC)
    if _device is None:
        logger.warning(f"[NXT/serial] Urządzenie {NXT_MAC} nie znalezione")
        return False

    # Otwórz nowe połączenie BT
    logger.info("[NXT/serial] Otwieram połączenie BT (openConnection)...")
    ret = _device.openConnection()
    signed = ret if ret < 0x80000000 else ret - 0x100000000
    logger.info(f"[NXT/serial] openConnection: {signed}")
    # Daj czas na zestawienie połączenia
    time.sleep(2.0)

    # Sprawdź czy /dev/cu.RoboC istnieje
    if not os.path.exists(NXT_DEV):
        logger.warning(f"[NXT/serial] {NXT_DEV} nie istnieje po openConnection")
        return False

    # Otwórz z O_NONBLOCK
    fd = _open_serial_nonblock(NXT_DEV, timeout=8.0)
    if fd is None:
        return False

    _fd = fd

    # Uruchom wątek czytający
    _reader_stop.clear()
    _reader_thread = threading.Thread(target=_reader_thread_func, args=(fd,),
                                      daemon=True, name="NXT-reader")
    _reader_thread.start()

    return True


def _send_raw_serial(cmd: bytes) -> None:
    """Wyślij Direct Command przez /dev/cu.RoboC (z 2-bajtowym nagłówkiem długości)."""
    if _fd is None:
        return
    packet = struct.pack("<H", len(cmd)) + cmd
    logger.debug(f"[NXT/serial] send {len(packet)} bytes: {packet.hex()}")
    try:
        os.write(_fd, packet)
    except OSError as e:
        logger.error(f"[NXT/serial] write error: {e}")


# ===========================================================================
# Strategia 2: IOBluetooth RFCOMM (fallback)
# ===========================================================================

def _make_delegate():
    class _RFCOMMDelegate(NSObject):
        @objc.typedSelector(b"v@:@@S")
        def rfcommChannelData_data_length_(self, channel, data, length):
            raw = bytes(data)
            with _recv_lock:
                _recv_buf.extend(raw)
            _recv_event.set()
            logger.debug(f"[NXT/rfcomm] recv {len(raw)} bytes: {raw.hex()}")

        def rfcommChannelClosed_(self, channel):
            logger.warning("[NXT/rfcomm] RFCOMM channel closed by remote")
            global _channel
            _channel = None

    return _RFCOMMDelegate.alloc().init()


def _bt_thread_func():
    global _bt_runloop
    _bt_runloop = NSRunLoop.currentRunLoop()
    logger.debug("[NXT/rfcomm] BT thread RunLoop started")
    while not _bt_stop.is_set():
        try:
            while True:
                task, result_event, result_holder = _bt_task_queue.get_nowait()
                try:
                    result_holder[0] = task()
                except Exception as e:
                    result_holder[0] = e
                finally:
                    result_event.set()
        except _queue_module.Empty:
            pass
        _bt_runloop.runUntilDate_(NSDate.dateWithTimeIntervalSinceNow_(0.05))
    logger.debug("[NXT/rfcomm] BT thread RunLoop stopped")


def _run_in_bt_thread(task_fn, timeout=10.0):
    result_event = threading.Event()
    result_holder = [None]
    _bt_task_queue.put((task_fn, result_event, result_holder))
    got = result_event.wait(timeout=timeout)
    if not got:
        raise TimeoutError("BT thread task timed out")
    result = result_holder[0]
    if isinstance(result, Exception):
        raise result
    return result


def _ensure_bt_thread():
    global _bt_thread, _bt_stop
    if _bt_thread is not None and _bt_thread.is_alive():
        return
    _bt_stop.clear()
    while not _bt_task_queue.empty():
        try:
            _bt_task_queue.get_nowait()
        except _queue_module.Empty:
            break
    _bt_thread = threading.Thread(target=_bt_thread_func, daemon=True, name="NXT-BT")
    _bt_thread.start()
    time.sleep(0.3)


def _try_open_rfcomm() -> tuple[int, object | None]:
    global _device, _delegate
    if _device.isConnected():
        logger.info("[NXT/rfcomm] Zamykam istniejące połączenie BT")
        _device.closeConnection()
        time.sleep(2.0)
    ret = _device.openConnection()
    logger.debug(f"[NXT/rfcomm] openConnection: {ret:#010x}")
    time.sleep(0.3)
    result, ch = _device.openRFCOMMChannelSync_withChannelID_delegate_(
        None, RFCOMM_CHANNEL_ID, _delegate
    )
    signed_result = result if result < 0x80000000 else result - 0x100000000
    is_open = ch is not None and ch.isOpen()
    logger.debug(
        f"[NXT/rfcomm] openRFCOMMChannelSync result={signed_result} "
        f"ch={ch} isOpen={is_open}"
    )

    # result=0 → sukces; result=AlreadyExists (-536870212) → kanał już istniał
    # w obu przypadkach ch może być ważnym kanałem — użyj go
    _ERR_ALREADY_EXISTS_SIGNED = -536870212
    if ch is not None and (result == 0 or signed_result == _ERR_ALREADY_EXISTS_SIGNED):
        if not is_open:
            # Poczekaj na otwarcie kanału (macOS może potrzebować chwili)
            logger.info("[NXT/rfcomm] isOpen=False — czekam na waitforChanneOpen...")
            wait_ret = ch.waitforChanneOpen()
            wait_signed = wait_ret if wait_ret < 0x80000000 else wait_ret - 0x100000000
            is_open_after = ch.isOpen()
            logger.info(
                f"[NXT/rfcomm] waitforChanneOpen={wait_signed} "
                f"isOpen={is_open_after}"
            )
            is_open = is_open_after
        logger.info(f"[NXT/rfcomm] Akceptuję kanał (result={signed_result}, isOpen={is_open})")
        return 0, ch

    logger.warning(f"[NXT/rfcomm] openRFCOMMChannelSync failed: result={signed_result}, ch={ch}")
    return signed_result, None


def _connect_rfcomm() -> bool:
    """Strategia 2: IOBluetooth RFCOMM channel (fallback)."""
    global _channel, _device, _delegate

    if not IOBLUETOOTH_AVAILABLE:
        return False

    logger.info("[NXT/rfcomm] Próba połączenia przez IOBluetooth RFCOMM")
    _ensure_bt_thread()

    try:
        _device = IOBluetoothDevice.deviceWithAddressString_(NXT_MAC)
        if _device is None:
            return False

        _device.openConnection()
        _delegate = _make_delegate()

        result, ch = _run_in_bt_thread(_try_open_rfcomm, timeout=15.0)
        if ch is None:
            return False

        _channel = ch
        logger.info(f"[NXT/rfcomm] Kanał RFCOMM otwarty: isOpen={_channel.isOpen()}")
        return True

    except Exception as e:
        logger.warning(f"[NXT/rfcomm] connect failed: {e}", exc_info=True)
        _channel = None
        return False


def _send_raw_rfcomm(cmd: bytes) -> None:
    """Wyślij Direct Command przez RFCOMM channel (z 2-bajtowym nagłówkiem)."""
    if not _channel:
        return
    packet = struct.pack("<H", len(cmd)) + cmd
    logger.debug(f"[NXT/rfcomm] send {len(packet)} bytes: {packet.hex()}")
    _channel.writeSync_length_(packet, len(packet))


# ===========================================================================
# Wspólna warstwa — routing do aktywnej strategii
# ===========================================================================

def _is_connected() -> bool:
    # isOpen() na macOS Tahoe zwraca False nawet gdy kanał jest aktywny —
    # używamy samej obecności _channel jako wskaźnika połączenia
    return _fd is not None or _channel is not None


def _send_raw(cmd: bytes) -> None:
    if _fd is not None:
        _send_raw_serial(cmd)
    elif _channel is not None:
        _send_raw_rfcomm(cmd)
    else:
        logger.debug(f"[NXT] (stub) send {cmd.hex()}")


# ===========================================================================
# Publiczne API
# ===========================================================================

def _force_bt_reset(device) -> None:
    """
    Wymuś pełne zamknięcie połączenia BT przed ponownym połączeniem.
    Problem: macOS nie usuwa /dev/cu.RoboC nawet po closeConnection() — node
    zostaje jako "zombie" i NXT nie inicjalizuje nowej sesji SPP gdy go otwieramy.
    Fix: blueutil --disconnect wymusza rozłączenie na poziomie OS → macOS usuwa node.
    """
    import subprocess

    # Krok 1: blueutil --disconnect (wymusza usunięcie /dev/cu.RoboC przez OS)
    logger.info(f"[NXT] blueutil --disconnect {NXT_MAC}...")
    try:
        subprocess.run(
            ["blueutil", "--disconnect", NXT_MAC],
            timeout=5, capture_output=True
        )
    except Exception as e:
        logger.warning(f"[NXT] blueutil failed: {e}")

    # Krok 2: IOBluetooth closeConnection jako fallback
    try:
        if device is not None and device.isConnected():
            logger.info("[NXT] closeConnection przez IOBluetooth...")
            device.closeConnection()
    except Exception as e:
        logger.warning(f"[NXT] closeConnection error: {e}")

    # Krok 3: czekaj aż /dev/cu.RoboC zniknie (max 8s)
    if os.path.exists(NXT_DEV):
        logger.info(f"[NXT] Czekam aż {NXT_DEV} zniknie (max 8s)...")
        for _ in range(16):
            time.sleep(0.5)
            if not os.path.exists(NXT_DEV):
                logger.info(f"[NXT] {NXT_DEV} zniknął — można połączyć od nowa")
                return
        logger.warning(f"[NXT] {NXT_DEV} nadal istnieje po 8s — kontynuuję mimo to")
    else:
        logger.info(f"[NXT] {NXT_DEV} nie istnieje — brak reset potrzebny")


def connect() -> bool:
    """
    Połącz z NXT przez IOBluetooth RFCOMM (strategia 2).
    
    Strategia 1 (/dev/cu.RoboC) porzucona — /dev/cu.RoboC istnieje permanentnie
    po sparowaniu niezależnie od stanu SPP. openConnection() zestawia tylko ACL link,
    nie otwiera kanału RFCOMM/SPP. NXT nie widzi połączenia i ignoruje komendy.
    
    Strategia 2 (IOBluetooth openRFCOMMChannelSync) faktycznie otwiera kanał SPP
    i NXT widzi połączenie (ikona BT na ekranie).
    """
    global _fd, _channel

    if not IOBLUETOOTH_AVAILABLE:
        logger.warning("[NXT] IOBluetooth not available")
        return False

    # Pobierz urządzenie i wyczyść zombie state
    device = IOBluetoothDevice.deviceWithAddressString_(NXT_MAC)
    if device is not None:
        _force_bt_reset(device)
        time.sleep(0.5)  # daj macOS chwilę po reset

    # Strategia 2: RFCOMM — z retry
    MAX_RETRIES = 3
    for attempt in range(1, MAX_RETRIES + 1):
        logger.info(f"[NXT] RFCOMM próba {attempt}/{MAX_RETRIES}")
        if _connect_rfcomm():
            logger.info("[NXT] Połączono przez IOBluetooth RFCOMM")
            _verify_connection()
            _start_keepalive()
            return True
        if attempt < MAX_RETRIES:
            logger.warning(f"[NXT] RFCOMM nieudane (próba {attempt}) — retry za 2s")
            time.sleep(2.0)

    logger.warning("[NXT] Wszystkie próby połączenia nieudane")
    return False


def _verify_connection() -> None:
    """
    Wyślij KEEP_ALIVE (no-response) jako ping — tylko żeby sprawdzić czy zapis działa.
    Nie używamy response=True żeby nie zaśmiecać bufora odczytu odpowiedziami NXT.
    """
    cmd = bytes([0x80, 0x0D])  # no-response direct command, KEEP_ALIVE
    _send_raw(cmd)
    logger.info("[NXT] Ping KEEP_ALIVE wysłany (no-response)")


def disconnect() -> None:
    global _fd, _channel, _device, _bt_stop
    _stop_keepalive()

    # Strategia 1
    if _reader_stop is not None:
        _reader_stop.set()
    if _fd is not None:
        try:
            os.close(_fd)
        except OSError:
            pass
        _fd = None

    # Strategia 2
    _bt_stop.set()
    try:
        if _channel:
            _channel.closeChannel()
            _channel = None
        if _device:
            _device.closeConnection()
            _device = None
    except Exception:
        pass


def _start_keepalive():
    global _keepalive_timer
    _stop_keepalive()

    def _ka():
        global _keepalive_timer
        if _is_connected():
            try:
                # KEEP_ALIVE no-response (0x80, 0x0D) — nie generuje odpowiedzi od NXT,
                # nie zaśmieca bufora, nie powoduje rozłączenia przez niekonsumowane ACK.
                cmd = bytes([0x80, 0x0D])
                _send_raw(cmd)
                logger.debug("[NXT] keepalive sent")
            except Exception as e:
                logger.debug(f"[NXT] keepalive error: {e}")
        _keepalive_timer = threading.Timer(5.0, _ka)
        _keepalive_timer.daemon = True
        _keepalive_timer.start()

    _keepalive_timer = threading.Timer(5.0, _ka)
    _keepalive_timer.daemon = True
    _keepalive_timer.start()


def _stop_keepalive():
    global _keepalive_timer
    if _keepalive_timer:
        _keepalive_timer.cancel()
        _keepalive_timer = None


def _set_output_state(port: int, power: int) -> None:
    """
    NXT Direct Command: SET_OUTPUT_STATE (0x04) — 12 bajtów.

    Byte 0: command type (0x80 = no-response direct command)
    Byte 1: opcode 0x04
    Byte 2: output port (0=A, 1=B, 2=C)
    Byte 3: power (-100..100, signed byte)
    Byte 4: mode (0x01=MOTORON, 0x00=coast)
    Byte 5: regulation mode (0x00=IDLE)
    Byte 6: turn ratio (signed byte, 0 = straight)
    Byte 7: run state (0x20=RUNNING, 0x00=IDLE)
    Bytes 8-11: tacho limit (u32 LE, 0 = run forever)
    """
    if not _is_connected():
        logger.debug(f"[NXT] (stub) SET_OUTPUT_STATE port={port} power={power}")
        return

    if power == 0:
        mode, reg, run = 0x00, 0x00, 0x00   # coast/IDLE
    else:
        mode, reg, run = 0x01, 0x00, 0x20   # MOTORON, IDLE reg, RUNNING

    power_byte = struct.pack('b', max(-100, min(100, power)))[0]

    cmd = bytes([
        0x80,           # no-response Direct Command
        0x04,           # SET_OUTPUT_STATE
        port & 0xFF,
        power_byte,
        mode,
        reg,
        0x00,           # turn ratio
        run,
        0x00, 0x00, 0x00, 0x00,  # tacho limit u32 LE
    ])
    logger.debug(f"[NXT] SET_OUTPUT_STATE port={port} power={power} run={run:#04x}")
    _send_raw(cmd)


def _run_motors(left_power: int, right_power: int) -> None:
    if not _is_connected():
        logger.debug(f"[NXT] (stub) left={left_power} right={right_power}")
        return
    try:
        _set_output_state(_PORT_B, left_power)
        time.sleep(0.02)
        _set_output_state(_PORT_C, right_power)
    except Exception as e:
        logger.error(f"[NXT] Motor error: {e}")


def stop() -> None:
    global _stop_timer
    if _stop_timer:
        _stop_timer.cancel()
        _stop_timer = None
    _run_motors(0, 0)


def _move(left: int, right: int, duration_ms: int) -> None:
    global _stop_timer
    with _lock:
        if _stop_timer:
            _stop_timer.cancel()
        _run_motors(left, right)
        _stop_timer = threading.Timer(duration_ms / 1000.0, stop)
        _stop_timer.daemon = True
        _stop_timer.start()


def set_speed_mode(mode: str) -> None:
    """Ustaw tryb prędkości: 'slow' (0.75x) lub 'fast' (1.0x, domyślny)."""
    global _speed_multiplier
    if mode == "slow":
        _speed_multiplier = 0.75
        logger.info("[NXT] Tryb prędkości: wolno (0.75x)")
    else:
        _speed_multiplier = 1.0
        logger.info("[NXT] Tryb prędkości: normalnie (1.0x)")


def execute(cmd: dict) -> None:
    """
    Execute a move command dict asynchronicznie (stop przez Timer po duration_ms).
      {"move": "forward", "duration_ms": 2000}
      {"move": "set_speed", "mode": "slow"}
    """
    action = cmd.get("move", "stop")

    # Obsługa zmiany prędkości
    if action == "set_speed":
        set_speed_mode(cmd.get("mode", "fast"))
        return

    duration = int(cmd.get("duration_ms", 1000))
    speed = int(round(int(cmd.get("speed", DEFAULT_SPEED)) * _speed_multiplier))
    turn_speed = int(round(int(cmd.get("turn_speed", TURN_SPEED)) * _speed_multiplier))
    speed = max(0, min(100, speed))
    turn_speed = max(0, min(100, turn_speed))

    logger.info(f"[NXT] Execute: {action} for {duration}ms speed={speed}")

    if action == "forward":
        _move(speed, speed, duration)
    elif action == "backward":
        _move(-speed, -speed, duration)
    elif action == "turn_left":
        _move(-turn_speed, turn_speed, duration)
    elif action == "turn_right":
        _move(turn_speed, -turn_speed, duration)
    elif action == "stop":
        stop()
    else:
        logger.warning(f"[NXT] Unknown action: {action}")


def execute_sync(cmd: dict) -> None:
    """
    Execute a move command i CZEKAJ aż ruch się zakończy (blokujące).
    Używane przez agenta ReAct żeby każdy krok miał pełny czas na wykonanie
    zanim agent zrobi kolejny snapshot i zdecyduje o następnej akcji.
      {"action": "forward", "duration_ms": 2000}
      {"action": "set_speed", "mode": "slow"}
    Obsługuje zarówno klucz "action" (agent) jak i "move" (legacy).
    """
    # Agent używa klucza "action", legacy używa "move"
    action = cmd.get("action") or cmd.get("move", "stop")

    if action == "set_speed":
        set_speed_mode(cmd.get("mode", "fast"))
        return

    if action == "done" or action == "none" or action == "stop":
        stop()
        return

    duration = int(cmd.get("duration_ms", 1000))
    speed = int(round(int(cmd.get("speed", DEFAULT_SPEED)) * _speed_multiplier))
    turn_speed = int(round(int(cmd.get("turn_speed", TURN_SPEED)) * _speed_multiplier))
    speed = max(0, min(100, speed))
    turn_speed = max(0, min(100, turn_speed))

    logger.info(f"[NXT] ExecuteSync: {action} for {duration}ms speed={speed}")

    # Uruchom silniki bezpośrednio (bez Timer — sami czekamy)
    with _lock:
        if _stop_timer:
            _stop_timer.cancel()
        if action == "forward":
            _run_motors(speed, speed)
        elif action == "backward":
            _run_motors(-speed, -speed)
        elif action == "turn_left":
            _run_motors(-turn_speed, turn_speed)
        elif action == "turn_right":
            _run_motors(turn_speed, -turn_speed)
        else:
            logger.warning(f"[NXT] ExecuteSync unknown action: {action}")
            return

    # Czekaj na zakończenie ruchu
    time.sleep(duration / 1000.0)
    stop()


def test_spin() -> None:
    """
    Test silników: obrót ~180° w lewo, pauza, obrót ~180° w prawo.
    Uruchamiany automatycznie po udanym connect().
    Dostosuj SPIN_DURATION_MS jeśli kąt jest za mały/za duży.
    """
    SPIN_SPEED = 70
    SPIN_DURATION_MS = 1800   # ~180° (dostosuj!)

    if not _is_connected():
        logger.warning("[NXT] test_spin: brak połączenia")
        return

    logger.info(f"[NXT] test_spin: obrót w lewo {SPIN_DURATION_MS}ms @ speed={SPIN_SPEED}")
    _run_motors(-SPIN_SPEED, SPIN_SPEED)
    time.sleep(SPIN_DURATION_MS / 1000.0)
    _run_motors(0, 0)

    logger.info("[NXT] test_spin: pauza 1s")
    time.sleep(1.0)

    logger.info(f"[NXT] test_spin: obrót w prawo {SPIN_DURATION_MS}ms")
    _run_motors(SPIN_SPEED, -SPIN_SPEED)
    time.sleep(SPIN_DURATION_MS / 1000.0)
    _run_motors(0, 0)

    logger.info("[NXT] test_spin: gotowe")
