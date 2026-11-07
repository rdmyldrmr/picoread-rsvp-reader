#include <Arduino.h>
#include <Wire.h>
#include <LittleFS.h>
#include <U8g2lib.h>

// ============================================================
// DONANIM 
// ============================================================

const uint8_t BUTTON_FORWARD = 10;
const uint8_t BUTTON_PLAY    = 12;

U8G2_SSD1306_128X32_UNIVISION_F_HW_I2C oled(
  U8G2_R0,
  U8X8_PIN_NONE
);

// ============================================================
// AYARLAR
// ============================================================

const uint8_t MENU_ITEMS_PER_PAGE = 3;
const uint32_t BUTTON_DEBOUNCE_MS = 35;

const uint16_t SPEEDS[] = {
  140, 150, 160, 170, 180, 190, 200
};

const uint8_t SPEED_COUNT =
  sizeof(SPEEDS) / sizeof(SPEEDS[0]);

const uint16_t DEFAULT_WPM = 170;
uint16_t currentWPM = DEFAULT_WPM;

// ============================================================
// DOSYA SİSTEMİ
// ============================================================

const char *SYSTEM_DIR    = "/.system";
const char *PROGRESS_FILE = "/.system/progress.txt";
const char *LASTBOOK_FILE = "/.system/lastbook.txt";
const char *CONFIG_FILE   = "/.system/config.txt";

// ============================================================
// KİTAPLAR
// ============================================================

const uint8_t MAX_BOOKS = 64;

String books[MAX_BOOKS];
uint8_t bookCount = 0;

int selectedBook = 0;
int lastPlayedBook = -1;

// ============================================================
// KİTAP OKUMA
// ============================================================

File currentBookFile;

String currentBookName = "";
String currentWord = "";

bool bookOpen = false;

uint32_t currentWordNumber = 1;
uint32_t currentWordStart = 0;
uint32_t currentWordNextPosition = 0;
uint32_t totalWords = 0;

// ============================================================
// SEYREK KELİME İNDEKSİ
// ============================================================

const uint16_t INDEX_INTERVAL = 100;
const uint16_t MAX_INDEX_ENTRIES = 2048;

uint32_t indexWord[MAX_INDEX_ENTRIES];
uint32_t indexPosition[MAX_INDEX_ENTRIES];

uint16_t indexCount = 0;

// ============================================================
// İLERLEME KAYDI
// ============================================================

uint8_t wordsSinceProgressSave = 0;
const uint8_t PROGRESS_SAVE_INTERVAL = 10;

// ============================================================
// BUTONLAR
// ============================================================

bool lastForwardReading = HIGH;
bool lastPlayReading = HIGH;

uint32_t lastForwardChange = 0;
uint32_t lastPlayChange = 0;

bool forwardPressed = false;
bool playPressed = false;

bool forwardEvent = false;
bool playEvent = false;

// ============================================================
// DURUMLAR
// ============================================================

enum ReaderState {
  STATE_HOME,
  STATE_BOOK_LIST,
  STATE_RESUME_MENU,
  STATE_READING,
  STATE_PAUSED,
  STATE_SPEED_MENU,
  STATE_SLEEP
};

ReaderState state = STATE_HOME;

// ============================================================
// HOME MENÜSÜ
//
// -1 = hiçbir öğe seçili değil
// ============================================================

const uint8_t HOME_ITEM_COUNT = 4;
int homeSelection = 0;

// ============================================================
// KİTAP LİSTESİ
// ============================================================

int bookPageStart = 0;

// ============================================================
// RESUME MENÜSÜ
// ============================================================

int resumeSelection = 0;

// ============================================================
// SPEED MENÜSÜ
// ============================================================

int speedSelection = 0;
int speedPageStart = 0;

// ============================================================
// PAUSED MENÜSÜ
//
// 0 = RESUME
// 1 = REWIND
// 2 = SLEEP
// 3 = GO BACK
// 4 = RESTART
//
// Sayfa 1: RESUME / REWIND
// Sayfa 2: SLEEP / GO BACK
// Sayfa 3: RESTART
// ============================================================

int pauseSelection = 0;

uint32_t rewindTargetWord = 1;
String rewindTargetPreview = "";

// ============================================================
// ZAMANLAYICI
// ============================================================

uint32_t lastWordTime = 0;

bool sleepWakeTiming = false;
uint32_t sleepBothPressedSince = 0;

// ============================================================
// WHITESPACE
// ============================================================

bool isWhitespaceByte(uint8_t c) {
  return (
    c == ' '  ||
    c == '\t' ||
    c == '\r' ||
    c == '\n' ||
    c == '\v' ||
    c == '\f'
  );
}

