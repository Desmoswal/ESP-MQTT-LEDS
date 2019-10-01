#define FASTLED_ALLOW_INTERRUPTS 0
#include <FastLED.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>

FASTLED_USING_NAMESPACE

/*** FastLED ***/
#define DATA_PIN    6
#define NUM_LEDS    300
#define BRIGHTNESS  10
#define LED_TYPE    WS2812B
#define COLOR_ORDER GRB
CRGB leds[NUM_LEDS];

/***********************************/
CRGBPalette16 currentPalette;
CRGBPalette16 targetPalette;
TBlendType    currentBlending = LINEARBLEND;


/*** WIFI ***/
const char* ssid = "";
const char* password = "";


/*** MQTT ***/
const char* mqtt_server = "192.168.0.200";
const char* mqtt_username = "esp";
const char* mqtt_password = "esp";
const int mqtt_port = 1883;

const char* light_set_topic = "leds/set";
const char* light_state_topic = "leds/state";

/*** OTA ***/
#define DEVICENAME ""
#define OTApassword ""
int OTAport = 8266;

/*** JSON ***/
const int BUFFER_SIZE = JSON_OBJECT_SIZE(10);
#define MQTT_MAX_PACKET_SIZE 512

/*** Globals ***/
bool stateOn = true;
byte red = 255;
byte green = 255;
byte blue = 255;

byte brightness = 0;
byte next_brightness = 10;

const char* on_cmd = "ON";
const char* off_cmd = "OFF";
String effect = "solid";
String next_effect = "";
bool new_effect = false;

/*** Strobe ***/
int strobeCount = 1;
int flashDelay = 30;

/*** Blendwave ***/
CRGB clr1;
CRGB clr2;
uint8_t speed;
uint8_t loc1;
uint8_t loc2;
uint8_t ran1;
uint8_t ran2;

/*** Lightning ***/
uint8_t frequency = 50;                                       // controls the interval between strikes
uint8_t flashes = 8;                                          //the upper limit of flashes per strike
unsigned int dimmer = 1;

uint8_t ledstart;                                             // Starting location of a flash
uint8_t ledlen;


WiFiClient espClient;
PubSubClient client(espClient);


void setup() {
  delay(3000); // 3 second delay for recovery
  Serial.begin(9600);

/*** FastLED ***/
  // tell FastLED about the LED strip configuration
  FastLED.addLeds<LED_TYPE,DATA_PIN,COLOR_ORDER>(leds, NUM_LEDS);//.setCorrection(TypicalLEDStrip);
  //FastLED.addLeds<LED_TYPE,DATA_PIN,CLK_PIN,COLOR_ORDER>(leds, NUM_LEDS).setCorrection(TypicalLEDStrip);

  // set master brightness control
  FastLED.setBrightness(brightness);

/*** Wifi and MQTT ***/
  setup_wifi();
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);

/*** OTA ***/
  setup_OTA();

/*** Info ***/
  Serial.println("Ready");
  Serial.println("IP: ");
  Serial.print(WiFi.localIP());
  
}

void setup_wifi()
{
  delay(10);
  //WiFi.mode(WIFI_STA);
  //WiFi.begin(ssid, password);
  while(WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    
  }
  Serial.println("Wifi connected.");
  Serial.println();
}

void reconnect()
{
  while(!client.connected())
  {
    if(client.connect("ESP", mqtt_username, mqtt_password))
    {
      client.subscribe(light_set_topic);
      Serial.print("Subscribed");
    }
    else
    {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      /* HELP FOR ERROR MESSAGE
        -4 : MQTT_CONNECTION_TIMEOUT - the server didn't respond within the keepalive time
        -3 : MQTT_CONNECTION_LOST - the network connection was broken
        -2 : MQTT_CONNECT_FAILED - the network connection failed
        -1 : MQTT_DISCONNECTED - the client is disconnected cleanly
        0 : MQTT_CONNECTED - the client is connected
        1 : MQTT_CONNECT_BAD_PROTOCOL - the server doesn't support the requested version of MQTT
        2 : MQTT_CONNECT_BAD_CLIENT_ID - the server rejected the client identifier
        3 : MQTT_CONNECT_UNAVAILABLE - the server was unable to accept the connection
        4 : MQTT_CONNECT_BAD_CREDENTIALS - the username/password were rejected
        5 : MQTT_CONNECT_UNAUTHORIZED - the client was not authorized to connect
      */
      delay(5000);
    }
  }
}

