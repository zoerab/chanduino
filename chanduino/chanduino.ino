#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include <SPI.h>
#include "WiFi.h"
#include "WiFiUdp.h"
#include "WiFiClientSecure.h"
#include "HTTPClient.h"
#include <Wire.h>
#include <Button2.h>
#include <Int64String.h>
#include "esp_adc_cal.h"
#include "EEPROM.h"

#include <TJpg_Decoder.h>

#ifndef TFT_DISPOFF
#define TFT_DISPOFF 0x28
#endif

#ifndef TFT_SLPIN
#define TFT_SLPIN   0x10
#endif

#define TFT_MOSI            19
#define TFT_SCLK            18
#define TFT_CS              5
#define TFT_DC              16
#define TFT_RST             23

#define TFT_BL          4  // Display backlight control pin
#define ADC_EN          14
#define ADC_PIN         34
#define BUTTON_1        35
#define BUTTON_2        0

TFT_eSPI tft = TFT_eSPI(135, 240); // Invoke custom library
Button2 btn1(BUTTON_1);
Button2 btn2(BUTTON_2);

// 4chan root certificate (Baltimore CyberTrust Root)
const char *root_ca = PSTR( \
                            "-----BEGIN CERTIFICATE-----\n" \
                            "MIIDdzCCAl+gAwIBAgIEAgAAuTANBgkqhkiG9w0BAQUFADBaMQswCQYDVQQGEwJJ\n" \
                            "RTESMBAGA1UEChMJQmFsdGltb3JlMRMwEQYDVQQLEwpDeWJlclRydXN0MSIwIAYD\n" \
                            "VQQDExlCYWx0aW1vcmUgQ3liZXJUcnVzdCBSb290MB4XDTAwMDUxMjE4NDYwMFoX\n" \
                            "DTI1MDUxMjIzNTkwMFowWjELMAkGA1UEBhMCSUUxEjAQBgNVBAoTCUJhbHRpbW9y\n" \
                            "ZTETMBEGA1UECxMKQ3liZXJUcnVzdDEiMCAGA1UEAxMZQmFsdGltb3JlIEN5YmVy\n" \
                            "VHJ1c3QgUm9vdDCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBAKMEuyKr\n" \
                            "mD1X6CZymrV51Cni4eiVgLGw41uOKymaZN+hXe2wCQVt2yguzmKiYv60iNoS6zjr\n" \
                            "IZ3AQSsBUnuId9Mcj8e6uYi1agnnc+gRQKfRzMpijS3ljwumUNKoUMMo6vWrJYeK\n" \
                            "mpYcqWe4PwzV9/lSEy/CG9VwcPCPwBLKBsua4dnKM3p31vjsufFoREJIE9LAwqSu\n" \
                            "XmD+tqYF/LTdB1kC1FkYmGP1pWPgkAx9XbIGevOF6uvUA65ehD5f/xXtabz5OTZy\n" \
                            "dc93Uk3zyZAsuT3lySNTPx8kmCFcB5kpvcY67Oduhjprl3RjM71oGDHweI12v/ye\n" \
                            "jl0qhqdNkNwnGjkCAwEAAaNFMEMwHQYDVR0OBBYEFOWdWTCCR1jMrPoIVDaGezq1\n" \
                            "BE3wMBIGA1UdEwEB/wQIMAYBAf8CAQMwDgYDVR0PAQH/BAQDAgEGMA0GCSqGSIb3\n" \
                            "DQEBBQUAA4IBAQCFDF2O5G9RaEIFoN27TyclhAO992T9Ldcw46QQF+vaKSm2eT92\n" \
                            "9hkTI7gQCvlYpNRhcL0EYWoSihfVCr3FvDB81ukMJY2GQE/szKN+OMY3EU/t3Wgx\n" \
                            "jkzSswF07r51XgdIGn9w/xZchMB5hbgF/X++ZRGjD8ACtPhSNzkE1akxehi/oCr0\n" \
                            "Epn3o0WC4zxe9Z2etciefC7IpJ5OCBRLbf1wbWsaY71k5h+3zvDyny67G7fyUIhz\n" \
                            "ksLi4xaNmjICq44Y3ekQEe5+NauQrz4wlHrQMz2nZQ/1/I6eYs9HRCwBXbsdtTLS\n" \
                            "R9I4LtD+gdwyah617jzV/OeBHRnDJELqYzmp\n" \
                            "-----END CERTIFICATE-----\n");