// ============================================================
// DOSYA KONTROLLERİ
// ============================================================

bool isHiddenSystemFile(const String &name) {
  if (name.startsWith("/.")) {
    return true;
  }

  if (name == "/lastbook.txt") return true;
  if (name == "/progress.txt") return true;
  if (name == "/resume.txt") return true;
  if (name == "/config.txt") return true;

  if (name.startsWith("/.system/")) {
    return true;
  }

  return false;
}

bool isBookFile(const String &name) {
  if (name.length() < 4) {
    return false;
  }

  if (isHiddenSystemFile(name)) {
    return false;
  }

  String lower = name;
  lower.toLowerCase();

  return lower.endsWith(".txt");
}

// ============================================================
// BUTON OLAYLARI
// ============================================================

bool takeForwardEvent() {
  if (forwardEvent) {
    forwardEvent = false;
    return true;
  }

  return false;
}

bool takePlayEvent() {
  if (playEvent) {
    playEvent = false;
    return true;
  }

  return false;
}

void updateButtons() {
  bool forwardNow = digitalRead(BUTTON_FORWARD);
  bool playNow = digitalRead(BUTTON_PLAY);

  uint32_t now = millis();

  // FORWARD

  if (forwardNow != lastForwardReading) {
    lastForwardChange = now;
    lastForwardReading = forwardNow;
  }

  if (
    forwardNow == LOW &&
    !forwardPressed &&
    now - lastForwardChange >= BUTTON_DEBOUNCE_MS
  ) {
    forwardPressed = true;
    forwardEvent = true;
  }

  if (forwardNow == HIGH) {
    forwardPressed = false;
  }

  // PLAY

  if (playNow != lastPlayReading) {
    lastPlayChange = now;
    lastPlayReading = playNow;
  }

  if (
    playNow == LOW &&
    !playPressed &&
    now - lastPlayChange >= BUTTON_DEBOUNCE_MS
  ) {
    playPressed = true;
    playEvent = true;
  }

  if (playNow == HIGH) {
    playPressed = false;
  }
}

// ============================================================
// WPM AYARLARI
// ============================================================

int findSpeedIndex(uint16_t speed) {
  for (uint8_t i = 0; i < SPEED_COUNT; i++) {
    if (SPEEDS[i] == speed) {
      return i;
    }
  }

  return -1;
}

void saveSpeedConfig() {
  if (!LittleFS.exists(SYSTEM_DIR)) {
    LittleFS.mkdir(SYSTEM_DIR);
  }

  File f = LittleFS.open(CONFIG_FILE, "w");

  if (!f) {
    return;
  }

  f.println(currentWPM);
  f.close();
}

void loadSpeedConfig() {
  currentWPM = DEFAULT_WPM;

  if (!LittleFS.exists(CONFIG_FILE)) {
    saveSpeedConfig();
    return;
  }

  File f = LittleFS.open(CONFIG_FILE, "r");

  if (!f) {
    return;
  }

  String line = f.readStringUntil('\n');
  f.close();
  line.trim();

  uint16_t loadedSpeed = line.toInt();

  if (findSpeedIndex(loadedSpeed) >= 0) {
    currentWPM = loadedSpeed;
  } else {
    currentWPM = DEFAULT_WPM;
    saveSpeedConfig();
  }
}

// ============================================================
// SON OYNATILAN KİTAP
// ============================================================

void saveLastBook(const String &filename) {
  if (!LittleFS.exists(SYSTEM_DIR)) {
    LittleFS.mkdir(SYSTEM_DIR);
  }

  File f = LittleFS.open(LASTBOOK_FILE, "w");

  if (!f) {
    return;
  }

  f.println(filename);
  f.close();
}

String loadLastBook() {
  if (!LittleFS.exists(LASTBOOK_FILE)) {
    return "";
  }

  File f = LittleFS.open(LASTBOOK_FILE, "r");

  if (!f) {
    return "";
  }

  String result = f.readStringUntil('\n');

  f.close();
  result.trim();

  return result;
}

// ============================================================
// İLERLEME OKUMA
// ============================================================

