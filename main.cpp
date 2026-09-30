// AlertMap clock: карта тревог Украины (контур страны, области по ближайшему центру) для GeekMagic SmallTV (ESP8266, ST7789 240x240)
// Данные: https://jaam.net.ua (поддерживается и v3)
// Зелёный = нет тревоги, жёлтый = тревога в части области, красный = тревога в области.
// Настройка и OTA: http://<ip-часов>/  и  http://<ip-часов>/update

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266HTTPUpdateServer.h>
#include <ESP8266mDNS.h>
#include <WiFiClientSecure.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include <memory>

#define FW_VERSION "1.1"
#define PIN_BL 5                       // подсветка, активный уровень LOW
#define HOSTNAME "alertmap"
#define AP_NAME "AlertMap-Setup"
#define DEFAULT_URL "https://jaam.net.ua"

// ---------- конфиг ----------
struct Config {
  String ssid, pass, url, webpass;
  uint16_t interval = 20;              // секунд между запросами
  uint8_t bright = 60;                 // 1..100
  uint8_t rot = 0;                     // 0..3
  bool invert = true;                  // инверсия цветов дисплея
} cfg;

// ---------- карта: контур Украины + области по ближайшему центру ----------
// lon/lat центра области, w - "вес" (больше = область чуть больше)
struct Region { const char* name; float lon, lat, w; };
static const Region REG[] = {
  {"Волинська область",        24.90, 51.10, 0.0},
  {"Рівненська область",       26.40, 50.80, 0.0},
  {"Житомирська область",      28.30, 50.70, 0.1},
  {"Київська область",         30.40, 50.45, 0.3},
  {"Чернігівська область",     32.00, 51.40, 0.5},
  {"Сумська область",          34.60, 50.90, 0.1},
  {"Львівська область",        24.00, 49.70, 0.0},
  {"Тернопільська область",    25.50, 49.50, 0.0},
  {"Хмельницька область",      26.90, 49.50, 0.0},
  {"Черкаська область",        31.60, 49.30, 0.0},
  {"Полтавська область",       33.70, 49.60, 0.1},
  {"Харківська область",       36.40, 49.50, 0.4},
  {"Закарпатська область",     23.20, 48.50, 0.0},
  {"Івано-Франківська область",24.60, 48.70, 0.0},
  {"Чернівецька область",      25.80, 48.30, 0.0},
  {"Вінницька область",        28.60, 49.00, 0.0},
  {"Кіровоградська область",   32.30, 48.60, 0.2},
  {"Дніпропетровська область", 35.20, 48.50, 0.5},
  {"Донецька область",         37.40, 48.10, 0.2},
  {"Луганська область",        39.10, 48.90, 0.3},
  {"Одеська область",          30.30, 46.90, 0.8},
  {"Миколаївська область",     31.90, 47.20, 0.2},
  {"Херсонська область",       33.30, 46.60, 0.5},
  {"Запорізька область",       35.60, 47.40, 0.1},
  {"Автономна Республіка Крим",34.10, 45.30, 0.2},
};
static const uint8_t REG_N = sizeof(REG) / sizeof(REG[0]);

// города-маркеры поверх карты: только Киев
struct City { const char* name; float lon, lat; uint8_t r; };
static const City CITY[] = {
  {"Київ", 30.52, 50.45, 4},
};
static const uint8_t CITY_N = 1;