void setup_OTA()
{
  ArduinoOTA.setPort(OTAport);
  ArduinoOTA.setHostname(DEVICENAME);
  ArduinoOTA.setPassword((const char *)OTApassword);

  ArduinoOTA.onStart([]() {
    Serial.println("Starting OTA");
  });

  ArduinoOTA.onEnd([]() {
    Serial.println("End OTA");
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("OTA Progress: %u%%\r", (progress / (total/100)));
  });

  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });

  ArduinoOTA.begin(9600);
}

void callback(char* topic, byte* payload, unsigned int length) 
{
  Serial.println("Message arrived: ");
  Serial.println("Topic: ");
  Serial.print(topic);

  char message[length +1];
  for (int i = 0; i< length; i++)
  {
    message[i] = (char)payload[i];
  }
  message[length] = '\0';

  Serial.println(message);

  if(!processJson(message))
  {
    return;
  }

  if(stateOn)
  {
    Serial.println("State is ON");
  }
  else
  {
    Serial.println("State is OFF");
  }

  Serial.println(effect);

  sendState();
}

void sendState()
{
  StaticJsonBuffer<BUFFER_SIZE> jsonBuffer;

  JsonObject& root = jsonBuffer.createObject();

  root["state"] = (stateOn)? on_cmd : off_cmd;
  JsonObject& color = root.createNestedObject("color");
  color["r"] = red;
  color["g"] = green;
  color["b"] = blue;

  root["brightness"] = brightness;
  root["effect"] = effect;

  char buffer[root.measureLength() + 1];
  root.printTo(buffer, sizeof(buffer));

  client.publish(light_state_topic, buffer, true);
}

bool processJson(char* message)
{
  StaticJsonBuffer<BUFFER_SIZE> jsonBuffer;

  JsonObject& root = jsonBuffer.parseObject(message);

  if(!root.success())
  {
    Serial.println("parseObject() failed");
    return false;
  }

  if(root.containsKey("state"))
  {
    if(strcmp(root["state"], on_cmd) == 0)
    {
      stateOn = true;
    }
    else if(strcmp(root["state"], off_cmd) ==0)
    {
      stateOn = false;
    }
  }

  if(root.containsKey("effect"))
  {
    next_effect = root["effect"].asString();
    new_effect = true;
  }

  if(root.containsKey("brightness"))
  {
    next_brightness = root["brightness"];
  }

  if(root.containsKey("color"))
  {
    red = root["color"]["r"];
    green = root["color"]["g"];
    blue = root["color"]["b"];
  }

  return true;
}

void loop()
{
  
  if(WiFi.status() != WL_CONNECTED)
  {
    delay(1);
    Serial.print("WIFI disconnected. Attempting reconnection.");
    setup_wifi();
    return;
  }

  if(!client.connected())
  {
    reconnect();
  }

  client.loop();

  
  if(!stateOn)
  {
    fade_out();
  }
  else
  {
    set_effect(next_effect);
  
    if(brightness < next_brightness)
    {
      fade_in();
    }
  }
/*********/
meteorRain( 150, 0, 58, 2, 5, true, 1);
FastLED.show();

}

void fade_out()
{
  while(brightness > 0)
  {
    brightness--;
    FastLED.setBrightness(brightness);
    FastLED.show();
    yield();
  }
}

void fade_in()
{
  if(brightness < next_brightness)
  {
    brightness++;
    FastLED.setBrightness(brightness);
    FastLED.show();
  }
}


void set_effect(String effectname)
{
  if(new_effect)
  {
    Serial.println("new effect is true");
    fade_out();
    new_effect = false;
  }
  if(effectname.equals("police"))
  {
    police();
    effect = effectname;
  }
  if(effectname.equals("solid"))
  {
    solid(red,green,blue);
    effect = effectname;
  }
  if(effectname.equals("palette"))
  {
    palette();
    effect = effectname;
  }
  if(effectname.equals("strobe"))
  {
    strobe(red,green,blue,5,300);
    effect = effectname;
  }
  if(effectname.equals("rainbow"))
  {
    rainbow();
    effect = effectname;
  }
  if(effectname.equals("blendwave"))
  {
    blendwave();
    effect = effectname;
  }
  if(effectname.equals("blur"))
  {
    blur();
    effect = effectname;
  }
  if(effectname.equals("ease"))
  {
    ease();
    effect = effectname;
  }
  if(effectname.equals("fillgrad"))
  {
    fill_grad();
    effect = effectname;
  }
}