// Web client settings
const int httpPort = 443;
const char *host = "a.4cdn.org";
const String useragent = "Chanduino/0.2";

// Wifi config webserver variables
WiFiServer server(80);
String header;

// How long to hold down button for secondary action
const int buttonDelay = 350;

// Current board
String board = "";
// Current thread
int thread = 0;
// Posts per page of current board
int perPage = 15;
bool foundPost = false;
// If a post is too long, split it into multiple "pages" (-1 = no split)
int multiPage = -1;

char buff[512];

int replies[2001];
int currentreply = 0;
int maxreply = 0;
String tim = "";

// Current board settings
int bgcolor = 0xD6DE; //0xF71A

/**
 * viewMode:
 * 1 - Browse replies in thread
 * 2 - Browse threads in board
 * 3 - Browse boards
 */
int viewMode = 1;
int wifiMode = 0;

// Board list cache
std::vector<String> boards_ds;
std::vector<bool> boards_ws;

// For loading images
uint8_t PicArray[15000] = {0};

WiFiClientSecure client;

/* Chan stuff */

/**
 * Handles buttonpresses and navigation.
 */
void button_init() {
  // UP button
  btn1.setReleasedHandler([](Button2 & b) {
    if (wifiMode == 0) {
      if (connect_wifi()) {
        unsigned int time = b.wasPressedFor();
        if (time < buttonDelay) {
          if (viewMode == 1 || viewMode == 2 || viewMode == 3) {
            if (multiPage < 1 || viewMode != 1) {
              multiPage = -1;
              currentreply--;
            } else {
              multiPage--;
            }
            if (currentreply < 0) {
              currentreply = maxreply;
            }
            if (viewMode == 3) {
              show_boards();
            } else {
              load_reply();
            }
          }
        } else {
          multiPage = -1;
          if (viewMode == 1) {
            viewMode = 2;
            load_posts();
            load_reply();
            //load_threads();
          } else if (viewMode == 2) {
            viewMode = 3;
            bgcolor = 0x2104; //light 0x2104 dark 0x18C3 green 0x554A red 0xDAAA
            currentreply = 0;
            maxreply = -2;
            show_boards();
          } else if (viewMode == 3) {
            currentreply += 12;
            if (currentreply > maxreply) {
              currentreply = 0;
            }
            show_boards();
          }
        }
      }
    } else {
      wifiMode = 0;
      WiFi.mode(WIFI_STA);
      if (connect_wifi()) {
        if (viewMode == 3) {
          show_boards();
        } else {
          tft.setTextColor(0x0000, bgcolor);
          tft.drawString("Connection restored!", tft.width() / 2, tft.height() / 2);
        }
        return;
      }
    }
  });

  // DOWN button
  btn2.setReleasedHandler([](Button2 & b) {
    if (wifiMode == 0) {
      if (connect_wifi()) {
        unsigned int time = b.wasPressedFor();
        if (time < buttonDelay) {
          if (viewMode == 1 || viewMode == 2 || viewMode == 3) {
            if (multiPage == -1 || viewMode != 1) {
              currentreply++;
            } else {
              multiPage++;
            }
            if (currentreply > maxreply) {
              currentreply = 0;
            }
            if (viewMode == 3) {
              show_boards();
            } else {
              load_reply();
            }
          }
        } else {
          multiPage = -1;
          if (viewMode == 1) {
            draw_img(1);
          } else if (viewMode == 2) {
            thread = replies[currentreply];
            viewMode = 1;
            load_posts();
            load_reply();
          } else if (viewMode == 3) {
            load_board();
            viewMode = 2;
            Serial.println(board);
            load_posts();
            load_reply();
          }
        }
      }
    } else {
      wifiMode = 0;
      WiFi.mode(WIFI_STA);
      if (connect_wifi()) {
        if (viewMode == 3) {
          show_boards();
        } else {
          tft.setTextColor(0x0000, bgcolor);
          tft.drawString("Connection restored!", tft.width() / 2, tft.height() / 2);
        }
        return;
      }
    }
  });
}

/**
 * Connects to 4chan's api.
 */
int connectToa4cdn() {
  // Flush everything already in
  while (client.available()) {
    // Strange hack because .flush() doesn't work as expected
    client.readStringUntil('\n');
    if(!client.available())
      delay(50);
  }
  if (!client.connected()) {
    if (!client.connect(host, httpPort)) {
      Serial.println("Connection failed");
      return 0;
    }
  }
  return 1;
}

