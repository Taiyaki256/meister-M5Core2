#include <M5Unified.h>
#include <SPI.h>
#include <MFRC522.h>
#include <WiFi.h>
#include <FirebaseClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include "secrets.h"

#define RST_PIN 13 // Core2のリセットピン
#define SS_PIN 32  // Core2のGPIO5

MFRC522 mfrc522(SS_PIN, RST_PIN); // RC522インスタンス作成

WiFiClientSecure ssl;

#define FIREBASE_PROJECT_ID "hengen-zizai"

void asyncCB(AsyncResult &aResult);

void printResult(AsyncResult &aResult);

DefaultNetwork network; // initilize with boolean parameter to enable/disable network reconnection

UserAuth user_auth(API_KEY, USER_EMAIL, USER_PASSWORD);

FirebaseApp app;

using AsyncClient = AsyncClientClass;

AsyncClient aClient(ssl, getNetwork(network));

Firestore::Documents Docs;

enum AppMode
{
  READ_MODE,
  DISPLAY_MODE,
  SETTING_MODE,
  COMMUNICATION_MODE
};

enum Sex
{
  MALE,
  FEMALE,
  OTHER
};

AppMode currentMode = READ_MODE;
String lastUID = "";
Sex sex = OTHER;
int age = 0;

String BAND_UUID = "7UuBeocmw70fngVx1ko7";

bool stampStatus[5] = {false, false, false, false, false};
int stampCount = 5;
int stampM = 0;    // このチェックポイントのID
bool task = false; // debug用

void settingMode()
{
  M5.Display.startWrite();
  M5.Display.clear(BLACK);
  M5.Display.drawString("設定モード", M5.Display.width() / 2, 30);
  M5.Display.endWrite();
}

void readMode()
{
  M5.Display.startWrite();
  auto black = M5.Display.color565(64, 64, 64);
  M5.Display.clear(BLACK);
  M5.Display.setFont(&fonts::lgfxJapanGothic_32);
  auto color = M5.Display.color565(235, 54, 93);
  M5.Display.setTextColor(color);
  M5.Display.drawString("NFCを近づけてね！", M5.Display.width() / 2, M5.Display.height() / 2);
  M5.Display.endWrite();
}

void displayMode()
{
  M5.Display.startWrite();
  auto color = M5.Display.color565(126, 205, 246);
  M5.Display.clear(color);
  M5.Display.setFont(&fonts::lgfxJapanGothicP_32);
  auto ycolor = M5.Display.color565(22, 27, 28);
  M5.Display.setTextColor(ycolor);
  M5.Display.drawString("スタンプラリー", M5.Display.width() / 2, 30);
  // M5.Display.drawString(lastUID, M5.Display.width() / 2, M5.Display.height() / 2 - 60);
  String sexStr = (sex == MALE) ? "男" : (sex == FEMALE) ? "女"
                                                         : "その他";
  M5.Display.drawString("性別：" + sexStr + " 年齢：" + String(age), M5.Display.width() / 2, 60);

  // スタンプ表示の改良版
  const int radius = 20;
  const int spacing = 45;                  // 円同士の間隔
  const int line = 1;                      // 表示行数
  int yPos = M5.Display.height() / 2 - 10; // 表示開始Y位置

  int nx = 0;
  int ny = 0;
  for (int l = 0; l < line; l++)
  {
    // 1行あたりの表示数（最終行は残り）
    int count = (l == line - 1) ? (stampCount - (line - 1) * (stampCount / line)) : stampCount / line;
    int startX = (M5.Display.width() - ((count - 1) * spacing)) / 2;

    for (int i = 0; i < count; i++)
    {
      int index = l * (stampCount / line) + i;
      auto w = M5.Display.color565(33, 65, 74);
      if (stampStatus[index])
      {
        auto g = M5.Display.color565(98, 235, 7);
        M5.Display.fillCircle(startX + (i * spacing), yPos, radius, w);
        M5.Display.fillCircle(startX + (i * spacing), yPos, radius - 4, g);
      }
      else
      {
        M5.Display.fillCircle(startX + (i * spacing), yPos, radius, w);
        M5.Display.fillCircle(startX + (i * spacing), yPos, radius - 4, color);
      }
      if (index == stampM)
      {
        nx = startX + (i * spacing);
        ny = yPos;
      }
    }
    yPos += spacing + 10; // 行間の余白追加
  }
  M5.Display.endWrite();

  M5.Display.startWrite();
  for (int i = 0; i < 4; i++)
  {
    auto g = M5.Display.color565(98, 235, 7);
    auto c = M5.Display.color565(0, 163, 28);
    M5.Display.fillCircle(nx, ny, radius - 4, c);
    delay(500);
    M5.Display.fillCircle(nx, ny, radius - 4, g);
    delay(500);
  }
  M5.Display.endWrite();
}

void communicationMode()
{
  M5.Display.startWrite();
  M5.Display.clear(BLACK);
  M5.Display.setFont(&fonts::lgfxJapanGothicP_32);
  auto ycolor = M5.Display.color565(235, 54, 93);
  M5.Display.setTextColor(ycolor);
  M5.Display.drawString("通信中...", M5.Display.width() / 2, 30);
  M5.Display.endWrite();
}

