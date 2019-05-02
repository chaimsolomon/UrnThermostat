#include <Ticker.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#define MQTT_KEEPALIVE 5

#include <OneWire.h>
#include <DallasTemperature.h>

#define ONE_WIRE_BUS 0
#define TEMPERATURE_PRECISION 12

// Setup a oneWire instance to communicate with any OneWire devices (not just Maxim/Dallas temperature ICs)
OneWire oneWire(ONE_WIRE_BUS);

// Pass our oneWire reference to Dallas Temperature.
DallasTemperature sensors(&oneWire);

// arrays to hold device addresses
DeviceAddress insideThermometer, outsideThermometer;


#define SSR_PIN 13

const char* ssid = "";
const char* password = "";
const char* mqtt_server = "";

WiFiClient espClient;
PubSubClient client(espClient);
long lastMsg = 0;
char msg[50];
int value = 0;


Ticker PwnTicker;
Ticker SensorTicker;

volatile int sensor_state = 0;

void onConnectionEstablished();

volatile int pwm_value = 50;
volatile int pwm_counter = 0;

volatile float temp_urn = 50.0;
volatile float temp_ssr = 30.0;
volatile int set_temp = 85;

void setup_wifi() {

  delay(10);
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  randomSeed(micros());

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  char* ptopic;
  ptopic = (char*) malloc(length + 1);
  strncpy(ptopic, (char*)payload, length);
  ptopic[length] = '\0';
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();

  if (strcmp(topic, "UrnTimer/SetTemp") == 0) {
    int val;
    val = atoi(ptopic);
    Serial.println(val);
    if (val > 0) set_temp = val;
  }
  free(ptopic);
}


void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Create a random client ID
    String clientId = "UrnThermostat";
    clientId += String(random(0xffff), HEX);
    // Attempt to connect
    if (client.connect(clientId.c_str())) {
      Serial.println("connected");
      // ... and resubscribe
      client.subscribe("UrnTimer/SetTemp");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(1000);
    }
  }
}


void sensor_isr() {
  float temp_diff;
  char out_str[50];
  switch (sensor_state) {
    case 0:
      if (temp_urn > 99) {
        pwm_value = 0;
        Serial.println("Urn too hot!");
        break;
      }
      temp_diff = set_temp - temp_urn;
      if (temp_diff > 10) {
        pwm_value = 100;
      } else if (temp_diff > 0) {
        pwm_value = 10*temp_diff;
      } else {
        pwm_value = 0;
      }
      sprintf(out_str, "%d", set_temp);
      client.publish("UrnTimer/set_temp", out_str);
      Serial.print("Set: ");
      Serial.println(pwm_value);
      sprintf(out_str, "%d", pwm_value);
      client.publish("UrnTimer/percent", out_str);

      break;
    case 1:
      Serial.println("Trigger Get Temp 0");
      sensors.setWaitForConversion(false);  // makes it async
      sensors.requestTemperatures();
      sensors.setWaitForConversion(true);
      break;      
    case 2:
      Serial.println("Get Temp 0");
      temp_urn = sensors.getTempC(outsideThermometer);

      sprintf(out_str, "%.0f", temp_urn);
      Serial.println(out_str);
      client.publish("UrnTimer/temp_urn", out_str);

      break;      
    case 3:
      Serial.println("Trigger Get Temp 1");
      break;      
    case 4:
      Serial.println("Get Temp 1");
      temp_ssr = sensors.getTempC(insideThermometer);

      sprintf(out_str, "%.0f", temp_ssr);
      Serial.println(out_str);
      client.publish("UrnTimer/temp_ssr", out_str);
      break;      
    default:
      sensor_state = -1;
  }
  sensor_state ++;
}

void pwm_isr() {
  if (temp_ssr < 60) {
    if (pwm_counter <= 105) {
      pwm_counter++;
      if (pwm_counter > pwm_value) {
        digitalWrite(LED_BUILTIN, HIGH); // Off
        digitalWrite(SSR_PIN, LOW); // Off
      } else
      {
        digitalWrite(LED_BUILTIN, LOW); // On
        digitalWrite(SSR_PIN, HIGH); // On
      }
    } else {
      pwm_counter = 0;
      digitalWrite(LED_BUILTIN, LOW); // On
      digitalWrite(SSR_PIN, HIGH); // On
  
    }  
  } else {
    digitalWrite(LED_BUILTIN, HIGH); // Off
    digitalWrite(SSR_PIN, LOW); // Off    
  }
}


// function to print a device address
void printAddress(DeviceAddress deviceAddress)
{
  for (uint8_t i = 0; i < 8; i++)
  {
    // zero pad the address if necessary
    if (deviceAddress[i] < 16) Serial.print("0");
    Serial.print(deviceAddress[i], HEX);
  }
}

// function to print the temperature for a device
void printTemperature(DeviceAddress deviceAddress)
{
  float tempC = sensors.getTempC(deviceAddress);
  Serial.print("Temp C: ");
  Serial.print(tempC);
  Serial.print(" Temp F: ");
  Serial.print(DallasTemperature::toFahrenheit(tempC));
}

// function to print a device's resolution
void printResolution(DeviceAddress deviceAddress)
{
  Serial.print("Resolution: ");
  Serial.print(sensors.getResolution(deviceAddress));
  Serial.println();
}

// main function to print information about a device
void printData(DeviceAddress deviceAddress)
{
  Serial.print("Device Address: ");
  printAddress(deviceAddress);
  Serial.print(" ");
  printTemperature(deviceAddress);
  Serial.println();
}

void setup()
{
  pinMode(LED_BUILTIN, OUTPUT);     // Initialize the LED_BUILTIN pin as an output
  pinMode(SSR_PIN, OUTPUT);
  Serial.begin(115200);
  PwnTicker.attach_ms(50, pwm_isr);
  SensorTicker.attach_ms(500, sensor_isr);
  setup_wifi();
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
  sensors.begin();
  // locate devices on the bus
  Serial.print("Locating devices...");
  Serial.print("Found ");
  Serial.print(sensors.getDeviceCount(), DEC);
  Serial.println(" devices.");
  // report parasite power requirements
  Serial.print("Parasite power is: ");
  if (sensors.isParasitePowerMode()) Serial.println("ON");
  else Serial.println("OFF");
  if (!sensors.getAddress(insideThermometer, 0)) Serial.println("Unable to find address for Device 0");
  if (!sensors.getAddress(outsideThermometer, 1)) Serial.println("Unable to find address for Device 1");
  // show the addresses we found on the bus
  Serial.print("Device 0 Address: ");
  printAddress(insideThermometer);
  Serial.println();

  Serial.print("Device 1 Address: ");
  printAddress(outsideThermometer);
  Serial.println();

  // set the resolution to 9 bit per device
  sensors.setResolution(insideThermometer, TEMPERATURE_PRECISION);
  sensors.setResolution(outsideThermometer, TEMPERATURE_PRECISION);

  Serial.print("Device 0 Resolution: ");
  Serial.print(sensors.getResolution(insideThermometer), DEC);
  Serial.println();

  Serial.print("Device 1 Resolution: ");
  Serial.print(sensors.getResolution(outsideThermometer), DEC);
  Serial.println();
  sensors.setWaitForConversion(false);
}

void loop()
{
  if (!client.connected()) {
    reconnect();
  }
  client.loop();
}