/**
 * Enters a board.
 */
void load_board() {
  Serial.print("\r\nConnecting to ");
  Serial.println(host);

  if (!connectToa4cdn()) {
    load_board();
    return;
  }

  client.print("GET /boards.json HTTP/1.1\r\n");
  client.print("Host: " + (String)host + "\r\n");
  client.print("User-Agent: " + useragent + "\r\n");
  client.print("Connection: keep-alive\r\n");
  client.print("Keep-Alive: timeout=30, max=1000\r\n");
  client.print("Cache-Control: no-cache\r\n\r\n");

  client.readStringUntil('{');
  Serial.println("START");
  int i = 0;
  while (client.available()) {
    String line = client.readStringUntil('"');
    if (line == "ws_board") {
      if (client.readStringUntil(',') == ":1") {
        bgcolor = 0xD6DE;
      } else {
        bgcolor = 0xF71A;
      }
    } else if (line == "per_page") {
      client.readStringUntil(':');
      perPage = client.readStringUntil(',').toInt();
      Serial.print("Setting perPage to: ");
      Serial.println(perPage);
      if (i == currentreply) {
        break;
      }
      i++;
    } else if (line == "board") {
      client.readStringUntil('"');
      board = client.readStringUntil('"');
    }
  }
}

/**
 * Loads list of boards.
 */
void load_boards() {
  DynamicJsonDocument jsonDoc(1024 * 96);
  HTTPClient http;

  Serial.print("\r\njsonDoc capacity: ");
  Serial.println(jsonDoc.capacity());

  Serial.print("\r\nConnecting to ");
  Serial.println(host);

  http.useHTTP10(true);
  http.begin("https://a.4cdn.org/boards.json", root_ca);
  int httpCode = http.GET();

  // https://arduinojson.org/v6/assistant/
  StaticJsonDocument<128> filter;
  filter["boards"][0]["ws_board"] = true;
  filter["boards"][0]["meta_description"] = true;

  DeserializationError err = deserializeJson(jsonDoc, http.getStream(), DeserializationOption::Filter(filter));
  if (err) {
    Serial.print(F("deserializeJson() failed with code "));
    Serial.println(err.c_str());
  }

  int i = 0;
  String desc = "";
  for (i = 0; i < jsonDoc["boards"].size(); i++) {
    desc = jsonDoc["boards"][i]["meta_description"].as<String>();
    desc.replace("&quot;", "'");
    desc.replace("&amp;", "&");
    desc.replace("\\\/", "\/");
    desc.replace("is 4chan's board for", "-");
    desc.replace("is 4chan's imageboard dedicated to the discussion of My Little Pony: Friendship is Magic.", "Ponies!");
    desc.replace("is 4chan's imageboard for", "-");
    desc.replace("is 4chan's imageboard dedicated to the discussion of", "-");
    desc.replace("is 4chan's imageboard dedicated to", "-");
    desc.replace("is 4chan's imageboard", "-");
    boards_ds.push_back(desc);
    boards_ws.push_back(jsonDoc["boards"][i]["ws_board"].as<int>());
  }
  Serial.println("END");
}

/**
 * Shows list of boards.
 */
void show_boards() {
  tft.fillScreen(bgcolor);
  draw_reply_number();

  if (boards_ws.size() == 0)
    load_boards();

  int drawn = 0;
  int i = 0;
  String desc = "";


  for (i = 0; i < boards_ws.size(); i++) {
    if (i > currentreply - 6) {
      if (drawn < 12) { //light 0x2104 dark 0x18C3 green 0x554A red 0xDAAA
        tft.setTextDatum(TL_DATUM);
        tft.setTextColor(boards_ws[i] ? ((boards_ds[i].indexOf("Ponies") > 0) ? 0xF81F : 0x554A) : 0xDAAA, (i == currentreply) ? 0x0000 : bgcolor);
        tft.drawString(boards_ds[i], 6, 6 + drawn * 10);
        drawn++;
      } else if(maxreply != -2) {
        break;
      }
    }
  }

  //currentreply = i-2;
  if (maxreply == -2) {
    maxreply = i - 1;
  }
  draw_reply_number();
}

/**
 * Loads a thread or reply and calls draw_reply on it.
 */