// внешний контур (lon, lat), по часовой стрелке с северо-запада
static const float OUT[][2] = {
  {23.55,51.55},{24.10,51.62},{24.60,51.90},{25.60,51.92},{26.50,51.98},{27.30,51.60},
  {28.00,51.55},{28.70,51.42},{29.30,51.38},{30.20,51.45},{30.65,51.40},{31.15,51.65},
  {31.75,52.10},{32.40,52.32},{33.25,52.37},{33.85,52.20},{34.15,51.85},{34.45,51.65},
  {35.05,51.25},{35.45,51.05},{35.75,50.65},{36.25,50.35},{36.90,50.30},{37.55,50.40},
  {38.05,50.05},{38.90,49.90},{39.60,49.75},{40.15,49.60},{40.05,49.05},{39.85,48.55},
  {39.75,48.05},{39.25,47.85},{38.85,47.85},{38.20,47.10},
  {37.55,47.05},{36.95,46.80},{36.25,46.65},{35.55,46.42},{35.05,46.20},
  {35.25,45.90},{35.60,45.55},{36.10,45.40},{36.60,45.38},{36.45,45.05},{35.90,45.02},
  {35.40,45.00},{34.95,44.85},{34.40,44.68},{34.15,44.50},{33.75,44.38},{33.55,44.55},
  {33.45,44.75},{33.40,45.15},{32.85,45.35},{32.50,45.35},{32.75,45.65},{32.90,46.05},
  {32.40,46.30},{31.85,46.30},{31.55,46.55},{31.55,46.65},{31.05,46.65},{30.75,46.50},
  {30.45,46.05},{30.20,45.85},{29.75,45.45},{29.70,45.20},{29.20,45.20},{28.85,45.30},
  {28.25,45.45},
  {28.40,45.65},{28.80,46.15},{29.60,46.45},{30.05,46.45},{30.10,46.90},{29.65,47.35},
  {29.35,47.75},{28.90,48.10},{28.30,48.35},{27.60,48.50},{26.65,48.25},{26.05,47.98},
  {25.25,47.90},{24.65,47.95},{23.90,47.95},{22.85,47.95},{22.15,48.40},{22.50,48.70},
  {22.55,49.08},{22.70,49.50},{23.15,49.95},{23.55,50.30},{24.00,50.85},{23.65,51.15},
};
#define ON (sizeof(OUT) / sizeof(OUT[0]))

enum Level : uint8_t { L_OK = 0, L_PART = 1, L_ALERT = 2, L_UNK = 3 };

static uint8_t lvl[REG_N];
static uint8_t cityLvl[1] = {L_UNK};

// ---------- геометрия экрана ----------
static const int CELL = 2;            // размер клетки карты, px
static const int MW = 120, MH = 80;   // карта 120x80 клеток = 240x160 px
static const int MAP_Y = 24;
static const float KX = 6.557f, KY = 9.8f, LON0 = 22.0f, LAT0 = 52.45f;

static float ox[ON], oy[ON];
static int16_t sx[REG_N], sy[REG_N];
static int32_t sw[REG_N];

static void initGeometry() {
  for (size_t i = 0; i < ON; i++) {
    ox[i] = (OUT[i][0] - LON0) * KX;
    oy[i] = (LAT0 - OUT[i][1]) * KY;
  }
  for (uint8_t i = 0; i < REG_N; i++) {
    sx[i] = (int16_t)((REG[i].lon - LON0) * KX * 4);
    sy[i] = (int16_t)((LAT0 - REG[i].lat) * KY * 4);
    sw[i] = (int32_t)(REG[i].w * 1537);
  }
}

TFT_eSPI tft;
ESP8266WebServer server(80);
ESP8266HTTPUpdateServer httpUpdater;

uint16_t C_BG, C_OK, C_PART, C_ALERT, C_UNK, C_DIM, C_LINE;

unsigned long lastFetch = 0, lastOk = 0, lastFooter = 0;
bool haveFetched = false, lastFetchOk = false, apOn = false;
String lastErr;

// ---------- утилиты ----------
static void setBacklight(uint8_t pct) {
  analogWriteRange(1023);
  analogWrite(PIN_BL, 1023 - (int)pct * 1023 / 100);
}

static uint16_t colorOf(uint8_t l) {
  switch (l) {
    case L_OK: return C_OK;
    case L_PART: return C_PART;
    case L_ALERT: return C_ALERT;
    default: return C_UNK;
  }
}

