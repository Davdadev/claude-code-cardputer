#include <M5Cardputer.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// Arrow key HID codes (not defined by M5Cardputer library)
#define KEY_UP_ARROW   0x52
#define KEY_DOWN_ARROW 0x51
#define KEY_LEFT_ARROW 0x50
#define KEY_RIGHT_ARROW 0x4F

// ── Colours ──────────────────────────────────────────────
#define COL_BG       TFT_BLACK
#define COL_TEXT     TFT_WHITE
#define COL_ACCENT   0xFD20   // orange
#define COL_DIM      TFT_DARKGREY
#define COL_INPUT    0xFD20   // orange input text
#define COL_REPLY    TFT_CYAN

// ── Settings (editable in settings mode) ─────────────────
String  serverUrl   = "http://192.168.4.181:5001/chat";
int     textSize    = 1;
bool    showStatus  = true;
String  systemPrompt = "";

// ── State ─────────────────────────────────────────────────
String  input      = "";
bool    inSettings = false;
int     inputLineY = 0;   // Y pixel where current input line starts
String  _postBody  = "";  // shared buffer for background HTTP task

// Forward declarations
void redrawInputLine(const String& val, bool masked = false);

// ── Helpers ───────────────────────────────────────────────
void cls() {
  M5Cardputer.Display.fillScreen(COL_BG);
  M5Cardputer.Display.setCursor(0, 0);
}

void header(const char* title) {
  M5Cardputer.Display.setTextColor(COL_ACCENT);
  M5Cardputer.Display.println(title);
  M5Cardputer.Display.setTextColor(COL_DIM);
  M5Cardputer.Display.println("────────────────────");
  M5Cardputer.Display.setTextColor(COL_TEXT);
}

// ── WiFi picker ───────────────────────────────────────────
String pickWifi() {
  cls();
  M5Cardputer.Display.setTextColor(COL_ACCENT);
  M5Cardputer.Display.println("Scanning WiFi...");
  M5Cardputer.Display.setTextColor(COL_TEXT);

  int n = WiFi.scanNetworks();
  if (n == 0) {
    M5Cardputer.Display.println("No networks found.");
    delay(2000);
    return "";
  }

  std::vector<String> ssids;
  for (int i = 0; i < n; i++) {
    String s = WiFi.SSID(i);
    bool dup = false;
    for (auto& e : ssids) { if (e == s) { dup = true; break; } }
    if (!dup) ssids.push_back(s);
  }

  int selected = 0;
  int total    = ssids.size();

  auto drawList = [&]() {
    cls();
    header("Select WiFi");
    M5Cardputer.Display.setTextColor(COL_DIM);
    M5Cardputer.Display.println("Up/Down arrows  Enter: OK");
    M5Cardputer.Display.setTextColor(COL_TEXT);
    int start = max(0, selected - 2);
    int end   = min(total, start + 5);
    for (int i = start; i < end; i++) {
      if (i == selected) {
        M5Cardputer.Display.setTextColor(COL_ACCENT);
        M5Cardputer.Display.print("> ");
      } else {
        M5Cardputer.Display.setTextColor(COL_TEXT);
        M5Cardputer.Display.print("  ");
      }
      M5Cardputer.Display.println(ssids[i]);
    }
    M5Cardputer.Display.setTextColor(COL_TEXT);
  };

  drawList();

  while (true) {
    M5Cardputer.update();
    if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
      Keyboard_Class::KeysState st = M5Cardputer.Keyboard.keysState();
      if (st.fn)  { /* ignore fn combos */ }
      // Arrow keys on Cardputer: KEY_UP = up arrow, KEY_DOWN = down arrow
      bool up   = false, down = false;
      for (auto c : st.word) {
        if (c == ';') up   = true;   // fn+, maps to up on some firmware
        if (c == '.') down = true;
      }
      // Also check KEY_ codes via the raw key list
      for (auto k : st.hid_keys) {
        if (k == KEY_UP_ARROW)   up   = true;
        if (k == KEY_DOWN_ARROW) down = true;
      }
      if (up   && selected > 0)        { selected--; drawList(); }
      if (down && selected < total - 1) { selected++; drawList(); }
      if (st.enter) return ssids[selected];
    }
    delay(10);
  }
}