void load_reply() {
  tft.fillScreen(bgcolor);
  draw_reply_number();

  Serial.print("\r\nConnecting to ");
  Serial.println(host);
  if (!connectToa4cdn()) {
    load_reply();
    return;
  }

  if (viewMode == 2)
    thread = replies[currentreply];
  client.print("GET /" + board + "/thread/" + String(thread) + ".json HTTP/1.1\r\n");
  client.print("Host: " + (String)host + "\r\n");
  client.print("User-Agent: " + useragent + "\r\n");
  client.print("Connection: keep-alive\r\n");
  client.print("Keep-Alive: timeout=30, max=1000\r\n");
  client.print("Cache-Control: no-cache\r\n\r\n");

  client.readStringUntil('{');
  Serial.println("START");
  foundPost = false;
  while (client.available()) {
    String line = client.readStringUntil('"');
    if (line == "no") {
      client.readStringUntil(':');
      String line2 = client.readStringUntil(',');
      if (String(replies[currentreply]) == line2) {
        Serial.println("Got it!");
        String jsonsnippet = String("{" + client.readStringUntil('}') + "}");
        draw_reply(jsonsnippet);
        draw_reply_number();
        foundPost = true;
        break;
      }
    }
  }
  Serial.println("END");
  // Artifact from an earlier version of the code.
  // Right now it just displays "0%" if loading a
  // post fails and retries loading the post.
  if (!foundPost) {
    tft.setTextSize(1);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(0x0000, bgcolor);
    tft.drawString("0%", tft.width() / 2, tft.height() / 2);
    load_reply();
  } else {
    draw_reply_number();
  }
}

/**
 * Draws a thread or reply.
 */
void draw_reply(String jsonsnippet) {
  DynamicJsonDocument reply(1024 * 32);  //32KB
  jsonsnippet.replace("\\u2019", "&#039;");
  jsonsnippet.replace("\\u201c", "&quot;");
  jsonsnippet.replace("\\u201d", "&quot;");
  jsonsnippet.replace("<s>", "<x>");
  jsonsnippet.replace("</s>", "</x>");
  jsonsnippet.replace("\\u", "$u");
  deserializeJson(reply, jsonsnippet);
  const char *nowb = reply["now"];
  const char *comb = reply["com"];
  const char *nameb = reply["name"];
  const char *subb = reply["sub"];
  const char *extb = reply["ext"];
  const char *fnameb = reply["filename"];
  const int imgw = reply["w"];
  const int imgh = reply["h"];
  int tnw = reply["tn_w"];
  int tnh = reply["tn_h"];
  String fulltext = "";
  if (String(subb).length() > 0) {
    fulltext.concat(String(subb) + " ");
  }
  fulltext.concat("<n>" + String(nameb) + "</n> " + String(nowb) + " No." + String(replies[currentreply]) + "<br>");
  tft.fillScreen(bgcolor); //moved this here or else pic won't be visible lol
  // Parse the tim element (timestamp) to load the image later on
  tim = "";
  if (String(fnameb).length() > 0) {
    fulltext.concat("File: <z>" + String(fnameb) + String(extb) + "</z> (" + String(imgw) + "x" + String(imgh) + ")<br>");
    bool readingtim = false;
    for (int x = 10; x < jsonsnippet.length(); x++) {
      if (readingtim) {
        if (String(jsonsnippet.charAt(x)) == String(",")) {
          break;
        }
        tim.concat(String(jsonsnippet.charAt(x)));
      }
      if (jsonsnippet.substring(x - 5, x + 1) == "\"tim\":") {
        readingtim = true;
      }
    }
    tnw /= 2;
    tnh /= 2;
  }
  fulltext.concat(comb);
  //FOR COLORS: http://www.rinkydinkelectronics.com/calc_rgb565.php (RGB565)
  //TEXT IS 6x8 CHARACTERS
  //4CHAN PREVIEW IMAGES ARE ???x125
  //SCREEN IS: 240x135
  int sx = 9;
  int sy = 6;
  int txtmode = 0;
  String entity = "";
  //4chan blue bg - 0xEF9F
  //4chan blue post bg - bgcolor
  tft.setTextColor(TFT_BLACK, bgcolor);
  tft.setTextSize(1);
  tft.setTextDatum(TL_DATUM);
  int currentMultiPage = 0;
  // This part parses and draws the text.
  // I honestly don't remember anything from the
  // night I wrote this so you are on your own.
  for (int x = 0; x < fulltext.length(); x++) {
    if (sx < tnw + 12 && sy < tnh + 6 && currentMultiPage == 0) {
      sx = tnw + 12;
    }
    if (sy > 130 && viewMode == 1) {
      if (multiPage < 1) {
        multiPage = 0;
        break;
      } else if (currentMultiPage < multiPage) {
        currentMultiPage += 1;
        sx = 9;
        sy = 6;
      } else {
        break;
      }
    }
    String cchar = String(fulltext.charAt(x));
    if (cchar == "<") {
      txtmode = 1;
      cchar = String(fulltext.charAt(x + 1));
      if (cchar == "a") {
        tft.setTextColor(0xD800, bgcolor);
      } else if (cchar == "b") {
        sx = 9;
        sy += 10;
      } else if (cchar == "n") {
        tft.setTextColor(0x13A8, bgcolor);
      } else if (cchar == "s") {
        tft.setTextColor(0x7CC4, bgcolor);
      } else if (cchar == "z") {
        tft.setTextColor(0x31AB, bgcolor);
      } else if (cchar == "x") {
        tft.setTextColor(0xFFFF, 0x0000);
      } else if (cchar == "/") {
        tft.setTextColor(TFT_BLACK, bgcolor);
      }

    }
    if (cchar == "&") {
      txtmode = 2;
      cchar = String(fulltext.charAt(x + 1));
      //should be replaced with full HTML entity parser
      if (currentMultiPage == multiPage || multiPage == -1 || viewMode != 1) {
        if (cchar == "g") {
          tft.drawString(">", sx, sy);
        } else if (cchar == "0") {
          tft.drawString("'", sx, sy);
        } else {
          tft.drawString("Õ", sx, sy);
        }
      }
      sx += 6;
      if (sx > 225) {
        sx = 9;
        sy += 10;

      }
    }
    if (txtmode == 0) {
      if (currentMultiPage == multiPage || multiPage == -1 || viewMode != 1) {
        tft.drawString(cchar, sx, sy);
      }
      sx += 6;
      if (sx > 225) {
        sx = 9;
        sy += 10;
      }
    } else if (txtmode == 1) {
      if (cchar == ">") {
        txtmode = 0;
      }
    } else if (txtmode == 2) {
      if (cchar == ";") {
        txtmode = 0;
      }
    }
  }
  if (currentMultiPage < multiPage) {
    currentreply++;
    multiPage = -1;
    if (currentreply > maxreply) {
      currentreply = 0;
    }
    load_reply();
    return;
  }
  draw_img(0);
}