void police()
{
  fill_solid(leds, NUM_LEDS, CRGB::Red);
  fill_solid(leds, NUM_LEDS/2, CRGB::Black);
  FastLED.show();
  delay(25);
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  FastLED.show();
  delay(25);
  fill_solid(leds, NUM_LEDS, CRGB::Red);
  fill_solid(leds, NUM_LEDS/2, CRGB::Black);
  FastLED.show();
  delay(25);
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  FastLED.show();
  delay(250);

  fill_solid(leds, NUM_LEDS, CRGB::Black);
  fill_solid(leds, NUM_LEDS/2, CRGB::Blue);
  FastLED.show();
  delay(25);
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  FastLED.show();
  delay(25);
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  fill_solid(leds, NUM_LEDS/2, CRGB::Blue);
  FastLED.show();
  delay(25);
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  FastLED.show();
  delay(250);
}

void palette()
{
  CRGBPalette16 currentPalettestriped;
  static uint8_t startIndex = 0;
    startIndex = startIndex + 1; /* higher = faster motion */
    fill_palette( leds, NUM_LEDS,
                  startIndex, 16, /* higher = narrower stripes */
                  currentPalettestriped, 255, LINEARBLEND);
  FastLED.show();
}

void solid(byte r, byte g, byte b)
{
  fill_solid(leds, NUM_LEDS, CRGB(r,g,b));
  FastLED.show();
}

void strobe(byte r, byte g, byte b, int strobeCount, int flashDelay)
{
  for(int i = 0; i < strobeCount; i++)
  {
    fill_solid(leds, NUM_LEDS, CRGB(r,g,b));
    FastLED.show();
    delay(flashDelay);
    fill_solid(leds, NUM_LEDS, CRGB::Black);
    FastLED.show();
    delay(flashDelay);
  }
  delay(flashDelay);
}

void rainbow() {
  
  uint8_t beatA = beatsin8(17, 0, 255);                        // Starting hue
  uint8_t beatB = beatsin8(13, 0, 255);
  fill_rainbow(leds, NUM_LEDS, (beatA+beatB)/2, 8);            // Use FastLED's fill_rainbow routine.
  FastLED.show();
}

void blendwave() 
{

  speed = beatsin8(6,0,255);

  clr1 = blend(CHSV(beatsin8(3,0,255),255,255), CHSV(beatsin8(4,0,255),255,255), speed);
  clr2 = blend(CHSV(beatsin8(4,0,255),255,255), CHSV(beatsin8(3,0,255),255,255), speed);

  loc1 = beatsin8(10,0,NUM_LEDS-1);
  
  fill_gradient_RGB(leds, 0, clr2, loc1, clr1);
  fill_gradient_RGB(leds, loc1, clr2, NUM_LEDS-1, clr1);

  FastLED.show();
} // blendwave()

void blur()
{
  
  uint8_t blurAmount = dim8_raw( beatsin8(3,64, 192) );       // A sinewave at 3 Hz with values ranging from 64 to 192.
  blur1d( leds, NUM_LEDS, blurAmount);                        // Apply some blurring to whatever's already on the strip, which will eventually go black.
  
  uint8_t  i = beatsin8(  9, 0, NUM_LEDS);
  uint8_t  j = beatsin8( 7, 0, NUM_LEDS);
  uint8_t  k = beatsin8(  5, 0, NUM_LEDS);
  
  // The color of each point shifts over time, each at a different speed.
  uint16_t ms = millis();  
  leds[(i+j)/2] = CHSV( ms / 29, 200, 255);
  leds[(j+k)/2] = CHSV( ms / 41, 200, 255);
  leds[(k+i)/2] = CHSV( ms / 73, 200, 255);
  leds[(k+i+j)/3] = CHSV( ms / 53, 200, 255);
  FastLED.show();
}

void ease() {

  static uint8_t easeOutVal = 0;
  static uint8_t easeInVal  = 0;
  static uint8_t lerpVal    = 0;

  easeOutVal = ease8InOutQuad(easeInVal);                     // Start with easeInVal at 0 and then go to 255 for the full easing.
  easeInVal++;

  lerpVal = lerp8by8(0, NUM_LEDS, easeOutVal);                // Map it to the number of LED's you have.

  leds[lerpVal] = CRGB::Red;
  fadeToBlackBy(leds, NUM_LEDS, 16);                          // 8 bit, 1 = slow fade, 255 = fast fade
  FastLED.show();
}