bool loadProgress(
  const String &filename,
  uint32_t &wordNumber,
  uint32_t &bytePosition
) {
  wordNumber = 1;
  bytePosition = 0;

  if (!LittleFS.exists(PROGRESS_FILE)) {
    return false;
  }

  File f = LittleFS.open(PROGRESS_FILE, "r");

  if (!f) {
    return false;
  }

  bool found = false;

  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();

    if (line.length() == 0) {
      continue;
    }

    int p1 = line.indexOf('|');

    if (p1 < 0) {
      continue;
    }

    int p2 = line.indexOf('|', p1 + 1);

    if (p2 < 0) {
      continue;
    }

    String savedFilename = line.substring(0, p1);
    String savedWord = line.substring(p1 + 1, p2);
    String savedPosition = line.substring(p2 + 1);

    savedFilename.trim();
    savedWord.trim();
    savedPosition.trim();

    if (savedFilename == filename) {
      wordNumber = savedWord.toInt();
      bytePosition = savedPosition.toInt();

      if (wordNumber < 1) {
        wordNumber = 1;
      }

      found = true;
      break;
    }
  }

  f.close();

  return found;
}

// ============================================================
// İLERLEME KAYDET
// ============================================================

void saveProgress() {
  if (!bookOpen || currentBookName.length() == 0) {
    return;
  }

  if (!LittleFS.exists(SYSTEM_DIR)) {
    LittleFS.mkdir(SYSTEM_DIR);
  }

  String lines[64];
  uint8_t count = 0;

  if (LittleFS.exists(PROGRESS_FILE)) {
    File oldFile = LittleFS.open(PROGRESS_FILE, "r");

    if (oldFile) {
      while (oldFile.available() && count < 64) {
        String line = oldFile.readStringUntil('\n');
        line.trim();

        if (line.length() == 0) {
          continue;
        }

        int p = line.indexOf('|');

        if (p < 0) {
          continue;
        }

        String name = line.substring(0, p);

        if (name != currentBookName) {
          lines[count++] = line;
        }
      }

      oldFile.close();
    }
  }

  File f = LittleFS.open(PROGRESS_FILE, "w");

  if (!f) {
    return;
  }

  for (uint8_t i = 0; i < count; i++) {
    f.println(lines[i]);
  }

  // filename|wordNumber|bytePosition
  f.print(currentBookName);
  f.print("|");
  f.print(currentWordNumber);
  f.print("|");
  f.println(currentWordStart);

  f.close();

  wordsSinceProgressSave = 0;
}

// ============================================================
// KİTAPLARI TARA
// ============================================================

void scanBooks() {
  bookCount = 0;

  File root = LittleFS.open("/", "r");

  if (!root) {
    return;
  }

  File file = root.openNextFile();

  while (file && bookCount < MAX_BOOKS) {
    String name = file.name();

    if (
      !file.isDirectory() &&
      !isHiddenSystemFile(name) &&
      isBookFile(name)
    ) {
      books[bookCount++] = name;
    }

    file.close();
    file = root.openNextFile();
  }

  root.close();

  for (uint8_t i = 0; i < bookCount; i++) {
    for (uint8_t j = i + 1; j < bookCount; j++) {
      if (books[j] < books[i]) {
        String temp = books[i];
        books[i] = books[j];
        books[j] = temp;
      }
    }
  }
}

// ============================================================
// KİTAP AÇ
// ============================================================

bool openBook(const String &filename) {
  if (currentBookFile) {
    currentBookFile.close();
  }

  currentBookFile = LittleFS.open(filename, "r");

  if (!currentBookFile) {
    bookOpen = false;
    return false;
  }

  currentBookName = filename;
  bookOpen = true;

  return true;
}

// ============================================================
// SEYREK KELİME İNDEKSİ OLUŞTUR
// ============================================================

void buildSparseIndex() {
  indexCount = 0;
  totalWords = 0;

  if (!bookOpen) {
    return;
  }

  currentBookFile.seek(0);

  bool insideWord = false;

  while (currentBookFile.available()) {
    uint32_t position = currentBookFile.position();
    int value = currentBookFile.read();

    if (value < 0) {
      break;
    }

    uint8_t c = (uint8_t)value;

    if (!isWhitespaceByte(c)) {
      if (!insideWord) {
        insideWord = true;

        uint32_t wordNumber = totalWords + 1;

        if (
          wordNumber == 1 ||
          ((wordNumber - 1) % INDEX_INTERVAL == 0)
        ) {
          if (indexCount < MAX_INDEX_ENTRIES) {
            indexWord[indexCount] = wordNumber;
            indexPosition[indexCount] = position;
            indexCount++;
          }
        }
      }
    } else {
      if (insideWord) {
        totalWords++;
        insideWord = false;
      }
    }
  }

  if (insideWord) {
    totalWords++;
  }

  currentBookFile.seek(0);
}

// ============================================================
// KELİME KONUMU BUL
// ============================================================