/**
 * Draws current thread/reply number in the corner.
 */
void draw_reply_number() {
  tft.setTextColor(TFT_BLACK, bgcolor);
  tft.setTextDatum(BR_DATUM);
  tft.drawString(String(currentreply + 1) + "/" + String(maxreply + 1), 231, 125);
}

/**
 * Loads either all threads on a board or all replies on a thread.
 */
void load_posts() {
  tft.setTextSize(1);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(0xD800, bgcolor);
  tft.drawString("Loading...", tft.width() / 2, tft.height() / 2);

  Serial.print("\r\nConnecting to ");
  Serial.println(host);

  if (!connectToa4cdn()) {
    load_posts();
    return;
  }

  if (viewMode == 1) {
    client.print("GET /" + board + "/thread/" + String(thread) + ".json HTTP/1.1\r\n");
  } else {
    client.print("GET /" + board + "/catalog.json HTTP/1.1\r\n");
  }
  client.print("Host: " + (String)host + "\r\n");
  client.print("User-Agent: " + useragent + "\r\n");
  client.print("Connection: keep-alive\r\n");
  client.print("Keep-Alive: timeout=30, max=1000\r\n");
  client.print("Cache-Control: no-cache\r\n\r\n");

  //memset(replies, 0, sizeof(replies));

  client.readStringUntil('{');
  Serial.println("START");
  int i = 0;
  int currentpost = 0; //only used for viewMode 2
  while (client.available()) {
    while (client.available()) {
      String line = client.readStringUntil('"');
      if (line == "no") {
        break;
      } else if (line == "replies") {
        if (viewMode == 2) {
          replies[i] = currentpost;
          //Serial.println("postid=" + String(replies[i]) + " i=" + String(i));
          i++;
        }
      }
    }
    client.readStringUntil(':');
    if (viewMode == 1) {
      replies[i] = client.readStringUntil(',').toInt();
      //Serial.println("postid=" + String(replies[i]) + " i=" + String(i));
      i++;
    } else {
      currentpost = client.readStringUntil(',').toInt();
    }
  }
  currentreply = 0;
  maxreply = i - 2;
  Serial.println("END");
  if (maxreply == -2) {
    load_posts();
  }
}

