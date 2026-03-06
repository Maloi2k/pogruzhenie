#!/usr/bin/env python3
import os
import shlex
import signal
import socket
import subprocess
import time
import serial

DEBUG = True


def debug(msg):
    if DEBUG:
        print(msg, flush=True)

def hide_cursor():
    try:
        subprocess.Popen(
            ["unclutter", "-display", ":0", "-idle", "0", "-root"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
    except Exception:
        pass


# ===== RS-485 =====
SERIAL_DEV = "/dev/ttyUSB0"  # если не делал udev — поставь "/dev/ttyUSB0"
BAUDRATE = 9600

# ===== Видео =====
BLACK_VIDEO = "/home/pi/videos/black.mp4"
VIDEO_TEMPLATE = "/home/pi/videos/{n}.mp4"  # 1.mp4, 2.mp4, ...

# ===== Адрес ноды =====
NODE_ID = 1  # на каждой Raspberry своё число

# ===== VLC RC (локальное управление) =====
RC_HOST = "127.0.0.1"
RC_PORT = 4212

# Старые команды (на всякий случай для совместимости)
LEGACY_MAP = {
    "PLAY1": 1,
    "PLAY2": 2,
}

def _kill_proc_group(p: subprocess.Popen | None):
    if not p:
        return
    if p.poll() is not None:
        return
    try:
        os.killpg(os.getpgid(p.pid), signal.SIGTERM)
        p.wait(timeout=2)
    except Exception:
        try:
            os.killpg(os.getpgid(p.pid), signal.SIGKILL)
        except Exception:
            pass


def _start_vlc_rc() -> subprocess.Popen:
    """
    Запускаем VLC ОДИН раз и дальше управляем им по RC.
    Это убирает "чёрный провал" при переключениях, потому что процесс/вывод не пересоздаются.
    """
    # ВАЖНО: звук включён (нет --no-audio)
    cmd = (
        f"cvlc --fullscreen --no-video-title-show --no-osd "
        f"--extraintf rc --rc-host {RC_HOST}:{RC_PORT} "
        f"{shlex.quote(BLACK_VIDEO)}"
    )
    return subprocess.Popen(
        cmd,
        shell=True,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        preexec_fn=os.setsid,
    )


def _rc_connect(timeout=0.4):
    return socket.create_connection((RC_HOST, RC_PORT), timeout=timeout)


def _rc_send(lines: list[str], retries: int = 2) -> None:
    """
    Шлём команды VLC RC.
    Если порт временно не отвечает — пробуем ещё раз.
    """
    last_err = None
    for _ in range(retries + 1):
        try:
            with _rc_connect() as s:
                for line in lines:
                    s.sendall((line + "\n").encode("utf-8"))
            return
        except Exception as e:
            last_err = e
            time.sleep(0.08)
    raise last_err  # noqa


def _rc_request(line: str, timeout=0.5) -> str:
    """
    Запрос с чтением ответа (нужен для status).
    RC иногда шлёт приглашения/лишние строки — нам хватит простого текста.
    """
    with _rc_connect(timeout=timeout) as s:
        s.settimeout(timeout)
        s.sendall((line + "\n").encode("utf-8"))
        chunks = []
        t0 = time.time()
        while time.time() - t0 < timeout:
            try:
                data = s.recv(4096)
                if not data:
                    break
                chunks.append(data.decode("utf-8", errors="ignore"))
                # обычно status приходит сразу
                if "state" in "".join(chunks).lower():
                    break
            except socket.timeout:
                break
        return "".join(chunks)


class PlayerRC:
    def __init__(self):
        self.vlc: subprocess.Popen | None = None
        self.play_once_active = False  # мы запустили "один раз" и ждём окончания

    def ensure_vlc(self):
        """
        Автовосстановление VLC:
        - если процесс умер
        - или RC-порт не отвечает
        """
        if self.vlc is None or self.vlc.poll() is not None:
            self._restart_vlc()
            return

        # Проверка, что RC живой
        try:
            _rc_send(["status"], retries=0)
        except Exception:
            self._restart_vlc()

    def _restart_vlc(self):
        _kill_proc_group(self.vlc)
        self.vlc = _start_vlc_rc()

        # ждём, пока поднимется RC
        for _ in range(30):
            try:
                _rc_send(["status"], retries=0)
                # после рестарта сразу уходим в black "вечно"
                self.black()
                return
            except Exception:
                time.sleep(0.1)

        # если всё совсем плохо — оставим как есть, следующий ensure попробует снова
        self.play_once_active = False

    def black(self):
        self.ensure_vlc()
        self.play_once_active = False

        _rc_send(["stop"], retries=2)
        time.sleep(0.05)
        _rc_send(["clear"], retries=2)
        time.sleep(0.05)
        _rc_send([f"add {BLACK_VIDEO}"], retries=2)
        time.sleep(0.05)
        _rc_send(
            [
                "repeat on",
                "loop off",
                "play",
            ],
            retries=2,
        )

    def play(self, n: int, loop: bool):
        self.ensure_vlc()
        path = VIDEO_TEMPLATE.format(n=n)

        if not os.path.isfile(path):
            self.black()
            return

        _rc_send(["stop"], retries=2)
        time.sleep(0.05)
        _rc_send(["clear"], retries=2)
        time.sleep(0.05)
        _rc_send([f"add {path}"], retries=2)
        time.sleep(0.05)

        if loop:
            self.play_once_active = False
            _rc_send(
                [
                    "repeat on",
                    "loop off",
                    "play",
                ],
                retries=2,
            )
        else:
            self.play_once_active = True
            _rc_send(
                [
                    "repeat off",
                    "loop off",
                    "play",
                ],
                retries=2,
            )

    def tick(self):
        self.ensure_vlc()

        if not self.play_once_active:
            return

        try:
            st = _rc_request("status", timeout=0.35).lower()

            if "state stopped" in st or "state end" in st or "state ended" in st:
                self.black()
                return

            # дополнительная защита:
            # если somehow включился repeat, принудительно выключаем
            if "repeat: on" in st:
                _rc_send(["repeat off", "loop off"], retries=1)

        except Exception:
            pass


def parse_line(line: str):
    """
    Новый формат:
      "<ADDR> <CMD> [ARGS...]"
    Примеры:
      "1 PLAY 2 LOOP"
      "3 PLAY 5"
      "ALL STOP"
    """
    parts = line.strip().split()
    if len(parts) < 2:
        return None

    addr_raw = parts[0].upper()
    cmd = parts[1].upper()
    args = parts[2:]

    if addr_raw == "ALL":
        addr = "ALL"
    else:
        try:
            addr = int(addr_raw)
        except ValueError:
            return None

    return addr, cmd, args


def main():
    hide_cursor()

    player = PlayerRC()
    player.black()

    while True:
        try:
            with serial.Serial(SERIAL_DEV, BAUDRATE, timeout=0.2) as ser:
                ser.reset_input_buffer()

                while True:
                    # фоновые проверки (возврат в black после ONCE + автоподнятие VLC)
                    player.tick()

                    raw = ser.readline()
                    if not raw:
                        continue

                    line = raw.decode(errors="ignore").strip()
                    if not line:
                        continue

                    debug(f"RS485 RX: {line}")

                    # ===== Legacy (PLAY1/PLAY2) =====
                    up = line.upper()
                    if up in LEGACY_MAP:
                        player.play(LEGACY_MAP[up], loop=False)
                        continue
                    if up in ("STOP", "BLACK", "REBOOT"):
                        # старый формат без адреса — можно считать "ALL" или "только мне"
                        # сделаем: только мне (чтобы на общей шине не ломало всех)
                        if up == "REBOOT":
                            subprocess.Popen(["sudo", "reboot"])
                            time.sleep(0.05)
                        else:
                            player.black()
                        continue

                    # ===== New protocol =====
                    parsed = parse_line(line)
                    if not parsed:
                        continue
                    addr, cmd, args = parsed

                    # фильтр по адресу
                    if addr != "ALL" and addr != NODE_ID:
                        continue

                    if cmd == "PLAY":
                        if not args:
                            continue
                        try:
                            n = int(args[0])
                        except ValueError:
                            continue
                        mode = args[1].upper() if len(args) >= 2 else "ONCE"
                        loop = mode == "LOOP"
                        player.play(n, loop=loop)

                    elif cmd in ("STOP", "BLACK"):
                        player.black()

                    elif cmd == "REBOOT":
                        subprocess.Popen(["sudo", "reboot"])
                        time.sleep(0.05)

        except serial.SerialException:
            # порт временно пропал — держим black, пробуем снова
            player.black()
            time.sleep(0.5)
        except Exception:
            player.black()
            time.sleep(0.5)


if __name__ == "__main__":
    main()