void fill_grad() {
  
  uint8_t starthue = beatsin8(5, 0, 255);
  uint8_t endhue = beatsin8(7, 0, 255);
  
  if (starthue < endhue) {
    fill_gradient(leds, NUM_LEDS, CHSV(starthue,255,255), CHSV(endhue,255,255), FORWARD_HUES);    // If we don't have this, the colour fill will flip around. 
  } else {
    fill_gradient(leds, NUM_LEDS, CHSV(starthue,255,255), CHSV(endhue,255,255), BACKWARD_HUES);
  }
  FastLED.show();
}

void lightning()
{
  ledstart = random16(NUM_LEDS);                               // Determine starting location of flash
  ledlen = random8(NUM_LEDS-ledstart);                        // Determine length of flash (not to go beyond NUM_LEDS-1)
  
  for (int flashCounter = 0; flashCounter < random8(3,flashes); flashCounter++) {
    if(flashCounter == 0) dimmer = 5;                         // the brightness of the leader is scaled down by a factor of 5
    else dimmer = random8(1,3);                               // return strokes are brighter than the leader
    
    fill_solid(leds+ledstart,ledlen,CHSV(255, 0, 255/dimmer));
    FastLED.show();                       // Show a section of LED's
    delay(random8(4,10));                                     // each flash only lasts 4-10 milliseconds
    fill_solid(leds+ledstart,ledlen,CHSV(255,0,0));           // Clear the section of LED's
    FastLED.show();
    
    if (flashCounter == 0) delay (150);                       // longer delay until next flash after the leader
    
    delay(50+random8(100));                                   // shorter delay between strokes  
  } // for()
  
  delay(random8(frequency)*100);
}

void CylonBounce(byte red, byte green, byte blue, int EyeSize, int SpeedDelay, int ReturnDelay){

  for(int i = 0; i < NUM_LEDS-EyeSize-2; i++) {
    fill_solid(leds, NUM_LEDS, CRGB(0,0,0));
    leds[i]= CRGB( red/10, green/10, blue/10);
    for(int j = 1; j <= EyeSize; j++) {
      leds[i+j] = CRGB( red, green, blue);
    }
    leds[i+EyeSize+1] = CRGB( red/10, green/10, blue/10);
    FastLED.show();
    delay(SpeedDelay);
  }

  delay(ReturnDelay);

  for(int i = NUM_LEDS-EyeSize-2; i > 0; i--) {
    fill_solid(leds, NUM_LEDS, CRGB(0,0,0));
    leds[i] =CRGB( red/10, green/10, blue/10);
    for(int j = 1; j <= EyeSize; j++) {
      leds[i+j]= CRGB( red, green, blue);
    }
    leds[i+EyeSize+1] = CRGB( red/10, green/10, blue/10);
    FastLED.show();
    delay(SpeedDelay);
  }
 
  delay(ReturnDelay);
}

void CenterToOutside(byte red, byte green, byte blue, int EyeSize, int SpeedDelay, int ReturnDelay) {
  for(int i =((NUM_LEDS-EyeSize)/2); i>=0; i--) {
    fill_solid(leds, NUM_LEDS, CRGB(0,0,0));
   
    leds[i] = CRGB( red/10, green/10, blue/10);
    for(int j = 1; j <= EyeSize; j++) {
      leds[i+j] = CRGB( red, green, blue);
    }
    leds[i+EyeSize+1] = CRGB( red/10, green/10, blue/10);
   
    leds[NUM_LEDS-i] = CRGB( red/10, green/10, blue/10);
    for(int j = 1; j <= EyeSize; j++) {
      leds[NUM_LEDS-i-j] = CRGB(red, green, blue);
    }
    leds[NUM_LEDS-i-EyeSize-1] = CRGB( red/10, green/10, blue/10);
   
    FastLED.show();
    delay(SpeedDelay);
  }
  delay(ReturnDelay);
}

void OutsideToCenter(byte red, byte green, byte blue, int EyeSize, int SpeedDelay, int ReturnDelay) {
  for(int i = 0; i<=((NUM_LEDS-EyeSize)/2); i++) {
    fill_solid(leds,NUM_LEDS, CRGB(0,0,0));
   
    leds[i] = CRGB( red/10, green/10, blue/10);
    for(int j = 1; j <= EyeSize; j++) {
      leds[i+j] = CRGB(red, green, blue);
    }
    leds[i+EyeSize+1] = CRGB(red/10, green/10, blue/10);
   
    leds[NUM_LEDS-i] = CRGB( red/10, green/10, blue/10);
    for(int j = 1; j <= EyeSize; j++) {
      leds[NUM_LEDS-i-j] = CRGB(red, green, blue);
    }
    leds[NUM_LEDS-i-EyeSize-1] = CRGB( red/10, green/10, blue/10);
   
    FastLED.show();
    delay(SpeedDelay);
  }
  delay(ReturnDelay);
}