uint32_t findWordPosition(uint32_t targetWord) {
  if (!bookOpen) {
    return 0;
  }

  if (targetWord < 1) {
    targetWord = 1;
  }

  if (totalWords > 0 && targetWord > totalWords) {
    targetWord = totalWords;
  }

  uint32_t bestPosition = 0;
  uint32_t bestWord = 1;

  for (uint16_t i = 0; i < indexCount; i++) {
    if (indexWord[i] <= targetWord) {
      bestWord = indexWord[i];
      bestPosition = indexPosition[i];
    } else {
      break;
    }
  }

  currentBookFile.seek(bestPosition);

  uint32_t wordNumber = bestWord;

  while (wordNumber < targetWord) {
    bool insideWord = false;

    while (currentBookFile.available()) {
      int value = currentBookFile.read();

      if (value < 0) {
        return currentBookFile.position();
      }

      uint8_t c = (uint8_t)value;

      if (!isWhitespaceByte(c)) {
        insideWord = true;
      } else if (insideWord) {
        break;
      }
    }

    while (currentBookFile.available()) {
      int value = currentBookFile.peek();

      if (
        value < 0 ||
        !isWhitespaceByte((uint8_t)value)
      ) {
        break;
      }

      currentBookFile.read();
    }

    wordNumber++;
  }

  while (currentBookFile.available()) {
    int value = currentBookFile.peek();

    if (
      value < 0 ||
      !isWhitespaceByte((uint8_t)value)
    ) {
      break;
    }

    currentBookFile.read();
  }

  return currentBookFile.position();
}

// ============================================================
// BELİRLİ KELİMEYE GİT
// ============================================================

bool gotoWord(uint32_t targetWord) {
  if (!bookOpen || totalWords == 0) {
    return false;
  }

  if (targetWord < 1) {
    targetWord = 1;
  }

  if (targetWord > totalWords) {
    targetWord = totalWords;
  }

  uint32_t position = findWordPosition(targetWord);

  currentBookFile.seek(position);

  currentWordNumber = targetWord;
  currentWordStart = position;
  currentWordNextPosition = position;

  return true;
}

// ============================================================
// SONRAKİ KELİMEYİ OKU
// ============================================================

bool readNextWord() {
  if (!bookOpen) {
    return false;
  }

  while (currentBookFile.available()) {
    uint32_t startPosition = currentBookFile.position();
    int value = currentBookFile.peek();

    if (value < 0) {
      return false;
    }

    if (!isWhitespaceByte((uint8_t)value)) {
      currentWordStart = startPosition;
      break;
    }

    currentBookFile.read();
  }

  if (!currentBookFile.available()) {
    return false;
  }

  String word = "";

  while (currentBookFile.available()) {
    int value = currentBookFile.peek();

    if (value < 0) {
      break;
    }

    uint8_t c = (uint8_t)value;

    if (isWhitespaceByte(c)) {
      break;
    }

    word += (char)currentBookFile.read();

    if (word.length() >= 100) {
      break;
    }
  }

  currentWordNextPosition = currentBookFile.position();
  currentWord = word;

  return currentWord.length() > 0;
}

// ============================================================
// KELİME ÖNİZLE
// ============================================================

String previewWordAt(uint32_t targetWord) {
  if (!bookOpen) {
    return "";
  }

  uint32_t oldPosition = currentBookFile.position();

  uint32_t position = findWordPosition(targetWord);
  currentBookFile.seek(position);

  while (currentBookFile.available()) {
    int value = currentBookFile.peek();

    if (
      value < 0 ||
      !isWhitespaceByte((uint8_t)value)
    ) {
      break;
    }

    currentBookFile.read();
  }

  String result = "";

  while (currentBookFile.available()) {
    int value = currentBookFile.peek();

    if (value < 0) {
      break;
    }

    uint8_t c = (uint8_t)value;

    if (isWhitespaceByte(c)) {
      break;
    }

    result += (char)currentBookFile.read();

    if (result.length() >= 40) {
      break;
    }
  }

  currentBookFile.seek(oldPosition);

  return result;
}

// ============================================================
// 20 KELİME GERİ SAR
// ============================================================

void rewind20Words() {
  if (currentWordNumber > 20) {
    rewindTargetWord = currentWordNumber - 20;
  } else {
    rewindTargetWord = 1;
  }

  rewindTargetPreview = previewWordAt(rewindTargetWord);
}

// ============================================================
// SEÇİLİ KİTABI BAŞLAT
// ============================================================

