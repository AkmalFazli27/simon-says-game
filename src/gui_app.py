import tkinter as tk
from tkinter import messagebox
from paho.mqtt import client as mqtt_client
import threading
import json
import uuid  # SARAN: Gunakan UUID untuk Client ID yang benar-benar unik

# BROKER = "test.mosquitto.org"
BROKER = "broker.hivemq.com"  # SARAN: HiveMQ lebih stabil untuk testing
PORT = 1883
TOPIC_COMMAND = "simon/command"
TOPIC_STATUS = "simon/status"
# SARAN: Menggunakan UUID untuk mencegah tabrakan ID
CLIENT_ID = f"simon_gui_{uuid.uuid4()}"


class SimonSaysGUI:
    """
    GUI permainan Simon Says dengan komunikasi MQTT ke ESP32.
    """

    # KOREKSI: Konstruktor menggunakan __init__ (dua garis bawah)
    def __init__(self, root, broker: str, port: int, client_id: str):
        self.root = root
        self.broker = broker
        self.port = port
        self.client_id = client_id
        self.client = None

        self._setup_widgets()
        self._connect_mqtt()

    def _setup_widgets(self):
        self.root.title("Simon Says - MQTT GUI")
        # ... (Tidak ada perubahan di sini, sudah sangat baik) ...
        frame = tk.Frame(self.root)
        frame.pack(pady=10)
        colors = ["green", "red", "yellow", "blue"]
        self.color_buttons = {}
        for i, color in enumerate(colors):
            btn = tk.Button(
                frame,
                text=color.capitalize(),
                bg=color,
                fg="white" if (color != "yellow") else "black",
                width=12,
                height=3,
                state="disabled",
                command=lambda c=color: self._handle_color_press(c),
            )
            r, c = divmod(i, 2)
            btn.grid(row=r, column=c, padx=5, pady=5)
            self.color_buttons[color] = btn
        self.start_btn = tk.Button(
            self.root, text="Start Game", width=12, command=self._handle_start_press
        )
        self.start_btn.pack(pady=10)
        self.status_lbl = tk.Label(
            self.root, text="Connecting to MQTT...", font=("Arial", 12)
        )
        self.status_lbl.pack(pady=5)

    def _on_connect(self, client, userdata, flags, rc, properties=None):
        """Callback saat koneksi ke broker berhasil."""
        if rc == 0:
            print("Terhubung ke MQTT Broker!")
            client.subscribe(TOPIC_STATUS)
            # SARAN: Mengirim pesan status awal UI dari sini
            self.root.after(0, lambda: self.update_ui_state("LOBBY"))
        else:
            print(f"Gagal terhubung, return code {rc}\n")

    def _connect_mqtt(self):
        self.client = mqtt_client.Client(
            client_id=self.client_id,
            callback_api_version=mqtt_client.CallbackAPIVersion.VERSION2,
        )
        self.client.on_connect = self._on_connect
        self.client.on_message = self._on_mqtt_message

        try:
            self.client.connect(self.broker, self.port)
        except Exception as e:
            messagebox.showerror(
                "Connection Error", f"Gagal terhubung ke broker MQTT:\n{e}"
            )
            return

        # SARAN: Gunakan loop_start() yang non-blocking dan mengelola thread secara otomatis
        self.client.loop_start()

    def _on_mqtt_message(self, client, userdata, msg):
        try:
            payload = msg.payload.decode()
            data = json.loads(payload)
            print(f"Menerima status: {data}")  # Bermanfaat untuk debugging
        except Exception as e:
            print(f"Gagal mem-parsing payload: {msg.payload.decode()} | Error: {e}")
            return

        state = data.get("state", "LOBBY")
        score = data.get("score", 0)
        self.root.after(0, lambda: self.update_ui_state(state, score))

    def _publish_command(self, action: str, value: str = ""):
        if not self.client or not self.client.is_connected():
            print("MQTT belum siap atau terputus.")
            return
        # SARAN: Menggunakan format payload yang lebih fleksibel
        payload = json.dumps({"action": action, "value": value})
        self.client.publish(TOPIC_COMMAND, payload)
        print(f"Mengirim perintah: {payload}")  # Bermanfaat untuk debugging

    def _handle_start_press(self):
        self._publish_command("start")

    def _handle_color_press(self, color: str):
        self._publish_command("input", color)

    def update_ui_state(self, state: str, score: int = 0):
        # Update label status
        self.status_lbl.config(
            text=f"State: {state.replace('_', ' ')} | Score: {score}"
        )

        if state in ["LOBBY", "GAME_OVER"]:
            self.start_btn.config(state="normal", text="Start Game")
            for btn in self.color_buttons.values():
                btn.config(state="disabled")

        elif state == "DISPLAYING_PATTERN":
            # PENINGKATAN: Menangani state saat ESP32 menampilkan pola
            self.start_btn.config(state="disabled")
            for btn in self.color_buttons.values():
                btn.config(state="disabled")

        elif state == "WAITING_FOR_INPUT":
            self.start_btn.config(state="disabled")
            for btn in self.color_buttons.values():
                btn.config(state="normal")

        if state == "GAME_OVER":
            # Menampilkan messagebox hanya sekali saat transisi ke GAME_OVER
            messagebox.showinfo("Game Over", f"Skor akhir Anda: {score}")


def main():
    root = tk.Tk()
    app = SimonSaysGUI(root, BROKER, PORT, CLIENT_ID)
    root.mainloop()


# KOREKSI: Entry point menggunakan __main__
if __name__ == "__main__":
    main()
