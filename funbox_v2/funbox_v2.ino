// https://github.com/miguelbalboa/rfid/issues/352#issue-282870788

// Header
#include <SPI.h>
#include <MFRC522.h>
#include <TimerOne.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// Define
#define RST_PIN 9 // Configurable, see typical pin layout above
#define SS_PIN 10 // Configurable, see typical pin layout above
#define BUZZER_PIN 3

// Global variables
MFRC522 mfrc522(SS_PIN, RST_PIN);                              // Create MFRC522 instance
LiquidCrystal_I2C lcd(0x27, 2, 1, 0, 4, 5, 6, 7, 3, POSITIVE); // LCD

bool rfid_tag_present_prev = false;
bool rfid_tag_present = false;
int _rfid_error_counter = 0;
bool _tag_found = false;

bool stage_ahead = false;
bool END = false;
int second = 20;
int minute = 0;
int halfsecond = 2;
int beeptoggle = 0;
int countdown = 0; // second
int sensing_time = 0;
int current_stage = 0;

// Enum
enum PLAY_MODE_STAGE
{
  EXP_MODE_STAGE0 = 0,
  EXP_MODE_STAGE1 = 1,
  EXP_MODE_STAGE2 = 2,
  DOM_MODE_STAGE0 = 3,

} playstage;

enum STAGE_CNTDWN_TIMING
{
  EXP_MODE_STAGE0_TIMING = 600, // 10min
  EXP_MODE_STAGE1_TIMING = 30,  // 30sec
  EXP_MODE_STAGE2_TIMING = 0,
  DOM_MODE_STAGE0_TIMING = 600, // 10min

} cnt_time;

enum EXP_JUDGE_MODE
{
  EXP_MODE_ALPHA_WIN = 0,
  EXP_MODE_ALPHA_LOSE = 1,
  EXP_MODE_BRAVO_WIN = 2,

} exp_judge;

enum PLAYBACK_MODE
{
  START = 8, // I set 0001_START.mp3 and fucking dont know why got 8 here
  ALPHA_TEAM_LOSE_EXPLOSION = 7,
  ALPHA_TEAM_WIN_EXPLOSION = 6,
  BRAVO_TEAM_WIN = 5,
  MOUNTING_BOMB_SIREN = 4,
  BOMB_MOUNTED_SIREN = 3,
  GROUNDED_SIREN = 1,

} playback;

enum RFID_CARD_UID
{
  ALPHA1 = 0x02,
  //  ALPHA2 = 0x??,
  BRAVO1 = 0x53,
  BRAVO2 = 0xDB,
  MOUNT1 = 0x63,
  MOUNT2 = 0x22,

} card_uid;

void setup()
{
  Serial.begin(115200); // Initialize serial communications with the PC
  while (!Serial)
    ;          // Do nothing if no serial port is opened (added for Arduinos based on ATMEGA32U4)
  SPI.begin(); // Init SPI bus

  // RFID
  mfrc522.PCD_Init(); // Init MFRC522

  // LCD
  lcd.begin(16, 2);
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Milsig Taiwan");
  delay(1000);
  lcd.setCursor(0, 1);
  lcd.print("MADSDK");
  delay(1000);

  // Buzzer
  pinMode(BUZZER_PIN, OUTPUT); // BUZZER
  beep_short(3);

  // ISR
  Timer1.initialize(500000); // 0.5s
  Timer1.attachInterrupt(TimingISR);

  // Countdown
  countdown = EXP_MODE_STAGE0_TIMING;
}

// current_stage
void loop()
{

  if (halfsecond > 0)
  {
    Serial.print("countdown: ");
    Serial.println(countdown);
    Serial.print("current_stage: ");
    Serial.println(current_stage);

    exp_lcd_handling(countdown, sensing_time, current_stage);
  }

  stage_ahead = rfid_sensing(current_stage);

  if (stage_ahead && current_stage == EXP_MODE_STAGE0)
  {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("MOUNTING DONE!");
    countdown = 999;

    //      Serial.print(F("Card UID:"));
    //      dump_byte_array(mfrc522.uid.uidByte, mfrc522.uid.size); // 顯示卡片的UID
    //      Serial.println();
    //      Serial.print(F("PICC type: "));
    //      MFRC522::PICC_Type piccType = mfrc522.PICC_GetType(mfrc522.uid.sak);
    //      Serial.println(mfrc522.PICC_GetTypeName(piccType));  //顯示卡片的類型

    mfrc522.PICC_HaltA();

    sensing_time = 0;
    current_stage++;
    delay(1000);
    countdown = EXP_MODE_STAGE1_TIMING;
  }
  else if (stage_ahead)
  {
    current_stage++;
  }

  /*
    EXP_MODE_STAGE0 = 0,
    EXP_MODE_STAGE1 = 1,
    EXP_MODE_STAGE2 = 2,
    DOM_MODE_STAGE0 = 3,
  */
  switch (current_stage)
  {
  case EXP_MODE_STAGE0:
    //
    if (countdown <= 0)
    {
      Serial.println("ALPHA LOSE");
      exp_lcd_judge(EXP_MODE_ALPHA_LOSE);
      //        delay(8000);
    }
    break;
  case EXP_MODE_STAGE1:
    // playback BOMB_MOUNTED_SIREN if mp3 idle
    if (countdown <= 0)
    {
      Serial.println("ALPHA WIN");
      exp_lcd_judge(EXP_MODE_ALPHA_WIN);
      //        delay(8000);
    }
    break;
  case EXP_MODE_STAGE2:
    //
    Serial.println("BRAVO WIN");
    exp_lcd_judge(EXP_MODE_BRAVO_WIN);
    END = 1;
    //      delay(8000);
    break;
  default:
    Serial.println("Invalid stage number");
  }

  if (END)
  {
    beep_short(10);
    while (1)
      ;
  }
}