bool startSelectedBook() {
  if (selectedBook < 0 || selectedBook >= bookCount) {
    return false;
  }

  if (!openBook(books[selectedBook])) {
    return false;
  }

  buildSparseIndex();

  if (totalWords == 0) {
    return false;
  }

  uint32_t savedWordNumber = 1;
  uint32_t savedBytePosition = 0;

  bool progressFound = loadProgress(
    currentBookName,
    savedWordNumber,
    savedBytePosition
  );

  uint32_t targetWord = 1;

  if (progressFound) {
    targetWord = savedWordNumber;

    if (targetWord < 1) {
      targetWord = 1;
    }

    if (targetWord > totalWords) {
      targetWord = totalWords;
    }
  }

  bool positioned = false;

  if (
    progressFound &&
    savedBytePosition < currentBookFile.size()
  ) {
    currentBookFile.seek(savedBytePosition);

    currentWordNumber = targetWord;
    currentWordStart = savedBytePosition;

    if (readNextWord()) {
      positioned = true;
    }
  }

  if (!positioned) {
    if (!gotoWord(targetWord)) {
      return false;
    }

    if (!readNextWord()) {
      if (!gotoWord(1) || !readNextWord()) {
        return false;
      }

      currentWordNumber = 1;
    }
  }

  // Sayaç, ekranda gösterilen kelimeyi belirtir.
  currentWordNumber = targetWord;

  saveLastBook(currentBookName);

  lastPlayedBook = selectedBook;
  wordsSinceProgressSave = 0;
  lastWordTime = millis();

  return true;
}

// ============================================================
// KİTABI BAŞTAN BAŞLAT
// ============================================================

bool restartCurrentBook() {
  if (!bookOpen || totalWords == 0) {
    return false;
  }

  if (!gotoWord(1)) {
    return false;
  }

  if (!readNextWord()) {
    return false;
  }

  currentWordNumber = 1;
  wordsSinceProgressSave = 0;

  saveLastBook(currentBookName);
  saveProgress();

  lastWordTime = millis();

  state = STATE_READING;
  drawReading();

  return true;
}

// ============================================================
// HOME
// ============================================================

void enterHome() {
  state = STATE_HOME;
  homeSelection = 0;
}

void drawHome() {
  oled.clearBuffer();
  oled.setFont(u8g2_font_5x8_tf);

  const uint8_t y1 = 7;
  const uint8_t y2 = 15;
  const uint8_t y3 = 23;
  const uint8_t y4 = 31;

  if (homeSelection == 0) {
    oled.drawStr(0, y1, ">");
  }
  oled.drawStr(8, y1, "RESUME");

  if (homeSelection == 1) {
    oled.drawStr(0, y2, ">");
  }
  oled.drawStr(8, y2, "SELECT BOOKS");

  if (homeSelection == 2) {
    oled.drawStr(0, y3, ">");
  }

  char speedText[28];

  snprintf(
    speedText,
    sizeof(speedText),
    "SELECT SPEED (%u)",
    currentWPM
  );

  oled.drawStr(8, y3, speedText);

  if (homeSelection == 3) {
    oled.drawStr(0, y4, ">");
  }
  oled.drawStr(8, y4, "SLEEP");

  oled.sendBuffer();
}

// ============================================================
// KİTAP LİSTESİ
// ============================================================

void drawBookList() {
  oled.clearBuffer();
  oled.setFont(u8g2_font_5x8_tf);

  int itemCount = (int)bookCount + 1;

  if (selectedBook < 0) {
    selectedBook = 0;
  }

  if (selectedBook >= itemCount) {
    selectedBook = itemCount - 1;
  }

  if (selectedBook < bookPageStart) {
    bookPageStart = selectedBook;
  }

  if (selectedBook >= bookPageStart + MENU_ITEMS_PER_PAGE) {
    bookPageStart =
      selectedBook - MENU_ITEMS_PER_PAGE + 1;
  }

  for (uint8_t row = 0; row < MENU_ITEMS_PER_PAGE; row++) {
    int index = bookPageStart + row;

    if (index >= itemCount) {
      break;
    }

    uint8_t y = 8 + row * 10;

    if (index == selectedBook) {
      oled.drawStr(0, y, ">");
    }

    if (index == (int)bookCount) {
      oled.drawStr(8, y, "GO BACK");
    } else {
      String name = books[index];

      if (name.startsWith("/")) {
        name.remove(0, 1);
      }

      // Uzun dosya adlarını ekrana sığdır.
      // UTF-8 karakterleri byte bazında kesilmediği için
      // yalnızca görüntüleme alanı doğal olarak kırpılır.
      oled.drawUTF8(8, y, name.c_str());
    }
  }

  oled.sendBuffer();
}

// ============================================================
// RESUME MENÜSÜ
// ============================================================

void drawResumeMenu() {
  oled.clearBuffer();
  oled.setFont(u8g2_font_6x10_tf);

  if (resumeSelection == 0) {
    oled.drawStr(0, 10, ">");
  }
  oled.drawStr(8, 10, "PLAY");

  if (resumeSelection == 1) {
    oled.drawStr(0, 22, ">");
  }
  oled.drawStr(8, 22, "GO BACK");

  oled.sendBuffer();
}