/**
 * Draw the current image. If `full` is enabled
 * show in fullscreen, otherwise show thumbnail.
 */
void draw_img(bool full) {
  if (!full && multiPage > 0)
    return;
  if (tim.length() == 0)
    return;

  connectToa4cdn();

  Serial.println("GET /" + board + "/" + tim + "s.jpg HTTP/1.1\r\n");
  client.print("GET /" + board + "/" + tim + "s.jpg HTTP/1.1\r\n");
  client.print("Host: i.4cdn.org\r\n");
  client.print("User-Agent: " + useragent + "\r\n");
  client.print("Connection: keep-alive\r\n");
  client.print("Keep-Alive: timeout=30, max=1000\r\n");
  client.print("Cache-Control: no-cache\r\n\r\n");
  int totalSize = 0;

  /*
    When calling draw_img after loading a huge thread,
    the thread will still be in the client for some reason
    even after attempting to flush it in connectToa4cdn()
    so what this part here does is just reading the stream
    until we reach a JPEG header and then we stop and move
    on. Also we can't use client.available() here for some
    reason so that sucks lol.
  */
  uint8_t jpeg_header[] = {0xff, 0xd8, 0xff, 0xe0, 0x00, 0x10, 0x4a, 0x46, 0x49, 0x46};
  uint8_t jpeg_header_buff[8];
  uint8_t index;
  while (true) {
    if (client.find("JFIF"))
      break;
    if (!client.connected())
      return;
  }

  uint8_t buff[1024];

  // Start off with a JPEG header
  memcpy(PicArray + totalSize, jpeg_header, 10);
  totalSize = totalSize + 10;

  // Keep loading the image until we have loaded it
  while (client.available()) {
    size_t size = client.available();
    if(size) {

      int c = client.readBytes(buff, ((size > sizeof(buff)) ? sizeof(buff) : size));
      memcpy(PicArray + totalSize, buff, c);
      totalSize = totalSize + c;
    }
  }

  // Just logging
  Serial.println("TotalS:" + String(totalSize));

  Serial.print("First 10 Bytes: ");
  for (int ipt = 0; ipt < 11; ipt++) {
    Serial.print(PicArray[ipt], HEX);
    Serial.print(",");
  }
  Serial.print("\nLast 10 Bytes : ");
  for (int ipt = 10; ipt >= 0; ipt--) {
    Serial.print(PicArray[totalSize - ipt], HEX);
    Serial.print(",");
  }
  Serial.println("");

  // if we are displaying as a thumbnail then show it 2x smaller
  TJpgDec.setJpgScale(full ? 1 : 2); //1, 2, 4 or 8
  uint16_t w = 0, h = 0;
  TJpgDec.getJpgSize(&w, &h, PicArray, sizeof(PicArray));
  // show the pic in the middle or the edge depending on if it is a thumbnail
  TJpgDec.drawJpg(full ? 120 - (w / 2) : 6, full ? 68 - (h / 2) : 6, PicArray, sizeof(PicArray));
}

// Not implemented as of right now
void refresh_post() {
  load_posts();
  load_reply();
  currentreply = 0;
}

/* Other stuff */

void setup() {
  Serial.begin(115200);
  // This part used to have an ascii art of Cadence but I removed it
  Serial.println("HI ANON!");
  if (!EEPROM.begin(512)) {
    Serial.println("Failed to initialise EEPROM");
    Serial.println("Restarting...");
    delay(1000);
    ESP.restart();
  } else {
    Serial.println("EEPROM initialized successfully!");
  }
  TJpgDec.setSwapBytes(false);
  TJpgDec.setCallback(tft_output);
  tft.init();
  tft.setRotation(1);
  tft.setCursor(0, 0);
  tft.setTextSize(1);
  tft.fillScreen(TFT_WHITE);
  tft.setTextDatum(MC_DATUM);

  if (TFT_BL > 0) { // TFT_BL has been set in the TFT_eSPI library in the User Setup file TTGO_T_Display.h
    pinMode(TFT_BL, OUTPUT); // Set backlight pin to output mode
    digitalWrite(TFT_BL, TFT_BACKLIGHT_ON); // Turn backlight on. TFT_BACKLIGHT_ON has been set in the TFT_eSPI library in the User Setup file TTGO_T_Display.h
  }

  tft.setSwapBytes(true);
  tft.setTextColor(0x0000, 0xFFFF);
  tft.drawString("Connecting...", tft.width() / 2, 8 * 7);

  //Initialize buttons
  button_init();

  viewMode = 3;
  bgcolor = 0x2104; //light 0x2104 dark 0x18C3 green 0x554A red 0xDAAA
  currentreply = 0;
  maxreply = -2;

  client.setCACert(root_ca);

  WiFi.mode(WIFI_STA);
  if (connect_wifi()) {
    show_boards();
  }
}

