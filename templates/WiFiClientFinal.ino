#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// --- Wi-Fi credentials (your home router's SSID and password) ---
const char* ssid = "pittu";
const char* password = "Johnedoms2@";

// --- MQTT Broker Details (Raspberry Pi's IP on your home network) ---
const char* mqtt_server = "192.168.0.100"; // <--- **REPLACE THIS WITH YOUR RASPBERRY PI'S IP**
const int mqtt_port = 1883;
const char* mqtt_client_id = "ESP8266_P10_Display_NEW"; // Unique ID for this MQTT client
const char* mqtt_topic_data = "p10/table_data"; // Topic to subscribe to

WiFiClient espClient;
PubSubClient client(espClient);

// Data structure to hold one set of production data
struct ProductionSet {
  int prod_id;
  int plan_day;
  int actual_day;
  int gap_day;
  int plan_month;
  int actual_month;
  int gap_month;
};

// --- GLOBAL STORAGE FOR ALL THREE PRODUCTION SETS ---
// Using an array, where index 0 is for prod_id 1, index 1 for prod_id 2, etc.
// Initialize with default values
ProductionSet productionData[3] = {
  {4, 0, 0, 0, 0, 0, 0}, // Data for prod_id 1 (stored at index 0)
  {5, 0, 0, 0, 0, 0, 0}, // Data for prod_id 2 (stored at index 1)
  {6, 0, 0, 0, 0, 0, 0}  // Data for prod_id 3 (stored at index 2)
};

// Flag to indicate if display update is needed
bool displayUpdateRequired = false;

// --- Function Prototypes ---
void setup_wifi();
void reconnect_mqtt();
void mqtt_callback(char* topic, byte* payload, unsigned int length);
void sendAllDataToATmega(); // New function to send all 3 sets
void updateP10Display(); // You'll need to implement this for your P10 display

// --- Setup ---
void setup() {
  Serial.begin(115200); // This Serial is for communicating with ATmega328P (and USB debug)

  setup_wifi();
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(mqtt_callback);

  // Initial send of all default data to ATmega (or once first data arrives)
  sendAllDataToATmega(); // Send initial state
}

// --- Loop ---
void loop() {
  if (!client.connected()) {
    reconnect_mqtt();
  }
  client.loop(); // Handles incoming MQTT messages

  // Check if display update is required and send all data to ATmega
  if (displayUpdateRequired) {
    sendAllDataToATmega();
    displayUpdateRequired = false; // Reset the flag
    // You might also call a specific P10 update function here if ATmega isn't handling the direct display refresh
    // updateP10Display();
  }
}

// --- Wi-Fi Connection Function ---
void setup_wifi() {
  delay(10);
  //Serial.println("\nConnecting to WiFi...");

  WiFi.begin(ssid, password);

  unsigned long startMillis = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    //Serial.print(".");
    if (millis() - startMillis > 30000) { // Timeout after 30 seconds
      //Serial.println("\nWiFi connection timed out. Retrying...");
      startMillis = millis(); // Reset timer for next attempt
      // You might want to reboot here or add more robust error handling
    }
  }

  //Serial.println("\nWiFi connected");
  //Serial.print("IP address: ");
  //Serial.println(WiFi.localIP());
}

// --- MQTT Reconnection Function ---
void reconnect_mqtt() {
  unsigned long startMillis = millis();
  while (!client.connected()) {
    //Serial.print("Attempting MQTT connection...");
    if (client.connect(mqtt_client_id)) {
      //Serial.println("connected");
      client.subscribe(mqtt_topic_data); // Subscribe to the data topic
      //Serial.print("Subscribed to topic: ");
      //Serial.println(mqtt_topic_data);
    } else {
      //Serial.print("failed, rc=");
      //Serial.print(client.state());
      //Serial.println(" try again in 5 seconds");
      delay(5000); // Wait before retrying
    }
    if (millis() - startMillis > 60000) { // Timeout after 60 seconds
      //Serial.println("MQTT connection timed out. Rebooting...");
      ESP.restart(); // Force a reboot if connection fails for too long
    }
  }
}

// --- MQTT Message Callback Function ---
void mqtt_callback(char* topic, byte* payload, unsigned int length) {
  //Serial.print("Message arrived [");
  //Serial.print(topic);
  //Serial.print("] ");

  // Deserialize JSON from payload
  StaticJsonDocument<200> doc; // Adjust size based on your JSON payload
  DeserializationError error = deserializeJson(doc, payload, length);

  if (error) {
    //Serial.print(F("deserializeJson() failed: "));
    //Serial.println(error.f_str());
    return;
  }

  int received_prod_id = doc["prod_id"] | 0;

  // Validate prod_id and update the correct element in the array
  if (received_prod_id >= 1 && received_prod_id <= 3) {
    int array_index = received_prod_id - 1; // prod_id 1 -> index 0, prod_id 2 -> index 1, etc.

    productionData[array_index].prod_id = received_prod_id; // Store actual prod_id
    productionData[array_index].plan_day = doc["plan_day"] | 0;
    productionData[array_index].actual_day = doc["actual_day"] | 0;
    productionData[array_index].gap_day = doc["gap_day"] | 0;
    productionData[array_index].plan_month = doc["plan_month"] | 0;
    productionData[array_index].actual_month = doc["actual_month"] | 0;
    productionData[array_index].gap_month = doc["gap_month"] | 0;

    //Serial.print("Data parsed for ProdID ");
    //Serial.print(received_prod_id);
    //Serial.println(". Marking for display update.");

    displayUpdateRequired = true; // Set flag to update display in main loop
  } else {
    //Serial.print("Received invalid prod_id: ");
    //Serial.println(received_prod_id);
  }
}

// --- NEW Function to send ALL three production sets to ATmega328P via Serial ---
void sendAllDataToATmega() {
  //Serial.println("Sending ALL production data to ATmega328P:");
  for (int i = 0; i < 3; i++) {
    const ProductionSet& data = productionData[i]; // Get reference to current data set

    // Format as a comma-separated string for ATmega
    String dataString = String(data.prod_id) + "," +
                        String(data.plan_day) + "," +
                        String(data.actual_day) + "," +
                        String(data.gap_day) + "," +
                        String(data.plan_month) + "," +
                        String(data.actual_month) + "," +
                        String(data.gap_month);

    Serial.println(dataString); // Send the string over serial to ATmega
    delay(20); // Small delay to ensure ATmega can process each line
  }
  //Serial.println("Finished sending all data sets.");
}

/*
// --- Placeholder for your P10 display update function (if ESP directly controls P10) ---
// If the ATmega is controlling the P10, this function might not be directly used here
// but rather the ATmega would read the serial data and update its display.
// However, if the ESP also has display logic, you'd put it here.
void updateP10Display() {
  // Example: You would draw the data for each productionData[i] on your P10 matrix here
  // based on its prod_id and current values.
  // This depends on your P10 library and setup.
  // For instance:
  // display.clear();
  // display.setTextSize(1);
  // display.setCursor(0,0);
  // display.print("P1: "); display.print(productionData[0].actual_day);
  // ... and so on for all three productionData elements
  // display.show(); // Update the physical display
  Serial.println("P10 Display update triggered (conceptual).");
}
*/