// ============================================================
// SPEED MENÜSÜ
// ============================================================

void drawSpeedMenu() {
  oled.clearBuffer();
  oled.setFont(u8g2_font_6x10_tf);

  if (speedSelection < speedPageStart) {
    speedPageStart = speedSelection;
  }

  if (speedSelection >= speedPageStart + MENU_ITEMS_PER_PAGE) {
    speedPageStart =
      speedSelection - MENU_ITEMS_PER_PAGE + 1;
  }

  for (uint8_t row = 0; row < MENU_ITEMS_PER_PAGE; row++) {
    int index = speedPageStart + row;

    if (index >= SPEED_COUNT) {
      break;
    }

    uint8_t y = 10 + row * 10;

    if (index == speedSelection) {
      oled.drawStr(0, y, ">");
    }

    char speedText[8];

    snprintf(
      speedText,
      sizeof(speedText),
      "%u",
      SPEEDS[index]
    );

    oled.drawStr(8, y, speedText);
  }

  oled.sendBuffer();
}

// ============================================================
// OKUMA EKRANI
// ============================================================

void drawReading() {
  oled.clearBuffer();
  oled.enableUTF8Print();
  oled.setFont(u8g2_font_unifont_t_extended);

  int width = oled.getUTF8Width(currentWord.c_str());
  int x = (128 - width) / 2;

  if (x < 0) {
    x = 0;
  }

  oled.drawUTF8(x, 21, currentWord.c_str());
  oled.sendBuffer();
}

// ============================================================
// PAUSED MENÜSÜ
//
// Üst satır: PAUSED + mevcut/toplam kelime
// Sayfa 1: RESUME / REWIND
// Sayfa 2: SLEEP / GO BACK
// Sayfa 3: RESTART
//
// Her çizimde ekran temizlenir.
// ============================================================

void drawPausedCurrentPage() {
  oled.clearBuffer();
  oled.setFont(u8g2_font_5x8_tf);

  // Sabit başlık
  oled.drawStr(0, 7, "PAUSED");

  // Kelime sayacı sağa hizalanır.
  String counter =
    String(currentWordNumber) + "/" + String(totalWords);

  int counterWidth = oled.getUTF8Width(counter.c_str());
  int counterX = 128 - counterWidth;

  // Çok uzun sayılarda başlıkla çakışmayı önle.
  if (counterX < 35) {
    counterX = 35;
  }

  oled.drawStr(counterX, 7, counter.c_str());

  // SAYFA 1: RESUME / REWIND
  if (pauseSelection <= 1) {
    if (pauseSelection == 0) {
      oled.drawStr(0, 17, ">");
    }

    oled.drawStr(8, 17, "RESUME");

    if (pauseSelection == 1) {
      oled.drawStr(0, 29, ">");
    }

    // Metin sola hizalanır; ekranın sağ kenarını aşarsa
    // OLED'in sınırında kırpılır.
    String rewindLine = "REWIND: ";
    rewindLine += rewindTargetPreview;

    oled.drawUTF8(8, 29, rewindLine.c_str());
  }

  // SAYFA 2: SLEEP / GO BACK
  else if (pauseSelection <= 3) {
    if (pauseSelection == 2) {
      oled.drawStr(0, 17, ">");
    }

    oled.drawStr(8, 17, "SLEEP");

    if (pauseSelection == 3) {
      oled.drawStr(0, 29, ">");
    }

    oled.drawStr(8, 29, "GO BACK");
  }

  // SAYFA 3: RESTART
  else {
    oled.drawStr(0, 17, ">");

    oled.drawStr(8, 17, "RESTART");
  }

  oled.sendBuffer();
}

// ============================================================
// SLEEP
// ============================================================

void enterSleep() {
  saveProgress();

  oled.clearBuffer();
  oled.sendBuffer();
  oled.setPowerSave(1);

  state = STATE_SLEEP;

  sleepWakeTiming = false;
  sleepBothPressedSince = 0;

  forwardEvent = false;
  playEvent = false;
}

// ============================================================
// SLEEP'TEN UYAN
// ============================================================

void wakeFromSleep() {
  oled.setPowerSave(0);

  // Uyandırma basışları menü olayı olarak kullanılmasın.
  forwardEvent = false;
  playEvent = false;

  // Butonlar hâlâ basılıyken yeni basış olayı üretme.
  forwardPressed = true;
  playPressed = true;

  lastForwardReading = digitalRead(BUTTON_FORWARD);
  lastPlayReading = digitalRead(BUTTON_PLAY);

  lastForwardChange = millis();
  lastPlayChange = millis();

  sleepWakeTiming = false;
  sleepBothPressedSince = 0;

  enterHome();

  // Uyku dönüşünde hiçbir seçenek seçili değil.
  homeSelection = -1;

  drawHome();
}