void LeftToRight(byte red, byte green, byte blue, int EyeSize, int SpeedDelay, int ReturnDelay) {
  for(int i = 0; i < NUM_LEDS-EyeSize-2; i++) {
    fill_solid(leds, NUM_LEDS, CRGB(0,0,0));
    leds[i] = CRGB(red/10, green/10, blue/10);
    for(int j = 1; j <= EyeSize; j++) {
      leds[i+j] = CRGB(red, green, blue);
    }
    leds[i+EyeSize+1] = CRGB(red/10, green/10, blue/10);
    FastLED.show();
    delay(SpeedDelay);
  }
  delay(ReturnDelay);
}

void RightToLeft(byte red, byte green, byte blue, int EyeSize, int SpeedDelay, int ReturnDelay) {
  for(int i = NUM_LEDS-EyeSize-2; i > 0; i--) {
    fill_solid(leds, NUM_LEDS, CRGB(0,0,0));
    leds[i] = CRGB(red/10, green/10, blue/10);
    for(int j = 1; j <= EyeSize; j++) {
      leds[i+j] = CRGB(red, green, blue);
    }
    leds[i+EyeSize+1] = CRGB(red/10, green/10, blue/10);
    FastLED.show();
    delay(SpeedDelay);
  }
  delay(ReturnDelay);
}

void Twinkle(byte red, byte green, byte blue, int Count, int SpeedDelay, boolean OnlyOne) {
  fill_solid(leds, NUM_LEDS, CRGB(0,0,0));
 
  for (int i=0; i<Count; i++) {
     leds[random(NUM_LEDS)] = CRGB(red,green,blue);
     FastLED.show();
     delay(SpeedDelay);
     if(OnlyOne) {
       fill_solid(leds, NUM_LEDS, CRGB(0,0,0));
     }
   }
 
  delay(SpeedDelay);
}

void TwinkleRandom(int Count, int SpeedDelay, boolean OnlyOne) {
  fill_solid(leds, NUM_LEDS, CRGB(0,0,0));
 
  for (int i=0; i<Count; i++) {
     leds[random(NUM_LEDS)] = CRGB(random(0,255),random(0,255),random(0,255));
     FastLED.show();
     delay(SpeedDelay);
     if(OnlyOne) {
       fill_solid(leds, NUM_LEDS, CRGB(0,0,0));
     }
   }
 
  delay(SpeedDelay);
}

void meteorRain(byte red, byte green, byte blue, byte meteorSize, byte meteorTrailDecay, boolean meteorRandomDecay, int SpeedDelay) {  
  fill_solid(leds, NUM_LEDS, CRGB(0,0,0));
 
  for(int i = 0; i < NUM_LEDS+NUM_LEDS; i++) {
   
   
    // fade brightness all LEDs one step
    for(int j=0; j<NUM_LEDS; j++) {
      if( (!meteorRandomDecay) || (random(10)>5) ) {
        fadeToBlack(j, meteorTrailDecay );        
      }
    }
   
    // draw meteor
    for(int j = 0; j < meteorSize; j++) {
      if( ( i-j <NUM_LEDS) && (i-j>=0) ) {
        leds[i-j] = CRGB( red, green, blue);
      }
    }
   
    FastLED.show();
    delay(SpeedDelay);
  }
}

void fadeToBlack(int ledNo, byte fadeValue) {
 #ifdef ADAFRUIT_NEOPIXEL_H
    // NeoPixel
    uint32_t oldColor;
    uint8_t r, g, b;
    int value;
   
    oldColor = strip.getPixelColor(ledNo);
    r = (oldColor & 0x00ff0000UL) >> 16;
    g = (oldColor & 0x0000ff00UL) >> 8;
    b = (oldColor & 0x000000ffUL);

    r=(r<=10)? 0 : (int) r-(r*fadeValue/256);
    g=(g<=10)? 0 : (int) g-(g*fadeValue/256);
    b=(b<=10)? 0 : (int) b-(b*fadeValue/256);
   
    strip.setPixelColor(ledNo, r,g,b);
 #endif
 #ifndef ADAFRUIT_NEOPIXEL_H
   // FastLED
   leds[ledNo].fadeToBlackBy( fadeValue );
 #endif  
}