void setup()
{
  auto cfg = M5.config();
  M5.begin(cfg);
  SPI.begin();
  mfrc522.PCD_Init();
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.begin(115200);

  // M5GFX設定
  auto &display = M5.Display;
  display.setFont(&fonts::lgfxJapanGothic_24);
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setEpdMode(epd_mode_t::epd_fastest);

  display.startWrite();
  display.clear();
  display.setTextDatum(middle_center);
  display.drawString("NFCリーダー準備完了", display.width() / 2, 30);
  // WiFi接続処理
  M5.Display.drawString("WiFi接続中...", M5.Display.width() / 2, 110);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    M5.Display.print(".");
    Serial.print(".");
  }
  Serial.println();
  Serial.print("Connected with IP: ");
  Serial.println(WiFi.localIP());
  Serial.println();
  M5.Display.drawString("接続成功!", M5.Display.width() / 2, 140);

  // NTPサーバーから時刻を取得
  configTime(9 * 3600, 0, "ntp.nict.jp", "ntp.jst.mfeed.ad.jp");
  struct tm timeInfo;
  if (!getLocalTime(&timeInfo))
  {
    Serial.println("Failed to obtain time");
    M5.Display.drawString("時刻取得失敗", M5.Display.width() / 2, 170);
  }
  else
  {
    Serial.println(&timeInfo, "%Y-%m-%d %H:%M:%S");
  }
  display.endWrite();

  M5.Speaker.tone(1200, 50);
  delay(50);
  M5.Speaker.tone(2000, 50);
  delay(50);

  Serial.printf("Firebase Client v%s\n", FIREBASE_CLIENT_VERSION);
  ssl.setInsecure();
  Serial.println("Initializing the app...");
  initializeApp(aClient, app, getAuth(user_auth), asyncCB, "authTask");
  app.getApp<Firestore::Documents>(Docs);

  delay(1000);
  currentMode = READ_MODE;
  readMode();
}

void parseNDEF(byte *data, int length) {
    int index = 0;
    while (index < length) {
        if (data[index] == 0x03) {  // NDEF開始 (0x03: NDEF Message TLV)
            int ndef_length = data[index + 1];  // NDEF全体の長さ
            index += 2;  // 0x03 と長さの部分をスキップ

            while (index < ndef_length + 2) {  // NDEF長さ内でループ
                byte tnf = data[index] & 0x7;  // TNF（Type Name Format）
                byte type_length = data[index + 1];   // Typeの長さ
                byte payload_length = data[index + 2]; // ペイロードの長さ
                byte record_type = data[index + 3];    // Record Type

                if (record_type == 0x54) {  // 'T' (Text Record)
                    byte lang_length = data[index + 4] & 0x3F; // 言語コードの長さ
                    Serial.print("言語コード: ");
                    for (byte i = 0; i < lang_length; i++) {
                        Serial.write(data[index + 5 + i]);  // 言語コード表示
                    }
                    Serial.println();

                    Serial.print("テキストデータ: ");
                    String text = "";
                    for (byte i = lang_length + 5; i < payload_length + 4; i++) {
                        Serial.write(data[index + i]);  // テキストデータ表示
                        text += (char)data[index + i];
                    }
                    BAND_UUID = text;
                    Serial.println("\n----------------------");
                }

                // 次のレコードへ移動
                index += 3 + type_length + payload_length;
            }
        }
        index++;
    }
    Serial.println("NDEF解析終了");
}

