// ***** INCLUDES *****
#include <Adafruit_ILI9341.h>
#include <Adafruit_MAX31856.h>
#include "Adafruit_GFX.h"
#include <Fonts/FreeSans9pt7b.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Update.h>
#include "FS.h"
#include <SD.h>
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager (development branch) -> download ZIP file -> Arduino IDE -> Project -> Add library -> Add library from ZIP file -> select downloaded file
#include "SPI.h"
#include "config.h"
//#include "LCD.h"
#include "Button.h"
#include "reflow_logic.h"
//#include "OTA.h"
#include "webserver.h"

HTTPClient http;

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
bool buzzer = 0;
bool useOTA = 0;
bool debug = 0;
bool verboseOutput = 1;
bool disableMenu = 0;
bool profileIsOn = 0;
bool updataAvailable = 0;
bool testState = 0;

// Button variables
int buttonVal[numDigButtons] = {0};                            // value read from button
int buttonLast[numDigButtons] = {0};                           // buffered value of the button's previous state
long btnDnTime[numDigButtons];                               // time the button was pressed down
long btnUpTime[numDigButtons];                               // time the button was released
boolean ignoreUp[numDigButtons] = {false};                     // whether to ignore the button release because the click+hold was triggered
boolean menuMode[numDigButtons] = {false};                     // whether menu mode has been activated or not
int debounce = 50;
int holdTime = 1000;
int oldTemp = 0;

byte numOfPointers = 0;
byte state = 0; // 0 = boot, 1 = main menu, 2 = select profile, 3 = change profile, 4 = add profile, 5 = settings, 6 = info, 7 = start reflow, 8 = stop reflow, 9 = test outputs
byte previousState = 0;

byte settings_pointer = 0;
byte previousSettingsPointer = 0;
bool   SD_present = false;
//char* json = "";
int profileNum = 0;
#define numOfProfiles 10
String jsonName[numOfProfiles];
char json;
// Structure for paste profiles
typedef struct {
  char      title[20];         // "Lead 183"
  char      alloy[20];         // "Sn63/Pb37"
  uint16_t  melting_point;     // 183

  uint16_t  temp_range_0;      // 30
  uint16_t  temp_range_1;      // 235

  uint16_t  time_range_0;      // 0
  uint16_t  time_range_1;      // 340

  char      reference[100];    // "https://www.chipquik.com/datasheets/TS391AX50.pdf"

  uint16_t  stages_preheat_0;  // 30
  uint16_t  stages_preheat_1;  // 100

  uint16_t  stages_soak_0;     // 120
  uint16_t  stages_soak_1;     // 150

  uint16_t  stages_reflow_0;   // 150
  uint16_t  stages_reflow_1;   // 183

  uint16_t  stages_cool_0;     // 240
  uint16_t  stages_cool_1;     // 183
} profile_t;

profile_t paste_profile[numOfProfiles];

WiFiManager wm;