String enterPassword(String ssid) {
  cls();
  header("Password");
  M5Cardputer.Display.setTextColor(COL_TEXT);
  M5Cardputer.Display.println(ssid);
  M5Cardputer.Display.setTextColor(COL_DIM);
  M5Cardputer.Display.println("Backspace: del  Enter: OK");
  M5Cardputer.Display.setTextColor(COL_INPUT);
  M5Cardputer.Display.print("> ");

  inputLineY = M5Cardputer.Display.getCursorY();
  M5Cardputer.Display.setTextColor(COL_ACCENT);
  M5Cardputer.Display.print("> ");

  String pwd = "";
  while (true) {
    M5Cardputer.update();
    if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
      Keyboard_Class::KeysState st = M5Cardputer.Keyboard.keysState();
      for (auto c : st.word) pwd += c;
      if (st.del && pwd.length() > 0) pwd.remove(pwd.length() - 1);
      redrawInputLine(pwd, true);
      if (st.enter) return pwd;
    }
    delay(10);
  }
}

void connectWifi() {
  WiFi.mode(WIFI_STA);
  while (true) {
    String ssid = pickWifi();
    if (ssid == "") continue;
    String pwd  = enterPassword(ssid);

    cls();
    M5Cardputer.Display.setTextColor(COL_ACCENT);
    M5Cardputer.Display.println("Connecting...");
    M5Cardputer.Display.setTextColor(COL_TEXT);
    WiFi.begin(ssid.c_str(), pwd.c_str());

    int tries = 0;
    while (WiFi.status() != WL_CONNECTED && tries < 20) {
      delay(500);
      M5Cardputer.Display.print(".");
      tries++;
    }
    if (WiFi.status() == WL_CONNECTED) {
      M5Cardputer.Display.setTextColor(COL_ACCENT);
      M5Cardputer.Display.println("\nConnected!");
      M5Cardputer.Display.setTextColor(COL_DIM);
      M5Cardputer.Display.println(WiFi.localIP().toString());
      delay(1500);
      return;
    }
    M5Cardputer.Display.setTextColor(TFT_RED);
    M5Cardputer.Display.println("\nFailed. Try again.");
    delay(1500);
    WiFi.disconnect();
  }
}

// ── Settings mode ─────────────────────────────────────────
void drawSettings(int sel) {
  cls();
  header("Settings  (Enter=edit ESC=back)");

  String items[3] = {
    "Server: " + serverUrl,
    "Text size: " + String(textSize),
    "Status bar: " + String(showStatus ? "on" : "off")
  };

  for (int i = 0; i < 3; i++) {
    if (i == sel) {
      M5Cardputer.Display.setTextColor(COL_ACCENT);
      M5Cardputer.Display.print("> ");
    } else {
      M5Cardputer.Display.setTextColor(COL_TEXT);
      M5Cardputer.Display.print("  ");
    }
    M5Cardputer.Display.println(items[i]);
  }
  M5Cardputer.Display.setTextColor(COL_TEXT);
}

String editLine(String label, String current) {
  cls();
  header(label.c_str());
  M5Cardputer.Display.setTextColor(COL_DIM);
  M5Cardputer.Display.println("Backspace: del  Enter: save");
  M5Cardputer.Display.setTextColor(COL_INPUT);
  M5Cardputer.Display.print("> ");
  M5Cardputer.Display.println(current);
  M5Cardputer.Display.print("> ");

  inputLineY = M5Cardputer.Display.getCursorY();
  String val = current;
  redrawInputLine(val);
  while (true) {
    M5Cardputer.update();
    if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
      Keyboard_Class::KeysState st = M5Cardputer.Keyboard.keysState();
      for (auto c : st.word) val += c;
      if (st.del && val.length() > 0) val.remove(val.length() - 1);
      redrawInputLine(val);
      if (st.enter) return val;
    }
    delay(10);
  }
}