bool rfid_sensing(int stage)
{
  int tlast;

  rfid_tag_present_prev = rfid_tag_present;

  _rfid_error_counter += 1;
  if (_rfid_error_counter > 2)
  {
    _tag_found = false;
  }

  // Detect Tag without looking for collisions
  byte bufferATQA[2];
  byte bufferSize = sizeof(bufferATQA);

  // Reset baud rates
  mfrc522.PCD_WriteRegister(mfrc522.TxModeReg, 0x00);
  mfrc522.PCD_WriteRegister(mfrc522.RxModeReg, 0x00);
  // Reset ModWidthReg
  mfrc522.PCD_WriteRegister(mfrc522.ModWidthReg, 0x26);

  MFRC522::StatusCode result = mfrc522.PICC_RequestA(bufferATQA, &bufferSize);

  if (result == mfrc522.STATUS_OK)
  {
    if (!mfrc522.PICC_ReadCardSerial())
    { // Since a PICC placed get Serial and continue
      return;
    }
    _rfid_error_counter = 0;
    _tag_found = true;
  }

  rfid_tag_present = _tag_found;

  if (!cmp_stage_match(stage, mfrc522.uid.uidByte[3]))
  {
    //    Serial.print("false!!");
    sensing_time = 0;
    return false;
  }
  else
  {
    //    Serial.print("true!!");
  }

  // rising edge
  if (rfid_tag_present && !rfid_tag_present_prev)
  {
    sensing_time = 0;
    Serial.print("_tag_found: ");
    Serial.print(_tag_found);
    Serial.print(", ");

    Serial.print("Tag 0x");
    Serial.print(mfrc522.uid.uidByte[3], HEX);
    Serial.println(" found");

    tlast = millis();
  }

  // falling edge
  if (!rfid_tag_present && rfid_tag_present_prev)
  {
    sensing_time = 0;
    Serial.print("_tag_found: ");
    Serial.print(_tag_found);
    Serial.print(", ");

    tlast = millis() - tlast;
    Serial.print("Tag gone: ");
    Serial.println(tlast);
  }

  //  if(sensing_time>0){
  //    Serial.print("sensing_time: ");
  //    Serial.println(sensing_time);
  //  }

  if (sensing_time > 3000)
  {
    Serial.print("sensing_time: ");
    Serial.println(sensing_time);
    sensing_time = 0;
    return true;
  }
  else
    return false;
}

void TimingISR()
{ // 0.5s

  if (END)
  {
    digitalWrite(BUZZER_PIN, LOW);
    return;
  }

  halfsecond--;

  if (_tag_found)
  {
    sensing_time += 500;
  }

  //  Serial.print("countdown: ");
  //  Serial.println(countdown);

  beeptoggle = (~beeptoggle);
  if (beeptoggle)
  {
    digitalWrite(BUZZER_PIN, HIGH);
  }
  else
  {
    digitalWrite(BUZZER_PIN, LOW);
  }

  if (halfsecond == 0)
  {
    countdown--;

    if (countdown <= 0)
    {
      END = true;
    }

    second--;

    if (second == 0)
    {
      minute--;
      second = 60;
    }

    halfsecond = 2;
  }
}

/*
  ALPHA1 = 0x02,
  BRAVO1 = 0x53,
  BRAVO2 = 0xDB,
  MOUNT1 = 0x63,
  MOUNT2 = 0x22,

  EXP_MODE_STAGE0 = 0,
  EXP_MODE_STAGE1 = 1,
  EXP_MODE_STAGE2 = 2,
  DOM_MODE_STAGE0 = 3,
*/

