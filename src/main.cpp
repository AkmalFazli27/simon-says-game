#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include "secrets.h"
#include <vector>
#include <string>
#include <random>
#include <map>
#include <ArduinoJson.h>
using namespace std;

// --- Konfigurasi Jaringan & MQTT ---
const char* ssid = SECURE_SSID;
const char* password = SECURE_PASSWORD;
const char* mqtt_server = "broker.hivemq.com";
const char* topic_command = "simon/command";
const char* topic_status = "simon/status";
const char* client_id = "esp32-simon-game-master-wokwi-v2"; // Pastikan unik

WiFiClient espClient;
PubSubClient client(espClient);

// --- Konfigurasi Hardware ---
std::map<std::string, int> ledPins = {
    {"red", 23},
    {"green", 19},
    {"blue", 5},
    {"yellow", 21}
};

// --- Deklarasi Fungsi (Forward Declaration) ---
void publish_status();
void start_sequence_animation();
void start_win_animation();
void start_lose_animation();
void callback(char* topic, byte* payload, unsigned int length);
void handle_wifi_and_mqtt();
void handle_game_logic();
void handle_animations();

// =========================================================================
// KELAS LOGIKA PERMAINAN (Sama seperti sebelumnya, tidak ada perubahan)
// =========================================================================
class SimonGame {
static const std::vector<std::string> colors;
public:
  enum class State { IDLE, PLAYING, GAME_OVER };

  SimonGame() 
    : round(0), player_input_idx(0), state(State::IDLE),
      rng(random_device{}()), dist(0, (int)colors.size()-1) {}

  bool start_game() {
    if (state == State::PLAYING) return false;
    sequence.clear(); 
    round = 0; 
    player_input_idx = 0; 
    state = State::PLAYING;
    next_round(); 
    return true;
  }

  std::vector<std::string> next_round() {
    sequence.push_back(colors[dist(rng)]);
    round++; 
    player_input_idx = 0; 
    return sequence;
  }

  std::string validate(const std::string& ans) {
    if (state != State::PLAYING || sequence.empty() || player_input_idx >= sequence.size()) {
      state = State::GAME_OVER;
      return "WRONG";
    }
    if (ans != sequence[player_input_idx]) { 
      state = State::GAME_OVER; 
      return "WRONG"; 
    }
    player_input_idx++; 
    return (player_input_idx == sequence.size() ? "ROUND_COMPLETE" : "CORRECT");
  }
 
  void reset_to_idle() {
    state = State::IDLE;
    round = 0;
    player_input_idx = 0;
    sequence.clear();
  }

  std::string get_state_string() const {
    switch(state){
      case State::IDLE: return "IDLE";
      case State::PLAYING: return "WAITING_FOR_INPUT";
      default: return "GAME_OVER";
    }
  }

  State get_state_enum() const { return state; }
  const std::vector<std::string>& get_sequence() const { return sequence; }
  int get_round() const { return round; }

private:
  std::vector<std::string> sequence;
  int round;
  size_t player_input_idx;
  State state;
  std::mt19937 rng;
  std::uniform_int_distribution<int> dist;
};

const std::vector<std::string> SimonGame::colors = {"red", "green", "blue", "yellow"};

// --- Global State Management ---
SimonGame game;
SimonGame::State last_published_state = SimonGame::State::GAME_OVER;

// Variabel untuk koneksi non-blocking
unsigned long last_mqtt_reconnect_attempt = 0;

// Variabel untuk state machine animasi non-blocking
enum class AnimationType { NONE, SEQUENCE, WIN, LOSE };
AnimationType current_animation = AnimationType::NONE;
unsigned long animation_start_time = 0;
unsigned long last_step_time = 0;
int animation_step = 0;

// =========================================================================
// FUNGSI SETUP & LOOP UTAMA
// =========================================================================
void setup() {
  Serial.begin(115200);
  
  for (const auto& pair : ledPins) {
    pinMode(pair.second, OUTPUT);
  }

  // Mulai koneksi WiFi (tanpa menunggu/blocking)
  WiFi.begin(ssid, password);
  Serial.print("Mencoba menghubungkan ke WiFi...");

  // Konfigurasi MQTT
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
}

void loop() {
  handle_wifi_and_mqtt(); // Mengelola koneksi di latar belakang
  handle_game_logic();    // Mengelola logika state game
  handle_animations();    // Menjalankan semua animasi visual
}

// =========================================================================
// FUNGSI HANDLER UNTUK LOOP()
// =========================================================================

void handle_wifi_and_mqtt() {
    // Jika WiFi tidak terhubung, jangan lakukan apa-apa selain mencoba.
    if (WiFi.status() != WL_CONNECTED) {
        static unsigned long last_wifi_check = 0;
        if (millis() - last_wifi_check > 1000) {
            Serial.print(".");
            last_wifi_check = millis();
        }
        return;
    }

    // Jika WiFi sudah terhubung tapi MQTT belum
    if (!client.connected()) {
        // Coba hubungkan kembali setiap 5 detik (non-blocking)
        if (millis() - last_mqtt_reconnect_attempt > 5000) {
            Serial.print("Mencoba koneksi MQTT...");
            if (client.connect(client_id)) {
                Serial.println("terhubung!");
                client.subscribe(topic_command);
                publish_status(); 
            } else {
                Serial.print("gagal, rc=");
                Serial.println(client.state());
            }
            last_mqtt_reconnect_attempt = millis();
        }
    } else {
        // Jika semua terhubung, panggil client.loop()
        client.loop();
    }
}