void runSettings() {
  int sel = 0;
  drawSettings(sel);

  while (true) {
    M5Cardputer.update();
    if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
      Keyboard_Class::KeysState st = M5Cardputer.Keyboard.keysState();

      bool up = false, down = false;
      for (auto k : st.hid_keys) {
        if (k == KEY_UP_ARROW)   up   = true;
        if (k == KEY_DOWN_ARROW) down = true;
      }
      if (up   && sel > 0) { sel--; drawSettings(sel); }
      if (down && sel < 2) { sel++; drawSettings(sel); }

      if (st.enter) {
        if (sel == 0) serverUrl = editLine("Server URL", serverUrl);
        if (sel == 1) {
          String ts = editLine("Text size (1-3)", String(textSize));
          int v = ts.toInt();
          if (v >= 1 && v <= 3) { textSize = v; M5Cardputer.Display.setTextSize(textSize); }
        }
        if (sel == 2) showStatus = !showStatus;
        drawSettings(sel);
      }

      // ESC = back (fn + backspace on Cardputer)
      if (st.fn && st.del) return;
      // Also allow typing 'q' to quit settings
      for (auto c : st.word) { if (c == 'q') return; }
    }
    delay(10);
  }
}

// ── Chat ──────────────────────────────────────────────────
void drawPrompt() {
  M5Cardputer.Display.setTextColor(COL_ACCENT);
  M5Cardputer.Display.println("");
  inputLineY = M5Cardputer.Display.getCursorY();
  M5Cardputer.Display.print("> ");
  M5Cardputer.Display.setTextColor(COL_INPUT);
}

void redrawInputLine(const String& val, bool masked) {
  int lineH = M5Cardputer.Display.fontHeight();
  M5Cardputer.Display.fillRect(0, inputLineY, M5Cardputer.Display.width(), lineH, COL_BG);
  M5Cardputer.Display.setCursor(0, inputLineY);
  M5Cardputer.Display.setTextColor(COL_ACCENT);
  M5Cardputer.Display.print("> ");
  M5Cardputer.Display.setTextColor(COL_INPUT);
  if (masked) {
    for (int i = 0; i < (int)val.length(); i++) M5Cardputer.Display.print('*');
  } else {
    M5Cardputer.Display.print(val);
  }
}

void handleCommand(String cmd) {
  cmd.trim();
  if (cmd == "/help") {
    M5Cardputer.Display.setTextColor(COL_ACCENT);
    M5Cardputer.Display.println("Commands:");
    M5Cardputer.Display.setTextColor(COL_TEXT);
    M5Cardputer.Display.println("/help     - this list");
    M5Cardputer.Display.println("/clear    - clear screen");
    M5Cardputer.Display.println("/wifi     - reconnect WiFi");
    M5Cardputer.Display.println("/settings - open settings");
    M5Cardputer.Display.println("/prompt   - change mode");
    M5Cardputer.Display.println("/model    - switch model");
    M5Cardputer.Display.println("/ip       - show IP");
  } else if (cmd == "/clear") {
    cls();
    M5Cardputer.Display.setTextColor(COL_ACCENT);
    M5Cardputer.Display.println("Claude Chat");
  } else if (cmd == "/wifi") {
    WiFi.disconnect();
    connectWifi();
  } else if (cmd == "/settings") {
    runSettings();
    cls();
    M5Cardputer.Display.setTextColor(COL_ACCENT);
    M5Cardputer.Display.println("Claude Chat");
  } else if (cmd == "/model") {
    pickModel();
    cls();
    M5Cardputer.Display.setTextColor(COL_ACCENT);
    M5Cardputer.Display.println("Claude Chat");
  } else if (cmd == "/prompt") {
    pickSystemPrompt();
    cls();
    M5Cardputer.Display.setTextColor(COL_ACCENT);
    M5Cardputer.Display.println("Claude Chat");
  } else if (cmd == "/ip") {
    M5Cardputer.Display.setTextColor(COL_DIM);
    M5Cardputer.Display.println(WiFi.localIP().toString());
  } else {
    M5Cardputer.Display.setTextColor(TFT_RED);
    M5Cardputer.Display.println("Unknown command. Try /help");
  }
  M5Cardputer.Display.setTextColor(COL_TEXT);
}

// Shared state for background HTTP task
struct PostResult {
  volatile bool done;
  volatile bool cancel;
  int           code;
  String        body;
};
static PostResult postResult;

