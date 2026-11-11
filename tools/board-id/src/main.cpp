// Diagnostic: identify the CYD's display controller and touch chip.
// Prints results to serial at 115200.
#include <Arduino.h>
#include <SPI.h>

// Display (HSPI)
static const int LCD_SCK = 14, LCD_MOSI = 13, LCD_MISO = 12, LCD_CS = 15, LCD_DC = 2, LCD_BL = 21;
// Resistive touch XPT2046 (bit-banged, standard CYD pins)
static const int T_CLK = 25, T_MOSI = 32, T_MISO = 39, T_CS = 33, T_IRQ = 36;

SPIClass lcd(HSPI);

void lcdRead(uint8_t cmd, uint8_t *out, int n) {
  lcd.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
  digitalWrite(LCD_CS, LOW);
  digitalWrite(LCD_DC, LOW);
  lcd.transfer(cmd);
  digitalWrite(LCD_DC, HIGH);
  for (int i = 0; i < n; i++) out[i] = lcd.transfer(0);
  digitalWrite(LCD_CS, HIGH);
  lcd.endTransaction();
}

void lcdCmd(uint8_t cmd) {
  lcd.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
  digitalWrite(LCD_CS, LOW);
  digitalWrite(LCD_DC, LOW);
  lcd.transfer(cmd);
  digitalWrite(LCD_CS, HIGH);
  lcd.endTransaction();
}

uint16_t touchRead(uint8_t ctrl) {
  digitalWrite(T_CS, LOW);
  for (int i = 7; i >= 0; i--) {
    digitalWrite(T_MOSI, (ctrl >> i) & 1);
    digitalWrite(T_CLK, HIGH); delayMicroseconds(2);
    digitalWrite(T_CLK, LOW);  delayMicroseconds(2);
  }
  delayMicroseconds(10);
  uint16_t v = 0;
  for (int i = 0; i < 16; i++) {
    digitalWrite(T_CLK, HIGH); delayMicroseconds(2);
    v = (v << 1) | digitalRead(T_MISO);
    digitalWrite(T_CLK, LOW);  delayMicroseconds(2);
  }
  digitalWrite(T_CS, HIGH);
  return v >> 4;  // 12-bit result
}

void dump(const char *label, uint8_t *b, int n) {
  Serial.printf("%-28s", label);
  for (int i = 0; i < n; i++) Serial.printf(" %02X", b[i]);
  Serial.println();
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  pinMode(LCD_CS, OUTPUT); digitalWrite(LCD_CS, HIGH);
  pinMode(LCD_DC, OUTPUT);
  pinMode(LCD_BL, OUTPUT); digitalWrite(LCD_BL, HIGH);
  pinMode(T_CS, OUTPUT); digitalWrite(T_CS, HIGH);
  pinMode(T_CLK, OUTPUT); pinMode(T_MOSI, OUTPUT);
  pinMode(T_MISO, INPUT); pinMode(T_IRQ, INPUT);
  lcd.begin(LCD_SCK, LCD_MISO, LCD_MOSI, -1);

  lcdCmd(0x01);  // software reset
  delay(150);
  lcdCmd(0x11);  // sleep out
  delay(150);

  uint8_t b[5];
  Serial.println("\n===== CYD BOARD ID =====");
  lcdRead(0x04, b, 4); dump("RDDID 0x04 (raw):", b, 4);
  uint32_t raw = (b[0] << 24) | (b[1] << 16) | (b[2] << 8) | b[3];
  uint32_t id = (raw << 1) >> 8;  // strip the 1-bit dummy clock
  Serial.printf("RDDID corrected:             %06X\n", id);
  lcdRead(0xD3, b, 4); dump("RDID4 0xD3 (ILI9341 ID):", b, 4);
  lcdRead(0xDA, b, 2); dump("RDID1 0xDA:", b, 2);
  lcdRead(0xDB, b, 2); dump("RDID2 0xDB:", b, 2);
  lcdRead(0xDC, b, 2); dump("RDID3 0xDC:", b, 2);
  lcdRead(0x0B, b, 2); dump("MADCTL 0x0B:", b, 2);

  uint16_t t0 = touchRead(0x84), t1 = touchRead(0xF4);
  Serial.printf("XPT2046 temp0 / temp1:       %u / %u\n", t0, t1);
  Serial.printf("Touch IRQ pin (1=idle):      %d\n", digitalRead(T_IRQ));
  Serial.printf("Flash: %u MB, PSRAM: %u bytes\n", ESP.getFlashChipSize() >> 20, ESP.getPsramSize());
  Serial.println("===== END =====");
}

void loop() {}