// ============================================================
// HOME OLAYLARI
// ============================================================

void handleHome() {
  // Sleep dönüşünden sonra ilk FORWARD basışı,
  // yalnızca RESUME seçeneğini görünür yapar.
  if (homeSelection < 0) {
    if (takeForwardEvent()) {
      homeSelection = 0;
      drawHome();
    }

    // Seçim işareti oluşmadan PLAY ile seçim yapılamaz.
    takePlayEvent();
    return;
  }

  // FORWARD: sonraki öğe
  if (takeForwardEvent()) {
    homeSelection++;

    if (homeSelection >= HOME_ITEM_COUNT) {
      homeSelection = 0;
    }

    drawHome();
  }

  // PLAY: seç
  if (takePlayEvent()) {
    switch (homeSelection) {
      case 0: {
        String lastBook = loadLastBook();

        if (lastBook.length() == 0) {
          return;
        }

        int found = -1;

        for (uint8_t i = 0; i < bookCount; i++) {
          if (books[i] == lastBook) {
            found = i;
            break;
          }
        }

        if (found < 0) {
          return;
        }

        selectedBook = found;
        state = STATE_RESUME_MENU;
        resumeSelection = 0;

        drawResumeMenu();
        break;
      }

      case 1:
        selectedBook = 0;
        bookPageStart = 0;

        state = STATE_BOOK_LIST;

        drawBookList();
        break;

      case 2:
        speedSelection = findSpeedIndex(currentWPM);

        if (speedSelection < 0) {
          speedSelection = 3;
        }

        speedPageStart = 0;
        state = STATE_SPEED_MENU;

        drawSpeedMenu();
        break;

      case 3:
        enterSleep();
        break;
    }
  }
}

// ============================================================
// KİTAP LİSTESİ OLAYLARI
// ============================================================

void handleBookList() {
  int itemCount = (int)bookCount + 1;

  if (takeForwardEvent()) {
    selectedBook++;

    if (selectedBook >= itemCount) {
      selectedBook = 0;
      bookPageStart = 0;
    }

    drawBookList();
  }

  if (takePlayEvent()) {
    if (selectedBook == (int)bookCount) {
      enterHome();
      drawHome();
      return;
    }

    if (bookCount == 0) {
      return;
    }

    state = STATE_RESUME_MENU;
    resumeSelection = 0;

    drawResumeMenu();
  }
}

// ============================================================
// RESUME MENÜSÜ OLAYLARI
// ============================================================

void handleResumeMenu() {
  if (takeForwardEvent()) {
    resumeSelection++;

    if (resumeSelection > 1) {
      resumeSelection = 0;
    }

    drawResumeMenu();
  }

  if (takePlayEvent()) {
    if (resumeSelection == 0) {
      if (startSelectedBook()) {
        state = STATE_READING;
        drawReading();
      }

      return;
    }

    if (resumeSelection == 1) {
      enterHome();
      drawHome();
      return;
    }
  }
}

// ============================================================
// SPEED MENÜSÜ OLAYLARI
// ============================================================

void handleSpeedMenu() {
  if (takeForwardEvent()) {
    speedSelection++;

    if (speedSelection >= SPEED_COUNT) {
      speedSelection = 0;
      speedPageStart = 0;
    }

    drawSpeedMenu();
  }

  if (takePlayEvent()) {
    currentWPM = SPEEDS[speedSelection];

    saveSpeedConfig();

    enterHome();
    drawHome();
  }
}

// ============================================================
// OKUMA OLAYLARI
// ============================================================

void handleReading() {
  // FORWARD okuma sırasında kullanılmıyor.
  takeForwardEvent();

  // PLAY: duraklat
  if (takePlayEvent()) {
    saveProgress();
    rewind20Words();

    pauseSelection = 0;
    state = STATE_PAUSED;

    drawPausedCurrentPage();
    return;
  }

  uint32_t interval = 60000UL / currentWPM;
  uint32_t now = millis();

  if (now - lastWordTime >= interval) {
    lastWordTime = now;

    if (readNextWord()) {
      // Ekranda gösterilen kelimenin sayısını artır.
      currentWordNumber++;

      wordsSinceProgressSave++;

      if (wordsSinceProgressSave >= PROGRESS_SAVE_INTERVAL) {
        saveProgress();
      }

      drawReading();
    } else {
      // Kitabın sonu
      saveProgress();
      rewind20Words();

      pauseSelection = 0;
      state = STATE_PAUSED;

      drawPausedCurrentPage();
    }
  }
}