void httpTask(void* param) {
  HTTPClient h;
  h.begin(serverUrl);
  h.setTimeout(30000);
  h.addHeader("Content-Type", "application/json");
  postResult.code = h.POST(_postBody);
  if (postResult.code == 200) postResult.body = h.getString();
  h.end();
  postResult.done = true;
  vTaskDelete(nullptr);
}

String sendToServer(String msg) {
  StaticJsonDocument<512> doc;
  doc["message"] = msg;
  if (systemPrompt.length() > 0) doc["system"] = systemPrompt;
  serializeJson(doc, _postBody);

  postResult = {false, false, 0, ""};

  // Run HTTP on core 0 (WiFi core) so it doesn't conflict with main loop
  xTaskCreatePinnedToCore(httpTask, "http", 8192, nullptr, 1, nullptr, 0);

  int statusY = M5Cardputer.Display.getCursorY();
  unsigned long start = millis();

  auto drawStatus = [&]() {
    int lineH = M5Cardputer.Display.fontHeight();
    M5Cardputer.Display.fillRect(0, statusY, M5Cardputer.Display.width(), lineH, COL_BG);
    M5Cardputer.Display.setCursor(0, statusY);
    M5Cardputer.Display.setTextColor(COL_DIM);
    unsigned long s = (millis() - start) / 1000;
    M5Cardputer.Display.printf("thinking %lus  fn+bksp=cancel", s);
  };

  drawStatus();

  unsigned long lastDraw = millis();
  while (!postResult.done) {
    M5Cardputer.update();

    // Redraw timer every second
    if (millis() - lastDraw >= 1000) {
      drawStatus();
      lastDraw = millis();
    }

    // Cancel: fn + backspace
    if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
      Keyboard_Class::KeysState st = M5Cardputer.Keyboard.keysState();
      if (st.fn && st.del) {
        postResult.cancel = true;
        // Wait for task to finish so WiFi stack is clean
        while (!postResult.done) delay(50);
        // Flush keys
        delay(100);
        while (M5Cardputer.Keyboard.isPressed()) { M5Cardputer.update(); delay(20); }
        return "(cancelled)";
      }
    }
    delay(50);
  }

  if (postResult.code == 200) {
    DynamicJsonDocument out(4096);
    deserializeJson(out, postResult.body);
    return out["reply"].as<String>();
  }
  return "HTTP " + String(postResult.code);
}

// ── Model picker ─────────────────────────────────────────
void pickModel() {
  const char* names[]  = { "Sonnet", "Opus", "Haiku" };
  const char* keys[]   = { "sonnet", "opus", "haiku" };
  const char* descs[]  = { "fast & smart", "most capable", "fastest" };
  const int count = 3;
  int sel = 0;

  auto draw = [&]() {
    cls();
    header("Model  (arrows + Enter)");
    for (int i = 0; i < count; i++) {
      if (i == sel) {
        M5Cardputer.Display.setTextColor(COL_ACCENT);
        M5Cardputer.Display.print("> ");
      } else {
        M5Cardputer.Display.setTextColor(COL_TEXT);
        M5Cardputer.Display.print("  ");
      }
      M5Cardputer.Display.print(names[i]);
      M5Cardputer.Display.setTextColor(COL_DIM);
      M5Cardputer.Display.print("  ");
      M5Cardputer.Display.println(descs[i]);
      M5Cardputer.Display.setTextColor(COL_TEXT);
    }
  };

  draw();

  while (true) {
    M5Cardputer.update();
    if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
      Keyboard_Class::KeysState st = M5Cardputer.Keyboard.keysState();
      bool up = false, down = false;
      for (auto k : st.hid_keys) {
        if (k == KEY_UP_ARROW)   up   = true;
        if (k == KEY_DOWN_ARROW) down = true;
      }
      if (up   && sel > 0)         { sel--; draw(); }
      if (down && sel < count - 1) { sel++; draw(); }
      if (st.enter) {
        // Tell the server to switch model
        HTTPClient http;
        http.begin(serverUrl.substring(0, serverUrl.lastIndexOf('/')) + "/model");
        http.setTimeout(10000);
        http.addHeader("Content-Type", "application/json");
        StaticJsonDocument<64> doc;
        doc["model"] = keys[sel];
        String body; serializeJson(doc, body);
        int code = http.POST(body);
        http.end();

        cls();
        M5Cardputer.Display.setTextColor(COL_ACCENT);
        M5Cardputer.Display.print("Model: ");
        M5Cardputer.Display.println(names[sel]);
        if (code != 200) {
          M5Cardputer.Display.setTextColor(TFT_RED);
          M5Cardputer.Display.println("Server error!");
        }
        delay(1000);
        return;
      }
    }
    delay(10);
  }
}