static String esc(const String& s) {
  String o;
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '&') o += F("&amp;");
    else if (c == '<') o += F("&lt;");
    else if (c == '>') o += F("&gt;");
    else if (c == '"') o += F("&quot;");
    else o += c;
  }
  return o;
}

// ---------- сохранение конфига ----------
static void loadConfig() {
  cfg.url = DEFAULT_URL;
  File f = LittleFS.open("/cfg.json", "r");
  if (!f) return;
  JsonDocument d;
  if (!deserializeJson(d, f)) {
    cfg.ssid = d["ssid"] | "";
    cfg.pass = d["pass"] | "";
    cfg.url = d["url"] | DEFAULT_URL;
    cfg.webpass = d["webpass"] | "";
    cfg.interval = d["interval"] | 20;
    cfg.bright = d["bright"] | 60;
    cfg.rot = d["rot"] | 0;
    cfg.invert = d["invert"] | true;
  }
  f.close();
  if (cfg.interval < 10) cfg.interval = 10;
  if (cfg.bright < 1) cfg.bright = 1;
  if (cfg.bright > 100) cfg.bright = 100;
  if (cfg.rot > 3) cfg.rot = 0;
}

static void saveConfig() {
  JsonDocument d;
  d["ssid"] = cfg.ssid;
  d["pass"] = cfg.pass;
  d["url"] = cfg.url;
  d["webpass"] = cfg.webpass;
  d["interval"] = cfg.interval;
  d["bright"] = cfg.bright;
  d["rot"] = cfg.rot;
  d["invert"] = cfg.invert;
  File f = LittleFS.open("/cfg.json", "w");
  if (!f) return;
  serializeJson(d, f);
  f.close();
}

// ---------- отрисовка ----------
static uint8_t regionAt(int px4, int py4) {
  int32_t best = 0x7fffffff;
  uint8_t bi = 0;
  for (uint8_t i = 0; i < REG_N; i++) {
    int32_t dx = px4 - sx[i], dy = py4 - sy[i];
    int32_t d = dx * dx + dy * dy - sw[i];
    if (d < best) { best = d; bi = i; }
  }
  return bi;
}

// ---- Киев (город): отдельная область внутри Киевской, по реальным координатам ----
#define IDX_KYIV_OBL 3
#define ID_KYIV REG_N               // id клетки Киева (25)
#define ID_OUT 255                  // вне контура страны
#define KYIV_SCALE 0.333f           // размер города относительно габаритов области (было 0.5)
static float kcx = -1000, kcy = -1000, kax = 1, kay = 1;

static uint8_t regionAtWithKyiv(int px4, int py4) {
  uint8_t bi = regionAt(px4, py4);
  
  if (bi == IDX_KYIV_OBL) {
    int16_t kx4 = (int16_t)((30.52f - LON0) * KX * 4);
    int16_t ky4 = (int16_t)((LAT0 - 50.45f) * KY * 4);
    
    int32_t kdx = px4 - kx4, kdy = py4 - ky4;
    if ((kdx * kdx + kdy * kdy) < 225) { 
      return ID_KYIV;
    }
  }
  return bi;
}

static int rowSpans(int cy, float* xs) {
  float yy = cy + 0.5f;
  int n = 0;
  for (size_t i = 0; i < ON; i++) {
    size_t j = (i + 1) % ON;
    float y1 = oy[i], y2 = oy[j];
    if ((y1 <= yy) != (y2 <= yy) && n < 32)
      xs[n++] = ox[i] + (yy - y1) * (ox[j] - ox[i]) / (y2 - y1);
  }
  for (int a = 1; a < n; a++) {                 // сортировка вставками
    float v = xs[a]; int b = a - 1;
    while (b >= 0 && xs[b] > v) { xs[b + 1] = xs[b]; b--; }
    xs[b + 1] = v;
  }
  return n;
}

