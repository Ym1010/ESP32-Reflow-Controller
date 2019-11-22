// ***** INCLUDES *****
#include <Adafruit_ILI9341.h>
#include <Adafruit_MAX31856.h>
#include "Adafruit_GFX.h"
#include <Fonts/FreeSans9pt7b.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include "FS.h"
#include <SD.h>
#include "SPI.h"
#include "config.h"
//#include "LCD.h"
#include "Button.h"
#include "reflow_logic.h"
#include "webserver.h"

WiFiMulti wifiMulti;

// Use software SPI: CS, DI, DO, CLK
//Adafruit_MAX31856 max = Adafruit_MAX31856(max_cs, max_di, max_do, max_clk);
// use hardware SPI, just pass in the CS pin
Adafruit_MAX31856 max31856 = Adafruit_MAX31856(max_cs);

// Use hardware SPI
Adafruit_ILI9341 display = Adafruit_ILI9341(display_cs, display_dc, display_rst);
//Adafruit_ILI9341 display = Adafruit_ILI9341(display_cs, display_dc, display_mosi, display_sclk, display_rst);

Preferences preferences;
WebServer server(80);

#define DEBOUNCE_MS 100
Button AXIS_Y = Button(BUTTON_AXIS_Y, true, DEBOUNCE_MS);
Button AXIS_X = Button(BUTTON_AXIS_X, true, DEBOUNCE_MS);

int digitalButtonPins[] = {BUTTON_SELECT, BUTTON_MENU, BUTTON_BACK};

#define numDigButtons sizeof(digitalButtonPins)

int buttonState;             // the current reading from the input pin
int lastButtonState = LOW;
unsigned long lastDebounceTime_ = 0;  // the last time the output pin was toggled
unsigned long debounceDelay = 200;    // the debounce time; increase if the output flicker

String activeStatus = "";
bool menu = 0;
bool isFault = 0;
bool connected = 0;
bool horizontal = 0;
bool fan = 0;
bool buttons = 0;
bool debug = 0;
bool verboseOutput = 1;

// Button variables
int buttonVal[numDigButtons] = {0};                            // value read from button
int buttonLast[numDigButtons] = {0};                           // buffered value of the button's previous state
long btnDnTime[numDigButtons];                               // time the button was pressed down
long btnUpTime[numDigButtons];                               // time the button was released
boolean ignoreUp[numDigButtons] = {false};                     // whether to ignore the button release because the click+hold was triggered
boolean menuMode[numDigButtons] = {false};                     // whether menu mode has been activated or not
int debounce = 50;
int holdTime = 1000;

byte numOfPointers = 0;
byte state = 0; // 0 = boot, 1 = main menu, 2 = select profile, 3 = change profile, 4 = add profile, 5 = settings, 6 = info
byte previousState = 0;
//byte menuPrintLine = 0;
//byte menuSelectLine = 0;
//byte rememberHomeMenuSelectLine = 0;
byte settings_pointer = 0;
byte previousSettingsPointer = 0;
bool   SD_present = false;

//// Types for Menu
//typedef enum MENU_STATE {
//  MENU_STATE_HOME,
//  MENU_STATE_MAIN_MENU,
//  MENU_STATE_REFLOWPROFILE,
//  MENU_STATE_EDIT_REFLOW,
//  MENU_STATE_SETTINGS,
//  MENU_STATE_INFO,
//  MENU_STATE_EDIT_NUMBER,
//  MENU_STATE_EDIT_NUMBER_DONE,
//  MENU_SETTING_LIST,
//}
//menuState_t;
//
//menuState_t menuState;

