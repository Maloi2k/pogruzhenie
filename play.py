#!/usr/bin/env python3
import os
import shlex
import signal
import subprocess
import time

import serial

SERIAL_DEV = "/dev/rs485"   # если не делал udev — поставь "/dev/ttyUSB0"
BAUDRATE = 9600             # как на Arduino

BLACK_VIDEO = "/home/pi/videos/black.mp4"

VIDEOS = {
    "PLAY1": "/home/pi/videos/video1.mp4",
    "PLAY2": "/home/pi/videos/video2.mp4",
}

def start_vlc_loop(filepath: str) -> subprocess.Popen:
    # Зацикленное fullscreen видео (black.mp4)
    cmd = (
        f'cvlc --fullscreen --loop --no-video-title-show --no-osd '
        f'--no-audio {shlex.quote(filepath)}'
    )
    return subprocess.Popen(
        cmd,
        shell=True,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        preexec_fn=os.setsid
    )

def start_vlc_play_once(filepath: str) -> subprocess.Popen:
    # Проиграть 1 раз и завершиться
    cmd = (
        f'cvlc --fullscreen --play-and-exit --no-video-title-show --no-osd '
        f'--no-audio {shlex.quote(filepath)}'
    )
    return subprocess.Popen(
        cmd,
        shell=True,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        preexec_fn=os.setsid
    )

def stop_proc(p: subprocess.Popen | None):
    if not p:
        return
    if p.poll() is not None:
        return
    try:
        # Завершаем всю группу процессов VLC
        os.killpg(os.getpgid(p.pid), signal.SIGTERM)
        p.wait(timeout=3)
    except Exception:
        try:
            os.killpg(os.getpgid(p.pid), signal.SIGKILL)
        except Exception:
            pass

class Player:
    def __init__(self):
        self.black_proc: subprocess.Popen | None = None
        self.video_proc: subprocess.Popen | None = None

    def ensure_black(self):
        # Если black уже крутится — ок
        if self.black_proc and self.black_proc.poll() is None:
            return
        # Останавливаем обычное видео, если вдруг есть
        stop_proc(self.video_proc)
        self.video_proc = None

        if os.path.isfile(BLACK_VIDEO):
            self.black_proc = start_vlc_loop(BLACK_VIDEO)

    def stop_all(self):
        stop_proc(self.video_proc)
        stop_proc(self.black_proc)
        self.video_proc = None
        self.black_proc = None

    def play_video(self, path: str):
        # Переключаемся с black на видео
        stop_proc(self.black_proc)
        self.black_proc = None

        stop_proc(self.video_proc)
        self.video_proc = None

        if os.path.isfile(path):
            self.video_proc = start_vlc_play_once(path)
        else:
            # Если файла нет — возвращаем black
            self.ensure_black()

    def stop_video_and_black(self):
        # По STOP мы хотим вернуться на black-loop
        stop_proc(self.video_proc)
        self.video_proc = None
        self.ensure_black()

def main():
    player = Player()

    # На старте — крутим black-loop
    player.ensure_black()

    while True:
        try:
            with serial.Serial(SERIAL_DEV, BAUDRATE, timeout=1) as ser:
                ser.reset_input_buffer()

                while True:
                    # Если обычное видео само закончилось — вернём black
                    if player.video_proc and player.video_proc.poll() is not None:
                        player.video_proc = None
                        player.ensure_black()

                    raw = ser.readline()
                    if not raw:
                        continue

                    cmd = raw.decode(errors="ignore").strip().upper()
                    if not cmd:
                        continue

                    if cmd in VIDEOS:
                        player.play_video(VIDEOS[cmd])

                    elif cmd == "STOP":
                        player.stop_video_and_black()

                    elif cmd == "BLACK":
                        # принудительно перейти в black (даже если видео играет)
                        player.stop_video_and_black()

                    elif cmd == "REBOOT":
                        # опционально: если хочешь
                        subprocess.Popen(["sudo", "reboot"])

                    time.sleep(0.05)

        except serial.SerialException:
            # порт временно пропал — просто держим black и пытаемся переподключиться
            player.ensure_black()
            time.sleep(1)

        except Exception:
            player.ensure_black()
            time.sleep(1)

if __name__ == "__main__":
    main()