static void computeKyiv() {
  float xs[32];
  long cnt = 0; float sumx = 0, sumy = 0;
  int minx = MW, maxx = -1, miny = MH, maxy = -1;
  for (int cy = 0; cy < MH; cy++) {
    int n = rowSpans(cy, xs), k = 0;
    for (int cx = 0; cx < MW; cx++) {
      while (k < n && xs[k] <= cx + 0.5f) k++;
      if ((k & 1) && regionAt(cx * 4 + 2, cy * 4 + 2) == IDX_KYIV_OBL) {
        cnt++; sumx += cx + 0.5f; sumy += cy + 0.5f;
        if (cx < minx) minx = cx; if (cx > maxx) maxx = cx;
        if (cy < miny) miny = cy; if (cy > maxy) maxy = cy;
      }
    }
  }
  if (!cnt) return;
  // центр - реальные координаты Киева; если они вне Киевской области - центр масс области
  kcx = (CITY[0].lon - LON0) * KX;
  kcy = (LAT0 - CITY[0].lat) * KY;
  if (regionAt((int)(kcx * 4), (int)(kcy * 4)) != IDX_KYIV_OBL) { kcx = sumx / cnt; kcy = sumy / cnt; }
  kax = (maxx - minx + 1) * KYIV_SCALE / 2.0f;
  kay = (maxy - miny + 1) * KYIV_SCALE / 2.0f;
  if (kax < 2.0f) kax = 2.0f;
  if (kay < 2.0f) kay = 2.0f;
}

// id региона для каждой клетки строки cy
static void rowIds(int cy, uint8_t* ids) {
  if (cy >= MH) { memset(ids, ID_OUT, MW); return; }
  float xs[32];
  int n = rowSpans(cy, xs), k = 0;
  for (int cx = 0; cx < MW; cx++) {
    while (k < n && xs[k] <= cx + 0.5f) k++;
    if (!(k & 1)) { ids[cx] = ID_OUT; continue; }
    uint8_t r = regionAt(cx * 4 + 2, cy * 4 + 2);
    if (r == IDX_KYIV_OBL) {
      float dx = (cx + 0.5f - kcx) / kax, dy = (cy + 0.5f - kcy) / kay;
      if (dx * dx + dy * dy <= 1.0f) r = ID_KYIV;
    }
    ids[cx] = r;
  }
}

static uint16_t idColor(uint8_t id) {
  if (id < REG_N) return colorOf(lvl[id]);
  if (id == ID_KYIV) return colorOf(cityLvl[0]);
  return C_BG;
}

static void drawMap() {
  uint8_t bufA[MW], bufB[MW];
  uint8_t *cur = bufA, *nxt = bufB;
  rowIds(0, cur);
  for (int cy = 0; cy < MH; cy++) {
    rowIds(cy + 1, nxt);
    int y = MAP_Y + cy * CELL;

    // заливка горизонтальными отрезками одного цвета
    int rs = 0;
    uint16_t rc = idColor(cur[0]);
    for (int cx = 1; cx <= MW; cx++) {
      uint16_t c = (cx < MW) ? idColor(cur[cx]) : 0;
      if (cx == MW || c != rc) {
        tft.fillRect(rs * CELL, y, (cx - rs) * CELL, CELL, rc);
        rs = cx; rc = c;
      }
    }
    // тонкие вертикальные границы (1 px) между разными областями
    for (int cx = 0; cx < MW - 1; cx++) {
      if (cur[cx] <= ID_KYIV && cur[cx + 1] <= ID_KYIV && cur[cx] != cur[cx + 1])
        tft.fillRect((cx + 1) * CELL - 1, y, 1, CELL, C_LINE);
    }
    // тонкие горизонтальные границы (1 px) к нижней строке
    int hs = -1;
    for (int cx = 0; cx <= MW; cx++) {
      bool e = cx < MW && cur[cx] <= ID_KYIV && nxt[cx] <= ID_KYIV && cur[cx] != nxt[cx];
      if (e && hs < 0) hs = cx;
      if (!e && hs >= 0) {
        tft.fillRect(hs * CELL, y + CELL - 1, (cx - hs) * CELL, 1, C_LINE);
        hs = -1;
      }
    }
    uint8_t* t = cur; cur = nxt; nxt = t;
  }
}