void setup() {

  Serial.begin(115200);

  Serial.println(projectName);

  Serial.println("FW version is: " + String(fwVersion) + "_&_" + String(__DATE__) + "_&_" + String(__TIME__));

  preferences.begin("store", false);
  buttons = preferences.getBool("buttons", 0);
  fan = preferences.getBool("fan", 0);
  horizontal = preferences.getBool("horizontal", 0);
  //  savedData = preferences.getString("data_bck", "");
  //  savedDataFlag = preferences.getBool("data_bck_flag", 0);
  //  prevSessId = preferences.getULong("prevSessId", 0);
  //
  //  machineName = preferences.getString("machineName", "");
  //  SSIDString = preferences.getString("SSIDString", "");
  //  passwordString = preferences.getString("passwordString", "");
  //  sendName = preferences.getBool("sendName", 0);
  //  sendFilament = preferences.getBool("sendFilament", 0);
  //  sendTime = preferences.getBool("sendTime", 0);
  preferences.end();

  Serial.println("Buttons: " + String(buttons));
  Serial.println("Fan is: " + String(fan));
  Serial.println("Horizontal: " + String(horizontal));
  //  Serial.println("API key is : " + (API_key));
  //  Serial.println("Saved data are: " + (savedData));
  //  Serial.println("Saved data flag is: " + String(savedDataFlag));
  //  Serial.println("Previous session ID was: " + String(prevSessId));
  //  Serial.println("Send name: " + String(sendName) + ", Send filament: " + String(sendFilament) + ", Send time: " + String(sendTime));
  //  Serial.println("Reset settings: " + String(resetSettings));
  //  Serial.println("SSIDs are: " + SSIDString);
  //  Serial.println("passwords are: " + passwordString);


  display.begin();
  startScreen();

  wifiMulti.addAP("SSID", "PASSWORD");
  wifiMulti.addAP("SSID", "PASSWORD");
  wifiMulti.addAP("SSID", "PASSWORD");

  // SSR pin initialization to ensure reflow oven is off

  pinMode(ssrPin, OUTPUT);
  digitalWrite(ssrPin, LOW);

  // Buzzer pin initialization to ensure annoying buzzer is off
  digitalWrite(buzzerPin, LOW);
  pinMode(buzzerPin, OUTPUT);

  // LED pins initialization and turn on upon start-up (active low)
  pinMode(ledPin, OUTPUT);

  // Start-up splash
  digitalWrite(buzzerPin, LOW);

  delay(100);

  // Turn off LED (active low)
  digitalWrite(ledPin, ledState);

  // Button initialization
  pinMode(BUTTON_AXIS_Y, INPUT_PULLDOWN);
  pinMode(BUTTON_AXIS_X, INPUT_PULLDOWN);

  for (byte i = 0; i < numDigButtons - 1 ; i++) {
    // Set button input pin
    if (digitalButtonPins[i] > 20  && digitalButtonPins[i] < 40) {
      pinMode(digitalButtonPins[i], INPUT_PULLUP);
      digitalWrite(digitalButtonPins[i], LOW  );
      Serial.println(digitalButtonPins[i]);
    }
  }

  Serial.println("Connecting ...");
  while (wifiMulti.run() != WL_CONNECTED) { // Wait for the Wi-Fi to connect: scan for Wi-Fi networks, and connect to the strongest of the networks above
    delay(250); Serial.print('.');
  }
  Serial.println("\nConnected to " + WiFi.SSID() + " Use IP address: " + WiFi.localIP().toString()); // Report which SSID and IP is in use

  // The logical name http://fileserver.local will also access the device if you have 'Bonjour' running or your system supports multicast dns
  if (!MDNS.begin("reflowserver")) {          // Set your preferred server name, if you use "myserver" the address would be http://myserver.local/
    Serial.println(F("Error setting up MDNS responder!"));
    ESP.restart();
  }

  ///////////////////////////// Server Commands
  server.on("/",         HomePage);
  server.on("/download", File_Download);
  server.on("/upload",   File_Upload);
  server.on("/fupload",  HTTP_POST, []() {
    server.send(200);
  }, handleFileUpload);
  ///////////////////////////// End of Request commands
  server.begin();
  Serial.println("HTTP server started");

  max31856.begin();
  max31856.setThermocoupleType(MAX31856_TCTYPE_K);

  // Set window size
  windowSize = 2000;
  // Initialize time keeping variable
  nextCheck = millis();
  // Initialize thermocouple reading variable
  nextRead = millis();

  Serial.print(F("Initializing SD card..."));
  if (!SD.begin(SD_CS_pin)) { // see if the card is present and can be initialised. Wemos SD-Card CS uses D8
    Serial.println(F("Card failed or not present, no SD Card data logging possible..."));
    SD_present = false;
  }
  else
  {
    Serial.println(F("Card initialised... file access enabled..."));
    SD_present = true;
  }

}

void updatePreferences() {
  preferences.begin("store", false);
  preferences.putBool("buttons", buttons);
  preferences.putBool("fan", fan);
  preferences.putBool("horizontal", horizontal);
  //  savedData = preferences.getString("data_bck", "");
  //  savedDataFlag = preferences.getBool("data_bck_flag", 0);
  //  prevSessId = preferences.getULong("prevSessId", 0);
  //
  //  machineName = preferences.getString("machineName", "");
  //  SSIDString = preferences.getString("SSIDString", "");
  //  passwordString = preferences.getString("passwordString", "");
  //  sendName = preferences.getBool("sendName", 0);
  //  sendFilament = preferences.getBool("sendFilament", 0);
  //  sendTime = preferences.getBool("sendTime", 0);
  preferences.end();
  if (verboseOutput != 0) {
    Serial.println("Buttons: " + String(buttons));
    Serial.println("Fan is: " + String(fan));
    Serial.println("Horizontal: " + String(horizontal));
  }
}

void processButtons() {
  for (int i = 0; i < numDigButtons; i++) {
    digitalButton(digitalButtonPins[i]);
  }
  readAnalogButtons();
}

void loop() {
  reflow_main();
  processButtons();
  server.handleClient(); // Listen for client connections
}