void handle_game_logic() {
    SimonGame::State current_game_state = game.get_state_enum();
    
    // Publikasikan status HANYA jika ada perubahan
    if (current_game_state != last_published_state) {
        publish_status();
        last_published_state = current_game_state;
    }

    // State machine untuk reset setelah game over
    if (current_game_state == SimonGame::State::GAME_OVER) {
        static unsigned long game_over_start_time = 0;
        if (game_over_start_time == 0) {
            game_over_start_time = millis();
        }

        // Setelah 3 detik, reset game ke IDLE
        if (millis() - game_over_start_time > 3000) {
            game.reset_to_idle();
            game_over_start_time = 0; // Reset timer
        }
    }
}

void handle_animations() {
    if (current_animation == AnimationType::NONE) {
        return; // Tidak ada animasi yang aktif
    }

    unsigned long current_time = millis();

    switch (current_animation) {
        case AnimationType::SEQUENCE: {
            const auto& seq = game.get_sequence();
            if (animation_step >= seq.size() * 2) { // Tiap warna punya 2 step (ON dan OFF)
                current_animation = AnimationType::NONE;
                publish_status(); // Update status ke WAITING_FOR_INPUT
                return;
            }

            // Interval antara ON dan OFF
            unsigned long interval = (animation_step % 2 == 0) ? 250 : 800; // 250ms OFF, 400ms ON

            if (current_time - last_step_time > interval) {
                int color_index = animation_step / 2;
                std::string color = seq[color_index];
                
                if (animation_step % 2 == 0) { // Step genap: nyalakan LED
                    digitalWrite(ledPins[color], HIGH);
                } else { // Step ganjil: matikan LED
                    digitalWrite(ledPins[color], LOW);
                }
                
                animation_step++;
                last_step_time = current_time;
            }
            break;
        }

        case AnimationType::WIN: {
            if (animation_step >= 6) { // 3 kedipan (3 ON, 3 OFF)
                current_animation = AnimationType::NONE;
                return;
            }
            if (current_time - last_step_time > 100) {
                bool turn_on = (animation_step % 2 == 0);
                for(const auto& pair : ledPins) {
                    digitalWrite(pair.second, turn_on ? HIGH : LOW);
                }
                animation_step++;
                last_step_time = current_time;
            }
            break;
        }

        case AnimationType::LOSE: {
            if (current_time - animation_start_time > 1000) {
                digitalWrite(ledPins["red"], LOW);
                current_animation = AnimationType::NONE;
            }
            break;
        }
        
        default: break;
    }
}

// =========================================================================
// FUNGSI MQTT CALLBACK
// =========================================================================
void callback(char* topic, byte* payload, unsigned int length) {
  // Parsing JSON
  JsonDocument doc;
  deserializeJson(doc, payload, length);
  const char* action = doc["action"];

  if (action == nullptr) return;

  // Jika ada animasi yang berjalan, abaikan input untuk sementara
  if (current_animation != AnimationType::NONE) return;

  if (strcmp(action, "start") == 0 && game.get_state_enum() == SimonGame::State::IDLE) {
    game.start_game();
    start_sequence_animation(); // Memicu animasi urutan
  } 
  else if (strcmp(action, "input") == 0 && game.get_state_enum() == SimonGame::State::PLAYING) {
    const char* value = doc["value"];
    if (value == nullptr) return;

    std::string result = game.validate(value);

    if (result == "ROUND_COMPLETE") {
      start_win_animation();
      game.next_round();
      start_sequence_animation();
    } 
    else if (result == "WRONG") {
      start_lose_animation();
    }
  }
}

// =========================================================================
// FUNGSI Pemicu Animasi & Publikasi
// =========================================================================
void publish_status() {
    JsonDocument doc;
    doc["state"] = game.get_state_string();
    doc["score"] = game.get_round() > 0 ? game.get_round() - 1 : 0;
    
    char buffer[128];
    serializeJson(doc, buffer);
    client.publish(topic_status, buffer, true);
    Serial.print("Status dipublikasikan: ");
    Serial.println(buffer);
}

void start_sequence_animation() {
    // Publikasikan status sementara
    JsonDocument doc;
    doc["state"] = "DISPLAYING_PATTERN";
    char buffer[128];
    serializeJson(doc, buffer);
    client.publish(topic_status, buffer, true);

    // Siapkan state machine animasi
    current_animation = AnimationType::SEQUENCE;
    animation_step = 0;
    last_step_time = millis();
}

void start_win_animation() {
    current_animation = AnimationType::WIN;
    animation_step = 0;
    last_step_time = millis();
}

void start_lose_animation() {
    current_animation = AnimationType::LOSE;
    animation_start_time = millis();
    digitalWrite(ledPins["red"], HIGH); // Langsung nyalakan LED merah
}