void loop()
{
  M5.update();
  app.loop();

  Docs.loop();

  if (app.ready() && !task)
  {
    Serial.println("Firebase ready");
    task = true;
  }
  // {
  //   Serial.println("Firebase ready");
  //   task = true;
  //   String documentPath = "checkpoints/1A/checked/LOSTnYJNcgZaanAwmmQ";
  //   Docs.get(aClient, Firestore::Parent(FIREBASE_PROJECT_ID), documentPath, GetDocumentOptions(), asyncCB, "getTask");
  // }
  switch (currentMode)
  {
  case READ_MODE:
    if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial())
    {
      MFRC522::PICC_Type piccType = mfrc522.PICC_GetType(mfrc522.uid.sak);
      Serial.println(mfrc522.PICC_GetTypeName(piccType));
      // チップのステータスを取得するための変数
      MFRC522::StatusCode status;
      byte full_data[128];  // 8ブロック分（最大128バイト）保存
      byte read_buffer[18]; // 1ブロック（16バイト）
      byte read_buffer_size = sizeof(read_buffer);
      int full_data_index = 0;

      Serial.println(F("データ読み取り中 ... "));
      Serial.println();

      // NDEFデータ取得（複数ブロックを連結）
      for (int n = 4; n < 25; n++) {  
          MFRC522::StatusCode status = (MFRC522::StatusCode)mfrc522.MIFARE_Read(n, read_buffer, &read_buffer_size);
          if (status != MFRC522::STATUS_OK) {
              Serial.print(F("読み込み失敗:"));
              Serial.println(mfrc522.GetStatusCodeName(status));
              return;
          }
          // 読み取ったデータを full_data に追加
          for (int i = 0; i < 4; i++) {
              if (full_data_index < sizeof(full_data)) {
                  full_data[full_data_index++] = read_buffer[i];
              }
          }
          // 0xFE（NDEF終端）を検出したら終了
          if (read_buffer[0] == 0xFE) break;
      }
      mfrc522.PICC_HaltA();

      // NDEFデータ解析（複数レコード対応）
      parseNDEF(full_data, full_data_index);

      // UID取得
      lastUID = "";
      for (byte i = 0; i < mfrc522.uid.size; i++)
      {
        lastUID += String(mfrc522.uid.uidByte[i] < 0x10 ? "0" : "");
        lastUID += String(mfrc522.uid.uidByte[i], HEX);
      }

      M5.Speaker.tone(2000, 100);
      currentMode = COMMUNICATION_MODE;
      communicationMode();

      BatchGetDocumentOptions options;
      options.documents("bands/" + BAND_UUID);
      for (int i = 0; i < 2; i++)
      {
        options.documents("checkpoints/1" + String(char('A'+i)) + "/checked/" + BAND_UUID);
      }

      Serial.println("Getting multiple documents...");
      Docs.batchGet(aClient, Firestore::Parent(FIREBASE_PROJECT_ID), options, asyncCB, "checkpoints/batchGetTask");

      time_t now;
      time(&now);

      String timestamp = String(now * 1000LL);              // 明示的にlong long型を使用
      Serial.println("Calculated timestamp: " + timestamp); // デバッグ用出力

      Values::StringValue tsV(timestamp);
      Document<Values::Value> doc("timestamp", Values::Value(tsV));
      Serial.println("生成タイムスタンプ: " + timestamp);
      String documentPath = "checkpoints/1" + String(char('A'+stampM)) + "/checked/" + BAND_UUID;
      AsyncResult aResult_no_callback;
      Docs.createDocument(aClient, Firestore::Parent(FIREBASE_PROJECT_ID), documentPath, DocumentMask(), doc, aResult_no_callback);
    }
    break;

  case DISPLAY_MODE:
    delay(1000);
    M5.Speaker.tone(800, 50); // 追加
    currentMode = READ_MODE;
    readMode();
    // ボタンクリック音（800Hzで50ms）
    break;

  case COMMUNICATION_MODE:
    break;
  }

  delay(100);
}

void asyncCB(AsyncResult &aResult) { printResult(aResult); }

void printResult(AsyncResult &aResult)
{
  if (aResult.isEvent())
  {
    Serial.printf("Event task: %s, msg: %s, code: %d\n", aResult.uid().c_str(), aResult.appEvent().message().c_str(), aResult.appEvent().code());
  }

  if (aResult.isDebug())
  {
    Serial.printf("Debug task: %s, msg: %s\n", aResult.uid().c_str(), aResult.debug().c_str());
  }

  if (aResult.isError())
  {
    Serial.printf("Error task: %s, msg: %s, code: %d\n", aResult.uid().c_str(), aResult.error().message().c_str(), aResult.error().code());
  }

  if (aResult.available())
  {
    Serial.printf("task: %s, payload: %s\n", aResult.uid().c_str(), aResult.c_str());
    if (aResult.uid() == "checkpoints/batchGetTask")
    {
      // JSONパース処理の追加
      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, aResult.payload());

      if (!error)
      {
        int i = 0;
        JsonArray results = doc.as<JsonArray>();
        for (JsonObject result : results)
        {
          if (result.containsKey("found"))
          {
            JsonObject document = result["found"];
            String name = document["name"].as<String>();
            String timestamp = document["fields"]["timestamp"]["stringValue"].as<String>();

            if (name.indexOf("bands/") != -1)
            {
              String dsex = document["fields"]["sex"]["stringValue"].as<String>();
              int dage = document["fields"]["age"]["integerValue"].as<int>();
              Serial.printf("Document found: %s\n", name.c_str());
              Serial.printf("Sex: %s, Age: %d\n", dsex.c_str(), dage);
              if (dsex == "man")
              {
                sex = MALE;
              }
              else if (dsex == "woman")
              {
                sex = FEMALE;
              }
              else
              {
                sex = OTHER;
              }
              age = dage;

              continue;
            }

            Serial.printf("Document found: %s\n", name.c_str());
            Serial.printf("Timestamp: %s\n", timestamp.c_str());
            stampStatus[i] = true;
          }
          else if (result.containsKey("missing"))
          {
            String missingDoc = result["missing"].as<String>();
            Serial.printf("Document missing: %s\n", missingDoc.c_str());
            stampStatus[i] = false;
          }
          i++;
        }

        M5.Speaker.tone(1000, 50);
        delay(50);
        M5.Speaker.tone(2000, 50);
        delay(50);
        M5.Speaker.tone(2000, 50);
        currentMode = DISPLAY_MODE;
        displayMode();
        // 音を鳴らす
      }
      else
      {
        Serial.printf("JSON parse error: %s\n", error.c_str());
      }
    }
  }
}