// ============================================================
// PAUSED MENÜSÜ OLAYLARI
// ============================================================

void handlePaused() {
  // FORWARD: sonraki seçenek
  if (takeForwardEvent()) {
    pauseSelection++;

    if (pauseSelection > 4) {
      pauseSelection = 0;
    }

    drawPausedCurrentPage();
    return;
  }

  if (!takePlayEvent()) {
    return;
  }

  // RESUME
  if (pauseSelection == 0) {
    state = STATE_READING;
    lastWordTime = millis();

    drawReading();
    return;
  }

  // REWIND
  if (pauseSelection == 1) {
    if (gotoWord(rewindTargetWord)) {
      if (!readNextWord()) {
        gotoWord(rewindTargetWord);
        readNextWord();
      }

      currentWordNumber = rewindTargetWord;

      saveProgress();
    }

    pauseSelection = 0;

    rewind20Words();
    drawPausedCurrentPage();
    return;
  }

  // SLEEP
  if (pauseSelection == 2) {
    enterSleep();
    return;
  }

  // GO BACK
  if (pauseSelection == 3) {
    enterHome();
    drawHome();
    return;
  }

  // RESTART
  if (pauseSelection == 4) {
    restartCurrentBook();
    return;
  }
}

// ============================================================
// SLEEP OLAYLARI
//
// İki buton aynı anda en az debounce süresi boyunca basılmalı.
// ============================================================

void handleSleep() {
  forwardEvent = false;
  playEvent = false;

  bool forwardDown =
    digitalRead(BUTTON_FORWARD) == LOW;

  bool playDown =
    digitalRead(BUTTON_PLAY) == LOW;

  bool bothDown = forwardDown && playDown;

  uint32_t now = millis();

  if (bothDown) {
    if (!sleepWakeTiming) {
      sleepWakeTiming = true;
      sleepBothPressedSince = now;
    } else if (
      now - sleepBothPressedSince >= BUTTON_DEBOUNCE_MS
    ) {
      wakeFromSleep();
    }
  } else {
    sleepWakeTiming = false;
    sleepBothPressedSince = 0;
  }
}

// ============================================================
// SETUP
// ============================================================

void setup() {
  pinMode(BUTTON_FORWARD, INPUT_PULLUP);
  pinMode(BUTTON_PLAY, INPUT_PULLUP);

  Wire.begin();

  oled.begin();
  oled.enableUTF8Print();
  oled.clearBuffer();
  oled.setFont(u8g2_font_6x10_tf);

  oled.drawStr(0, 12, "RSVP READER");
  oled.drawStr(0, 25, "STARTING...");
  oled.sendBuffer();

  delay(700);

  // LittleFS
  if (!LittleFS.begin()) {
    oled.clearBuffer();
    oled.setFont(u8g2_font_6x10_tf);

    oled.drawStr(0, 12, "LITTLEFS ERROR");
    oled.drawStr(0, 25, "CHECK FLASH");
    oled.sendBuffer();

    while (true) {
      delay(1000);
    }
  }

  if (!LittleFS.exists(SYSTEM_DIR)) {
    LittleFS.mkdir(SYSTEM_DIR);
  }

  loadSpeedConfig();
  scanBooks();

  String lastBook = loadLastBook();

  lastPlayedBook = -1;

  if (lastBook.length() > 0) {
    for (uint8_t i = 0; i < bookCount; i++) {
      if (books[i] == lastBook) {
        lastPlayedBook = i;
        selectedBook = i;
        break;
      }
    }
  }

  if (lastPlayedBook < 0) {
    selectedBook = 0;
  }

  lastForwardReading = digitalRead(BUTTON_FORWARD);
  lastPlayReading = digitalRead(BUTTON_PLAY);

  lastForwardChange = millis();
  lastPlayChange = millis();

  forwardPressed = false;
  playPressed = false;

  forwardEvent = false;
  playEvent = false;

  enterHome();
  drawHome();
}

// ============================================================
// LOOP
// ============================================================

void loop() {
  updateButtons();

  switch (state) {
    case STATE_HOME:
      handleHome();
      break;

    case STATE_BOOK_LIST:
      handleBookList();
      break;

    case STATE_RESUME_MENU:
      handleResumeMenu();
      break;

    case STATE_READING:
      handleReading();
      break;

    case STATE_PAUSED:
      handlePaused();
      break;

    case STATE_SPEED_MENU:
      handleSpeedMenu();
      break;

    case STATE_SLEEP:
      handleSleep();
      break;
  }

  delay(1);
}