static void drawCities() {
  for (uint8_t i = 1; i < CITY_N; i++) {   // 0 = Киев, он рисуется областью
    int x = (int)((CITY[i].lon - LON0) * KX * CELL);
    int y = MAP_Y + (int)((LAT0 - CITY[i].lat) * KY * CELL);
    tft.fillCircle(x, y, CITY[i].r + 1, C_BG);
    tft.fillCircle(x, y, CITY[i].r, colorOf(cityLvl[i]));
  }
}

static void drawLegend() {
  int r = 0, y = 0, g = 0;
  for (uint8_t i = 0; i < REG_N; i++) {
    if (lvl[i] == L_ALERT) r++;
    else if (lvl[i] == L_PART) y++;
    else if (lvl[i] == L_OK) g++;
  }
  const int xs[3] = {4, 84, 164};
  const uint16_t cols[3] = {C_ALERT, C_PART, C_OK};
  const String txt[3] = {"Alert " + String(r), "Part. " + String(y), "Calm " + String(g)};
  tft.setTextDatum(TL_DATUM);
  tft.setTextPadding(62);
  tft.setTextColor(TFT_WHITE, C_BG);
  for (int i = 0; i < 3; i++) {
    tft.fillRect(xs[i], 193, 10, 10, cols[i]);
    tft.drawString(txt[i], xs[i] + 14, 190, 2);
  }
  tft.setTextPadding(0);
}

static void drawDot() {
  tft.fillCircle(226, 12, 5, lastFetchOk ? C_OK : C_ALERT);
}

static void drawFooter() {
  tft.setTextDatum(TC_DATUM);
  tft.setTextPadding(240);
  tft.setTextColor(C_DIM, C_BG);
  String s;
  if (lastOk) s = "upd " + String((millis() - lastOk) / 1000) + "s ago";
  else s = "waiting for data";
  if (!lastFetchOk && lastErr.length()) s += "  " + lastErr;
  tft.drawString(s, 120, 212, 2);
  String ip = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString()
                                              : String(AP_NAME) + " " + WiFi.softAPIP().toString();
  tft.drawString(ip, 120, 226, 2);
  tft.setTextPadding(0);
}

static void drawAll() {
  tft.fillScreen(C_BG);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_WHITE, C_BG);
  tft.drawString("AIR ALERT MAP", 6, 6, 2);
  drawMap();
  drawCities();
  drawLegend();
  drawDot();
  drawFooter();
}

static void splash(const String& a, const String& b = "") {
  tft.fillScreen(C_BG);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, C_BG);
  tft.drawString(a, 120, 100, 4);
  tft.setTextColor(C_DIM, C_BG);
  tft.drawString(b, 120, 140, 2);
}

// ---------- получение данных ----------
static uint8_t levelOf(JsonVariant v) {
  if (v.is<bool>()) return v.as<bool>() ? L_ALERT : L_OK;   // формат v3
  if (v.is<JsonObject>()) {                                 // формат v2
    bool a = v["alertnow"] | false;
    bool d = v["district"] | false;
    return a ? (d ? L_PART : L_ALERT) : L_OK;
  }
  return L_UNK;
}