void setup() {
  WiFi.mode(WIFI_STA); // explicitly set mode, esp defaults to STA+AP

  Serial.begin(115200);

  Serial.println(projectName);

  Serial.println("FW version is: " + String(fwVersion) + "_&_" + String(__DATE__) + "_&_" + String(__TIME__));

  preferences.begin("store", false);
  buttons = preferences.getBool("buttons", 0);
  fan = preferences.getBool("fan", 0);
  horizontal = preferences.getBool("horizontal", 0);
  buzzer = preferences.getBool("buzzer", 0);
  useOTA = preferences.getBool("useOTA", 0);

  preferences.end();

  Serial.println();
  Serial.println("Buttons: " + String(buttons));
  Serial.println("Fan is: " + String(fan));
  Serial.println("Horizontal: " + String(horizontal));
  Serial.println("Buzzer: " + String(buzzer));
  Serial.println("OTA: " + String(useOTA));
  Serial.println();

  // Extract "profiles" from memory
  preferences.begin("profiles");
  // Extract the size of saved array
  size_t schLen = preferences.getBytes("profiles", NULL, NULL);
  // Get array to buffer and check the size
  char buffer[schLen];
  preferences.getBytes("profiles", buffer, schLen);
  // Check, that Preferences are not empty
  if (schLen > 0) {
    if (schLen % sizeof(profile_t)) {
      log_e("Data is not correct size!");
      return;
    }
    // Save the extracted data into variable
    profile_t *profiles = (profile_t *) buffer;
    profileNum = schLen / sizeof(profile_t);
    // Load profiles from memory to array for program usage
    for (int i=0; i < profileNum; i++) {
      paste_profile[i] = profiles[i];
    }
    // Print of Title names loaded from Preferences
    Serial.println("Titles from Preferences: ");
    for (int i = 0; i < profileNum; i++) {
      Serial.print((String)i + ". ");
      Serial.println(paste_profile[i].title);
    }
    Serial.println();
  }
  else {
    Serial.println("No data found in Preferences memory.");
  }
  preferences.end();

  display.begin();
  startScreen();

  //reset settings - wipe credentials for testing
  //wm.resetSettings();

  wm.setConfigPortalBlocking(false);
  if (wm.autoConnect("ReflowOvenAP")) {
    Serial.println("connected...yeey :)");
  }
  else {
    Serial.println("Configportal running");
  }

  // SSR pin initialization to ensure reflow oven is off

  pinMode(ssrPin, OUTPUT);
  digitalWrite(ssrPin, LOW);

  // Buzzer pin initialization to ensure annoying buzzer is off
  digitalWrite(buzzerPin, LOW);
  pinMode(buzzerPin, OUTPUT);

  // LED pins initialization and turn on upon start-up (active low)
  pinMode(ledPin, OUTPUT);

  // Start-up splash
  //digitalWrite(fanPin, LOW);
  pinMode(fanPin, OUTPUT);

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
  if (WiFi.status() == WL_CONNECTED) { // Wait for the Wi-Fi to connect: scan for Wi-Fi networks, and connect to the strongest of the networks above
    //delay(250); Serial.print('.');
    Serial.println("\nConnected to " + WiFi.SSID() + "; IP address: " + WiFi.localIP().toString()); // Report which SSID and IP is in use
    connected = 1;

    if (useOTA != 0) {
      OTA();
    }
  }

  // The logical name http://reflowserver.local will also access the device if you have 'Bonjour' running or your system supports multicast dns
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
  } else {
    Serial.println(F("Card initialised... file access enabled..."));
    SD_present = true;
    // Reset number of profiles for fresh load from SD card
    profileNum = 0;
    listDir(SD, "/profiles", 0);
  }
  // Load data from SD card, if available
  if (SD_present == true) {
    // Scan all profiles possible
    for (int i = 0; i < profileNum; i++) {
      parseJsonProfile(jsonName[i], i);
    }
    // Put all profiles into Preferences
    preferences.begin("profiles");
    // Get the possible length of saved array
    size_t schLen = profileNum*sizeof(profile_t);
    // Put all bytes into "profiles"
    preferences.putBytes("profiles", paste_profile, schLen);
    // Test read the data back and test their correct number
    char buffer[schLen];
    preferences.getBytes("profiles", buffer, schLen);
    if (schLen % sizeof(profile_t)) {
      log_e("Data is not correct size!");
      return;
    }
    // Check of Saved profiles
    Serial.print("Saved profile titles: ");
    for (int i = 0; i < profileNum; i++) {
      Serial.print(paste_profile[i].title);
      Serial.print(", ");
    }
    Serial.println();
    preferences.end();
  }
  Serial.println();
  Serial.print("Number of profiles: ");
  Serial.println(profileNum);

  Serial.println("Titles and alloys: ");
  for (int i = 0; i < profileNum; i++) {
    Serial.print((String)i + ". ");
    Serial.print(paste_profile[i].title);
    Serial.print(", ");
    Serial.println(paste_profile[i].alloy);
  }
  Serial.println();
  
}

void updatePreferences() {
  preferences.begin("store", false);
  preferences.putBool("buttons", buttons);
  preferences.putBool("fan", fan);
  preferences.putBool("horizontal", horizontal);
  preferences.putBool("buzzer", buzzer);
  preferences.putBool("useOTA", useOTA);
  preferences.end();

  if (verboseOutput != 0) {
    Serial.println();
    Serial.println("Buttons is: " + String(buttons));
    Serial.println("Fan is: " + String(fan));
    Serial.println("Horizontal is: " + String(horizontal));
    Serial.println("OTA is : " + String(useOTA));
    Serial.println("Buzzer is: " + String(buzzer));
    Serial.println();
  }
}

void processButtons() {
  for (int i = 0; i < numDigButtons; i++) {
    digitalButton(digitalButtonPins[i]);
  }
  readAnalogButtons();
}

void loop() {
  wm.process();
  if (state != 9) { // if we are in test menu, disable LED & SSR control in loop
    reflow_main();
  }
  processButtons();
  server.handleClient(); // Listen for client connections
}

void listDir(fs::FS & fs, const char * dirname, uint8_t levels) {
  Serial.printf("Listing directory: %s\n", dirname);

  File root = fs.open(dirname);
  if (!root) {
    Serial.println("Failed to open directory");
    return;
  }
  if (!root.isDirectory()) {
    Serial.println("Not a directory");
    return;
  }

  File file = root.openNextFile();
  String tempFileName;
  while (file) {
    if (file.isDirectory()) {
      Serial.print("  DIR : ");
      Serial.println(file.name());
      if (levels) {
        listDir(fs, file.name(), levels - 1);
      }
    } else {
      tempFileName = file.name();
      if (tempFileName.endsWith("json")) {
        Serial.println("Find this JSON file: "  + tempFileName);
        jsonName[profileNum] = tempFileName;
        profileNum++;
      }
    }
    file = root.openNextFile();
  }
}

void readFile(fs::FS & fs, String path, const char * type) {
  Serial.printf("Reading file: %s\n", path);

  File file = fs.open(path);
  if (!file) {
    Serial.println("Failed to open file for reading");
    return;
  }
  Serial.print("Read from file: ");
  while (file.available()) {
    Serial.write(file.read());
  }
  file.close();
}
