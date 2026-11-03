#!/usr/bin/env bash
# Start ROVER backend
# Usage:
#   ./start.sh                  # z lokalnym odtwarzaniem audio (default)
#   ./start.sh --no-local-audio # bez odtwarzania na backendzie
set -e
cd "$(dirname "$0")"

# Parsuj argumenty
LOCAL_PLAYBACK=1
for arg in "$@"; do
    case $arg in
        --no-local-audio) LOCAL_PLAYBACK=0 ;;
    esac
done

# Zatrzymaj poprzedni proces na porcie 8000 gracefully (SIGTERM → poczekaj → SIGKILL)
# SIGTERM daje backendowi czas na disconnect() i zamknięcie kanału RFCOMM przed wyjściem.
# kill -9 zostawia "zombie" BT connection w macOS i NXT nie przyjmuje kolejnego połączenia.
if lsof -ti:8000 &>/dev/null; then
    PIDS=$(lsof -ti:8000)
    echo "Zatrzymuję poprzedni proces (PIDs: $PIDS) przez SIGTERM..."
    echo "$PIDS" | xargs kill -TERM 2>/dev/null || true
    # Czekaj do 5s na zakończenie
    for i in $(seq 1 10); do
        sleep 0.5
        if ! lsof -ti:8000 &>/dev/null; then
            echo "Proces zakończony."
            break
        fi
    done
    # Jeśli nadal żyje — dopiero wtedy SIGKILL
    if lsof -ti:8000 &>/dev/null; then
        echo "Nie zakończył się — SIGKILL..."
        lsof -ti:8000 | xargs kill -9 2>/dev/null || true
        sleep 1
    fi
    # Daj macOS chwilę na faktyczne zamknięcie kanału RFCOMM w stosie BT
    echo "Czekam 2s na zamknięcie stosu BT..."
    sleep 2
fi

echo "Odtwarzanie lokalne: $([ $LOCAL_PLAYBACK -eq 1 ] && echo 'WŁĄCZONE' || echo 'WYŁĄCZONE')"
exec env LOCAL_PLAYBACK=$LOCAL_PLAYBACK venv/bin/python3 main.py