static bool fetchAlerts() {
  if (WiFi.status() != WL_CONNECTED) { lastErr = "no wifi"; return false; }

  std::unique_ptr<WiFiClient> client;
  if (cfg.url.startsWith("https")) {
    auto* c = new BearSSL::WiFiClientSecure();
    c->setInsecure();
    c->setBufferSizes(4096, 512);
    client.reset(c);
  } else {
    client.reset(new WiFiClient());
  }

  HTTPClient http;
  http.setTimeout(8000);
  http.setUserAgent("AlertMapClock/" FW_VERSION);
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  if (!http.begin(*client, cfg.url)) { lastErr = "bad url"; return false; }
  int code = http.GET();
  if (code != 200) {
    lastErr = code < 0 ? String("conn ") + code : String("HTTP ") + code;
    http.end();
    return false;
  }
  String body = http.getString();
  http.end();

  JsonDocument doc;
  DeserializationError e = deserializeJson(doc, body);
  JsonObject st = doc["states"];
  if (e || st.isNull()) { lastErr = "bad json"; return false; }

  uint8_t nl[REG_N];
  memcpy(nl, lvl, sizeof(nl));
  uint8_t nc[CITY_N];
  memcpy(nc, cityLvl, sizeof(nc));
  for (JsonPair kv : st) {
    const char* k = kv.key().c_str();
    uint8_t l = levelOf(kv.value());
    bool found = false;
    for (uint8_t i = 0; i < REG_N; i++) {
      if (!strcmp(k, REG[i].name)) { nl[i] = l; found = true; break; }
    }
    if (found) continue;
    for (uint8_t i = 0; i < CITY_N; i++) {
      if (!strcmp(k, CITY[i].name)) { nc[i] = l; break; }
    }
  }

  bool changed = memcmp(nl, lvl, sizeof(nl)) || memcmp(nc, cityLvl, sizeof(nc));
  memcpy(lvl, nl, sizeof(nl));
  memcpy(cityLvl, nc, sizeof(nc));
  if (changed) { drawMap(); drawCities(); }
  lastErr = "";
  return true;
}

// ---------- веб ----------
static bool auth() {
  if (cfg.webpass.length() == 0) return true;
  if (server.authenticate("admin", cfg.webpass.c_str())) return true;
  server.requestAuthentication();
  return false;
}

static String sel(int v, int cur) { return v == cur ? F(" selected") : F(""); }