void exp_lcd_judge(int exp_judge_mode)
{
  lcd.clear();
  lcd.setCursor(0, 0);

  switch (exp_judge_mode)
  {
  case EXP_MODE_ALPHA_WIN:
    lcd.print("ALPHA WIN");
    break;
  case EXP_MODE_ALPHA_LOSE:
    lcd.print("ALPHA LOSE");
    break;
  case EXP_MODE_BRAVO_WIN:
    lcd.print("BRAVO WIN");
    break;
  default:
    lcd.print("Invalid");
  }
}
void exp_lcd_handling(int cntdwn, int sensing_t, int stage)
{

  int whitespace = 6;
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("COUNTDOWN: ");
  lcd.print(cntdwn);
  lcd.print("s");

  Serial.println(sensing_t);

  if (sensing_t > 0 && stage == EXP_MODE_STAGE0)
  {
    lcd.setCursor(0, 1);
    lcd.print("MOUNTING: ");
    while (sensing_t > 0)
    {

      lcd.print("O");
      whitespace--;
      sensing_t -= 500;
    }
    while (whitespace > 0)
    {
      lcd.print("X");
      whitespace--;
    }
  }
  else if (sensing_t > 0 && stage == EXP_MODE_STAGE1)
  {
    lcd.setCursor(0, 1);
    lcd.print("UNMNTING: ");
    while (sensing_t > 0)
    {

      lcd.print("O");
      whitespace--;
      sensing_t -= 500;
    }
    while (whitespace > 0)
    {
      lcd.print("X");
      whitespace--;
    }
  }
  else if (sensing_t <= 0 && stage == EXP_MODE_STAGE0)
  {
    lcd.setCursor(0, 1);
    lcd.print("WAIT MOUNTING...");
  }
  else if (sensing_t <= 0 && stage == EXP_MODE_STAGE1)
  {
    lcd.setCursor(0, 1);
    lcd.print("WAIT UNMNTING...");
  }

  return;
}
bool cmp_stage_match(int stage, byte uid)
{
  int uid32 = (int)uid;
  //  Serial.print("got uid!!!!!!!!!!: 0x");
  //  Serial.println(uid32, HEX);
  //  Serial.print("stage: ");
  //  Serial.println(stage);

  if (stage == EXP_MODE_STAGE0 && (uid == MOUNT1 || uid == MOUNT2))
  {
    //    Serial.println("EXP_MODE_STAGE0 pass");
    return true;
  }
  else if (stage == EXP_MODE_STAGE1 && (uid == BRAVO1 || uid == BRAVO2))
  {
    //    Serial.println("EXP_MODE_STAGE1 pass");
    return true;
  }
  else
  {
    return false;
  }
}

void beep_short(int count)
{
  while (count--)
  {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(100);
    digitalWrite(BUZZER_PIN, LOW);
    delay(100);
  }
  digitalWrite(9, LOW);
}
void beep_long(int duration)
{
  digitalWrite(BUZZER_PIN, HIGH);
  delay(duration * 1000);
  digitalWrite(BUZZER_PIN, LOW);

  delay(100);
}
void mfrc522_fast_Reset()
{
  digitalWrite(RST_PIN, HIGH);
  mfrc522.PCD_Reset();
  mfrc522.PCD_WriteRegister(mfrc522.TModeReg, 0x80);      // TAuto=1; timer starts automatically at the end of the transmission in all communication modes at all speeds
  mfrc522.PCD_WriteRegister(mfrc522.TPrescalerReg, 0x43); // 10µs.

  mfrc522.PCD_WriteRegister(mfrc522.TReloadRegH, 0x00); // Reload timer with 0x01E = 30, ie 0.3ms before timeout.
  mfrc522.PCD_WriteRegister(mfrc522.TReloadRegL, 0x1E);
  mfrc522.PCD_WriteRegister(mfrc522.TReloadRegL, 0x5);

  mfrc522.PCD_WriteRegister(mfrc522.TxASKReg, 0x40); // Default 0x00. Force a 100 % ASK modulation independent of the ModGsPReg register setting
  mfrc522.PCD_WriteRegister(mfrc522.ModeReg, 0x3D);  // Default 0x3F. Set the preset value for the CRC coprocessor for the CalcCRC command to 0x6363 (ISO 14443-3 part 6.2.4)

  mfrc522.PCD_AntennaOn(); // Enable the antenna driver pins TX1 and TX2 (they were disabled by the reset)
}
void dump_byte_array(byte *buffer, byte bufferSize)
{
  for (byte i = 0; i < bufferSize; i++)
  {
    Serial.print(buffer[i] < 0x10 ? " 0" : " ");
    Serial.print(buffer[i], HEX);
  }
}