// ── System prompt picker ──────────────────────────────────
void pickSystemPrompt() {
  const char* labels[] = {
    "Concise",
    "Technical",
    "Creative",
    "ELI5",
    "Custom"
  };
  const char* prompts[] = {
    "You are a helpful assistant on a tiny handheld device. "
    "Reply in plain text only, no markdown, no bullet points. "
    "Keep every reply under 50 words.",

    "You are a technical assistant. Reply in plain text, no markdown. "
    "Be precise and concise. Under 60 words.",

    "You are a creative assistant. Reply in plain text, no markdown. "
    "Be imaginative but brief. Under 60 words.",

    "Explain everything simply like I am 10 years old. "
    "Plain text only, no markdown. Under 50 words.",

    ""   // placeholder — user types their own
  };
  const int count = 5;
  int sel = 0;

  auto draw = [&]() {
    cls();
    header("Mode  (arrows + Enter)");
    for (int i = 0; i < count; i++) {
      if (i == sel) {
        M5Cardputer.Display.setTextColor(COL_ACCENT);
        M5Cardputer.Display.print("> ");
      } else {
        M5Cardputer.Display.setTextColor(COL_TEXT);
        M5Cardputer.Display.print("  ");
      }
      M5Cardputer.Display.println(labels[i]);
    }
    M5Cardputer.Display.setTextColor(COL_TEXT);
  };

  draw();

  while (true) {
    M5Cardputer.update();
    if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
      Keyboard_Class::KeysState st = M5Cardputer.Keyboard.keysState();
      bool up = false, down = false;
      for (auto k : st.hid_keys) {
        if (k == KEY_UP_ARROW)   up   = true;
        if (k == KEY_DOWN_ARROW) down = true;
      }
      if (up   && sel > 0)         { sel--; draw(); }
      if (down && sel < count - 1) { sel++; draw(); }
      if (st.enter) {
        if (sel == count - 1) {
          // Custom: let user type their own prompt
          systemPrompt = editLine("Custom prompt", "");
        } else {
          systemPrompt = prompts[sel];
        }
        return;
      }
    }
    delay(10);
  }
}

// ── Setup & loop ──────────────────────────────────────────
void setup() {
  auto cfg = M5.config();
  M5Cardputer.begin(cfg, true);
  M5Cardputer.Display.setRotation(1);
  M5Cardputer.Display.setTextSize(textSize);
  M5Cardputer.Display.setTextScroll(true);
  M5Cardputer.Display.fillScreen(COL_BG);

  connectWifi();
  pickSystemPrompt();

  cls();
  M5Cardputer.Display.setTextColor(COL_ACCENT);
  M5Cardputer.Display.println("Claude Chat");
  M5Cardputer.Display.setTextColor(COL_DIM);
  M5Cardputer.Display.println("Type /help for commands");
  M5Cardputer.Display.setTextColor(COL_TEXT);
  drawPrompt();
}

void loop() {
  M5Cardputer.update();

  if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
    Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();

    bool changed = false;
    for (auto c : status.word) { input += c; changed = true; }
    if (status.del && input.length() > 0) { input.remove(input.length() - 1); changed = true; }
    if (changed) redrawInputLine(input);
    if (status.enter) {
      M5Cardputer.Display.println("");
      if (input.startsWith("/")) {
        handleCommand(input);
      } else {
        String reply = sendToServer(input);
        M5Cardputer.Display.setTextColor(COL_REPLY);
        M5Cardputer.Display.println(reply);
      }
      input = "";
      drawPrompt();
    }
  }
  delay(10);
}