static void handleRoot() {
  if (!auth()) return;
  int r = 0, y = 0, g = 0;
  for (uint8_t i = 0; i < REG_N; i++) {
    if (lvl[i] == L_ALERT) r++;
    else if (lvl[i] == L_PART) y++;
    else if (lvl[i] == L_OK) g++;
  }
  String h;
  h.reserve(4500);
  h += F("<!doctype html><html lang=uk><head><meta charset=utf-8>"
         "<meta name=viewport content='width=device-width,initial-scale=1'><title>Alert Map</title><style>"
         "body{font-family:sans-serif;background:#111;color:#eee;max-width:480px;margin:auto;padding:12px}"
         "input,select{width:100%;box-sizing:border-box;padding:8px;margin:4px 0 12px;background:#222;color:#eee;border:1px solid #444;border-radius:6px}"
         "input[type=checkbox]{width:auto}"
         "button,a.b{display:block;text-align:center;padding:12px;background:#2b7;color:#000;border:0;border-radius:8px;font-size:16px;text-decoration:none;margin:8px 0;width:100%;box-sizing:border-box}"
         "label{font-size:13px;color:#aaa}.s{background:#1a1a1a;padding:8px;border-radius:8px;font-size:13px;line-height:1.6}"
         "</style></head><body><h2>Карта тривог</h2><div class=s>");
  h += F("<b style='color:#e33'>Червоних: ");
  h += r;
  h += F("</b> &nbsp; <b style='color:#fc0'>Жовтих: ");
  h += y;
  h += F("</b> &nbsp; <b style='color:#2b7'>Зелених: ");
  h += g;
  h += F("</b><br>Оновлено: ");
  h += lastOk ? String((millis() - lastOk) / 1000) + " с тому" : String("ще ні");
  if (lastErr.length()) { h += F("<br>Помилка: "); h += esc(lastErr); }
  h += F("<br>IP: ");
  h += WiFi.localIP().toString();
  h += F(" &nbsp; RSSI: ");
  h += WiFi.RSSI();
  h += F(" dBm<br>Heap: ");
  h += ESP.getFreeHeap();
  h += F(" &nbsp; FW ");
  h += FW_VERSION;
  h += F("</div><br><form method=post action=/save>");

  h += F("<label>Wi-Fi (SSID)</label><input name=ssid list=nets value=\"");
  h += esc(cfg.ssid);
  h += F("\"><datalist id=nets></datalist><button type=button onclick=scan() style='background:#357'>Сканувати мережі</button>");
  h += F("<label>Пароль Wi-Fi (порожнє = не змінювати)</label><input type=password name=pass>");
  h += F("<label>URL даних</label><input name=url value=\"");
  h += esc(cfg.url);
  h += F("\"><label>Інтервал оновлення, с (мін. 10)</label><input type=number name=interval min=10 max=600 value=");
  h += cfg.interval;
  h += F("><label>Яскравість, %</label><input type=range name=bright min=1 max=100 value=");
  h += cfg.bright;
  h += F("><label>Поворот екрана</label><select name=rot>");
  for (int i = 0; i < 4; i++) {
    h += F("<option value=");
    h += i;
    h += sel(i, cfg.rot);
    h += '>';
    h += i * 90;
    h += F("°</option>");
  }
  h += F("</select><label><input type=checkbox name=invert");
  if (cfg.invert) h += F(" checked");
  h += F("> Інвертувати кольори (увімкніть, якщо кольори «негативом»)</label><br><br>");
  h += F("<label>Пароль веб-сторінки та OTA (логін admin, порожнє = не змінювати)</label><input type=password name=webpass>");
  h += F("<label><input type=checkbox name=clearweb> Прибрати пароль</label><br><br>");
  h += F("<button type=submit>Зберегти і перезавантажити</button></form>"
         "<a class=b href=/update style='background:#a63;color:#fff'>Оновлення прошивки (OTA)</a>"
         "<script>function scan(){fetch('/scan').then(r=>r.json()).then(a=>{"
         "document.getElementById('nets').innerHTML=a.map(s=>'<option value=\"'+s.replace(/\"/g,'&quot;')+'\">').join('')})}"
         "</script></body></html>");
  server.send(200, "text/html; charset=utf-8", h);
}

static void handleSave() {
  if (!auth()) return;
  if (server.hasArg("ssid")) cfg.ssid = server.arg("ssid");
  if (server.arg("pass").length()) cfg.pass = server.arg("pass");
  String u = server.arg("url");
  u.trim();
  if (u.length()) cfg.url = u;
  cfg.interval = constrain(server.arg("interval").toInt(), 10, 600);
  cfg.bright = constrain(server.arg("bright").toInt(), 1, 100);
  cfg.rot = constrain(server.arg("rot").toInt(), 0, 3);
  cfg.invert = server.hasArg("invert");
  if (server.hasArg("clearweb")) cfg.webpass = "";
  else if (server.arg("webpass").length()) cfg.webpass = server.arg("webpass");
  saveConfig();
  server.send(200, "text/html; charset=utf-8",
              F("<!doctype html><meta charset=utf-8><meta name=viewport content='width=device-width'>"
                "<body style='font-family:sans-serif;background:#111;color:#eee;padding:20px'>"
                "Збережено. Перезавантаження…<script>setTimeout(()=>location.href='/',12000)</script>"));
  delay(500);
  ESP.restart();
}

static void handleScan() {
  if (!auth()) return;
  int n = WiFi.scanNetworks();
  String o = "[";
  for (int i = 0; i < n; i++) {
    String s = WiFi.SSID(i);
    s.replace("\\", "\\\\");
    s.replace("\"", "\\\"");
    if (i) o += ',';
    o += '"' + s + '"';
  }
  o += ']';
  server.send(200, "application/json", o);
}