void loop() {
  if (wifiMode == 1) {
    wifiLoop();
  }
  button_loop();
}

void wifi_scan() {
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(1);

  tft.drawString("Scan Network", tft.width() / 2, tft.height() / 2);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);

  int16_t n = WiFi.scanNetworks();
  tft.fillScreen(TFT_BLACK);
  if (n == 0) {
    tft.drawString("no networks found", tft.width() / 2, tft.height() / 2);
  } else {
    tft.setTextDatum(TL_DATUM);
    tft.setCursor(0, 0);
    Serial.printf("Found %d net\n", n);
    for (int i = 0; i < n; ++i) {
      sprintf(buff,
              "[%d]:%s(%d)",
              i + 1,
              WiFi.SSID(i).c_str(),
              WiFi.RSSI(i));
      tft.println(buff);
    }
  }
  WiFi.mode(WIFI_OFF);
}

/**
 * Hosts the hotspot website.
 */
void wifiLoop() {
  WiFiClient client = server.available();/* I don't remember writing this comment and I do not like it: */
  if (client) {                             // If a new client connects,
    Serial.println("New Client.");          // print a message out in the serial port
    String currentLine = "";                // make a String to hold incoming data from the client
    while (client.connected()) {            // loop while the client's connected
      if (client.available()) {             // if there's bytes to read from the client,
        char c = client.read();             // read a byte, then
        Serial.write(c);                    // print it out the serial monitor
        header += c;                        // and add it to the header.
        if (c == '\n') {                    // If the byte is a newline character
          // if the current line is blank, you got two newline characters in a row.
          // that's the end of the client HTTP request, so send a response:
          if (currentLine.length() == 0) {
            String requestBody;

            while (client.available()) {
              requestBody += (char)client.read();
            }

            if (requestBody.length()) {
              Serial.println("Got credentials!");
              DynamicJsonDocument credentials(1024 * 1); //1KB
              deserializeJson(credentials, requestBody);
              //set EEPROM creds
              const char *jsonssid = credentials["ssid"];
              const char *jsonpwd = credentials["pwd"];
              EEPROM.writeString(0, jsonssid);
              EEPROM.writeString(256, jsonpwd);
              EEPROM.commit();
              wifiMode = 0;
              WiFi.mode(WIFI_STA);
              if (connect_wifi()) {
                if (viewMode == 3) {
                  show_boards();
                } else {
                  tft.setTextColor(0x0000, bgcolor);
                  tft.drawString("Connection restored!", tft.width() / 2, tft.height() / 2);
                }
                return;
              }
            }

            client.println("HTTP/1.1 200 OK");
            client.println("Content-type:text/html");
            client.println("Connection: close");
            client.println();

            // Display the HTML web page
            client.println("<!DOCTYPE html><html>");
            client.println("<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
            client.println("<link rel=\"icon\" href=\"data:,\">");
            client.println("<title>Chanduino</title>");
            client.println("<style>html { font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: center }");
            client.println("body { background-color: #1D1F21; color: #B294BB }");
            client.println(".button { background-color: #B5BD68; border: none; color: white; padding: 16px 40px;");
            client.println("text-decoration: none; font-size: 30px; margin: 2px; cursor: pointer;}");
            client.println("</style></head>");

            // Web Page Heading
            client.println("<body><h1>Chanduino</h1>");
            if (requestBody.length()) {
              client.println("<p>Chanduino is attempting to connect to WiFi...</p>");
            } else {
              int n = WiFi.scanNetworks();
              Serial.println("scan done");
              if (n == 0) {
                client.println("<p>No networks detected, please refresh</p>");
              } else {
                client.println("<h3>Pick a network!</h3>");
                for (int i = 0; i < n; ++i) {
                  String iSSID = WiFi.SSID(i);
                  String esc_iSSID = iSSID;
                  iSSID.replace('"', '&quot;');
                  iSSID.replace("'", "\'");
                  esc_iSSID.replace('&', '&amp;');
                  esc_iSSID.replace('<', '&lt;');
                  esc_iSSID.replace('>', '&gt;');
                  esc_iSSID.replace('"', '&quot;');
                  client.print(String("<button class=\"button\" onclick=\"connectWifi(") + ((WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? "true" : "false") + ",'" + iSSID + "')\">" + esc_iSSID + ((WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? "" : "*") + "</button><br>");
                  client.print("</p>");
                }
              }
            }
            client.println("<script>\nfunction connectWifi(open,ssid){\n  let pwd = '';\n  if (!open){\n    pwd = prompt('Please enter the password for ' + ssid, '');\n    if (pwd == null) { \n      return;\n    }\n  }\n  fetch('/', { headers: { 'Accept': 'application/json', 'Content-Type': 'application/json' }, method: 'POST', body: JSON.stringify({ssid,pwd})});\n  document.body.innerHTML = '<h1>Connecting...</h1>';\n}\n</script>");
            client.println("</body></html>");

            // The HTTP response ends with another blank line
            client.println();
            // Break out of the while loop
            break;
          } else { // if you got a newline, then clear currentLine
            currentLine = "";
          }
        } else if (c != '\r') {  // if you got anything else but a carriage return character,
          currentLine += c;      // add it to the end of the currentLine
        }
      }
    }
    // Clear the header variable
    header = "";
    // Close the connection
    Serial.println("Client disconnected.");
    Serial.println("");
  }
}

void button_loop() {
  btn1.loop();
  btn2.loop();
}

/**
 * Connects to the saved WiFI.
 * If no WiFi is found, launches a hotspot (website) and asks the user to connect to a network through it.
 */
bool connect_wifi() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.print("WiFi not connected, attempting to re-connect...");
    WiFi.begin(EEPROM.readString(0).c_str(), EEPROM.readString(256).c_str());
    Serial.println(EEPROM.readString(0));
    // Prints WiFI password, commented out for obvious reasons
    //Serial.println(EEPROM.readString(256));
    // Try to connect for 5 seconds
    for (int i = 100; i > 0; i--) {
      delay(50);
      Serial.print(".");
      if (WiFi.status() == WL_CONNECTED) {
        break;
      }
    }
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("No WiFi, showing error screen");
      WiFi.mode(WIFI_OFF);
      WiFi.mode(WIFI_AP);
      // You can specify a password for the AP:
      // WiFi.softAP("Chanduino", "password");
      WiFi.softAP("Chanduino");
      IPAddress IP = WiFi.softAPIP();
      server.begin();
      Serial.print("AP IP address: ");
      Serial.println(IP);
      tft.setTextColor(0x0000, 0xFFFF);
      tft.setTextDatum(MC_DATUM);
      tft.drawString("No wifi detected!", tft.width() / 2, 8 * 2);
      tft.drawString("Press any button to retry", tft.width() / 2, 8 * 3);
      tft.drawString("or connect to the 'Chanduino'", tft.width() / 2, 8 * 4);
      tft.drawString("Wifi and visit the following", tft.width() / 2, 8 * 5);
      tft.drawString("URL on your device for setup:", tft.width() / 2, 8 * 6);
      tft.drawString("http://" + ipToString(IP), tft.width() / 2, 8 * 7);

      tft.drawString("Note: you might have to disable", tft.width() / 2, 8 * 14);
      tft.drawString("mobile data on phones", tft.width() / 2, 8 * 15);
      wifiMode = 1;
      return false;
    }
    return true;
  }
}

String ipToString(IPAddress ip) {
  String s = "";
  for (int i = 0; i < 4; i++)
    s += i  ? "." + String(ip[i]) : String(ip[i]);
  return s;
}

void disconnect_wifi() {
  WiFi.disconnect();
  WiFi.mode(WIFI_OFF);
}

bool tft_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t *bitmap) {
  if ( y >= tft.height() ) return 0;
  //w,h = 16x16
  tft.pushImage(x, y, w, h, bitmap);
  return 1;
}

void heap() {
  Serial.print("Free heap: ");
  Serial.println(ESP.getFreeHeap());
}

void cb_connection_ok(void *pvParameter) {
  ESP_LOGI(TAG, "I have a connection!");
}