static void handleStatus() {
  JsonDocument d;
  d["fw"] = FW_VERSION;
  d["ip"] = WiFi.localIP().toString();
  d["rssi"] = WiFi.RSSI();
  d["heap"] = ESP.getFreeHeap();
  d["last_ok_s_ago"] = lastOk ? (int)((millis() - lastOk) / 1000) : -1;
  d["error"] = lastErr;
  JsonArray a = d["levels"].to<JsonArray>();
  for (uint8_t i = 0; i < REG_N; i++) a.add(lvl[i]);
  String o;
  serializeJson(d, o);
  server.send(200, "application/json", o);
}

// ---------- Wi-Fi ----------
static void startAP() {
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_NAME);
  apOn = true;
  if (cfg.ssid.length()) WiFi.begin(cfg.ssid.c_str(), cfg.pass.c_str());
}

static void wifiStart() {
  WiFi.persistent(false);
  WiFi.hostname(HOSTNAME);
  WiFi.setAutoReconnect(true);
  WiFi.mode(WIFI_STA);
  if (cfg.ssid.length()) {
    WiFi.begin(cfg.ssid.c_str(), cfg.pass.c_str());
    unsigned long t = millis();
    int dots = 0;
    while (WiFi.status() != WL_CONNECTED && millis() - t < 20000) {
      splash("Wi-Fi", cfg.ssid + String(".....").substring(0, (dots++ % 5) + 1));
      delay(500);
    }
  }
  if (WiFi.status() != WL_CONNECTED) {
    startAP();
    splash("Setup mode", String("Wi-Fi: ") + AP_NAME + "  http://192.168.4.1");
    delay(3000);
  } else {
    splash("Connected", WiFi.localIP().toString());
    delay(1500);
  }
}

// ---------- setup / loop ----------
void setup() {
  Serial.begin(115200);
  pinMode(PIN_BL, OUTPUT);
  digitalWrite(PIN_BL, HIGH);          // подсветка выкл до инициализации
  LittleFS.begin();
  loadConfig();

  tft.init();
  tft.setRotation(cfg.rot);
  tft.invertDisplay(cfg.invert);
  C_BG = tft.color565(10, 10, 10);
  C_OK = tft.color565(0, 170, 60);
  C_PART = tft.color565(255, 200, 0);
  C_ALERT = tft.color565(230, 30, 30);
  C_UNK = tft.color565(70, 70, 70);
  C_DIM = tft.color565(130, 130, 130);
  C_LINE = tft.color565(225, 230, 230);   // тонкие границы областей
  tft.fillScreen(C_BG);
  setBacklight(cfg.bright);

  for (uint8_t i = 0; i < REG_N; i++) lvl[i] = L_UNK;
  initGeometry();
  computeKyiv();

  wifiStart();

  server.on("/", HTTP_GET, handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/scan", HTTP_GET, handleScan);
  server.on("/status", HTTP_GET, handleStatus);
  httpUpdater.setup(&server, "/update", cfg.webpass.length() ? "admin" : "", cfg.webpass);
  server.begin();
  MDNS.begin(HOSTNAME);
  MDNS.addService("http", "tcp", 80);

  drawAll();
}

void loop() {
  server.handleClient();
  MDNS.update();

  unsigned long now = millis();

  if (apOn && WiFi.status() == WL_CONNECTED) {   // подключились - AP больше не нужна
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    apOn = false;
  }

  if (WiFi.status() == WL_CONNECTED &&
      (!haveFetched || now - lastFetch >= (unsigned long)cfg.interval * 1000UL)) {
    haveFetched = true;
    lastFetch = now;
    lastFetchOk = fetchAlerts();
    if (lastFetchOk) lastOk = millis();
    Serial.printf("fetch %s %s heap=%u\n", lastFetchOk ? "ok" : "FAIL", lastErr.c_str(), ESP.getFreeHeap());
    drawLegend();
    drawDot();
    drawFooter();
    lastFooter = millis();
  }

  if (millis() - lastFooter >= 1000) {
    lastFooter = millis();
    drawFooter();
  }
  delay(2);
}
