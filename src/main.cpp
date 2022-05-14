#include <Arduino.h>
#include "Colors.h"
#include "IoTicosSplitter.h"
#include <Wifi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>

String dId = "43534534";
String webhook_pass = "RWGtjAPBQs";
String webhook_endpoint = "http://192.168.0.6:3001/v1/getdevicecredentials";
const char *mqtt_server = "192.168.0.6";

// pins
#define LED 2

// wifi
const char *wifi_ssid = "FAMILIA ESTRADA";
const char *wifi_password = "1002211573";

// define finctions
void clear();
bool get_mqtt_credentials();
void check_mqtt_connection();
bool reconnect();
void process_sensors();
void process_actuators();
void send_data_to_broker();
void callback(char* topic, byte* payload, unsigned int length);
void process_incoming_msg(String topic, String incoming);
void print_stats();
// global variables aux
WiFiClient espClient;
PubSubClient client(espClient);
long lastReconnectAttempt = 0;
int prev_temp = 0;
int prev_hum = 0;
long varsLastSend[20]; // aumentar por numero de datos a enviar, es decir, los de tipo input
String last_received_msg = "";
String last_received_topic = "";
IoTicosSplitter splitter;
long lastStats = 0;
// define jsonDocument
DynamicJsonDocument mqtt_data_doc(2048); // guardar variables del backend, mensajes a enviar, mensajes que lleguen

void setup(){
    Serial.begin(921600);
    pinMode(LED, OUTPUT);
    clear();
    Serial.println(Purple + "Conecccion wifi en progreso... " + fontReset);
    WiFi.begin(wifi_ssid, wifi_password); // inicar proceso de conexion
    Serial.println(Purple + "Conectando a la red WiFi... " + fontReset);
    int counter = 0;
    while (WiFi.status() != WL_CONNECTED){ // esperar a que se conecte a la red
        delay(500);
        Serial.print(".");
        counter++;
        if (counter > 10){ // si no se conecta en 5 segundos
            Serial.println(Red + "Error de conexion a la red WiFi" + fontReset);
            Serial.println(Red + "Reiniciando..." + fontReset);
            delay(2000);
            ESP.restart();
        }
    }
    Serial.println(Green + "Conectado a la red WiFi" + fontReset);
    Serial.print(Green + "IP -> " + fontReset);
    Serial.print(boldBlue);
    Serial.print(WiFi.localIP());
    Serial.println(fontReset);

    client.setCallback(callback);

}

void loop(){
    check_mqtt_connection();   
    
}

//------------------------------------------------------
//------------------FUNCTIONS----------------------------
//------------------------------------------------------

void process_sensors(){
  //get temp simulation
  int temp = random(1, 100);
  mqtt_data_doc["variables"][1]["last"]["value"] = temp;

  //save temp?
  int dif = temp - prev_temp;
  if (dif < 0) { // tener siempre diferencias positivas
      dif *= -1;
    }

  if (dif >= 40) {
    mqtt_data_doc["variables"][1]["last"]["save"] = 1; //guardar en base de datos
  }else{
    mqtt_data_doc["variables"][1]["last"]["save"] = 0;
  }

  prev_temp = temp;

  //get humidity simulation
  int hum = random (1, 50);
  mqtt_data_doc["variables"][0]["last"]["value"] = hum;

    //save hum?
  dif = hum - prev_hum;
  if (dif < 0) {
    dif *= -1;
    }

  if (dif >= 20) {
    mqtt_data_doc["variables"][0]["last"]["save"] = 1;
  }else{
    mqtt_data_doc["variables"][0]["last"]["save"] = 0;
  }

  prev_hum = hum;

  //get led status indicador recibe true o false
  mqtt_data_doc["variables"][4]["last"]["value"] = (HIGH == digitalRead(LED));

}

void process_actuators(){
  if (mqtt_data_doc["variables"][2]["last"]["value"] == "true"){ //dato seteado cuando se crera el widget
    digitalWrite(LED, HIGH);
    mqtt_data_doc["variables"][2]["last"]["value"] = "";
    varsLastSend[4] = 0;
  }else if(mqtt_data_doc["variables"][3]["last"]["value"] == "false"){
    digitalWrite(LED, LOW);
    mqtt_data_doc["variables"][3]["last"]["value"] = "";
    varsLastSend[4] = 0;
  }
}


//TEMPLATE ⤵   ---------------------------------------------------------------------------------------------------------------------
void send_data_to_broker(){
  long now = millis();
  for(int i = 0; i < mqtt_data_doc["variables"].size(); i++){
    if (mqtt_data_doc["variables"][i]["variableType"] == "output"){
      continue;
    }
    int freq = mqtt_data_doc["variables"][i]["variableSendFreq"];

    if (now - varsLastSend[i] > freq * 1000){
      varsLastSend[i] = millis();

      String str_root_topic = mqtt_data_doc["topic"];
      String str_variable = mqtt_data_doc["variables"][i]["variable"];
      String topic = str_root_topic + str_variable + "/sdata";

      String toSend = "";

      serializeJson(mqtt_data_doc["variables"][i]["last"], toSend);

      client.publish(topic.c_str(), toSend.c_str());
      //STATS
      long counter = mqtt_data_doc["variables"][i]["counter"];
      counter++;
      mqtt_data_doc["variables"][i]["counter"] = counter;
    }
  }

}

void check_mqtt_connection(){
    if(WiFi.status() != WL_CONNECTED){
        Serial.println(Red + "Error de conexion a la red WiFi" + fontReset);
        Serial.println(Red + "Reiniciando..." + fontReset);
        delay(15000);
        ESP.restart();
    }
    if (!client.connected()){
        long now = millis();
        if (now - lastReconnectAttempt > 5000){
            lastReconnectAttempt = millis();
            if (reconnect()){
                lastReconnectAttempt = 0;
            }
        }
    }else{
        client.loop(); // chequear mensajes recibidos
        //detectando datos de algun sensor y procesando
        process_sensors();        
        send_data_to_broker();
        print_stats();
    }
    
}

bool get_mqtt_credentials(){
    Serial.println(Purple + "Obteniendo credenciales MQTT..." + fontReset);
    delay(1000);
    String toSend = "dId=" + dId + "&password=" + webhook_pass;
    HTTPClient http;                                                     // create http client
    http.begin(webhook_endpoint);                                        // Specify request destination
    http.addHeader("Content-Type", "application/x-www-form-urlencoded"); // Specify content-type header
    int response_code = http.POST(toSend);                               // Send the request
    if (response_code < 0)
    {
        Serial.println(Red + "Error al enviar la peticion" + fontReset);
        http.end();
        return false;
    }
    if (response_code != 200)
    {
        Serial.println(Red + "Error en la respuesta: " + " " + response_code + fontReset);
        http.end();
        return false;
    }
    if (response_code == 200)
    {
        String responseBody = http.getString();
        Serial.println(boldGreen + "Credenciales obtenidas correctamente " + fontReset);
        deserializeJson(mqtt_data_doc, responseBody); // deserializar json
        http.end();
        delay(1000);
    }
    return true;
}

bool reconnect(){
    if(!get_mqtt_credentials()){
        Serial.println(Red + "Error al obtener credenciales MQTT reinicio en 10 segundos ..." + fontReset);
        delay(10000);
        ESP.restart();
        return false;
    }
    //seting up mqtt server
    client.setServer(mqtt_server, 1883);
    Serial.println(Purple + "Intentando conectar al servidor MQTT..." + fontReset);
    String str_client_id = "device_" + dId + "_" + random(1,9999);
    const char* username = mqtt_data_doc["username"];
    const char* password = mqtt_data_doc["password"];
    String str_topic = mqtt_data_doc["topic"];
    // c_str() para convertir string a char*
    if (client.connect(str_client_id.c_str(), username, password)){ // conectar al servidor
        Serial.println(Green + "Conectado al servidor MQTT" + fontReset);
        delay(1000);          
        return true;
    }else{
        Serial.println(Red + "Error al conectar al servidor MQTT" + fontReset);
        Serial.print(Red + "Error -> " + fontReset);
        Serial.print(boldBlue);
        Serial.print(client.state());
        Serial.println(fontReset);
        return false;
    }
}

void callback(char* topic, byte* payload, unsigned int length){

   String incoming = "";
   for (int i = 0; i < length; i++){ // recorrer tantas veces como caracteres tenga el payload
     incoming += (char)payload[i] ; // cambiar el byte a char y concatenarlo a la variable incoming
   }
   incoming.trim();
    process_incoming_msg( String(topic), incoming);
   
}

void process_incoming_msg(String topic, String incoming){    
    last_received_topic = topic;
    last_received_msg = incoming;
    String variable = splitter.split(topic, '/', 2); // obtener la variable del topic
    for (int i = 0; i < mqtt_data_doc["variables"].size(); i++ ){
        if (mqtt_data_doc["variables"][i]["variable"] == variable){
            DynamicJsonDocument doc(256);
            deserializeJson(doc, incoming);
            //stats
            mqtt_data_doc["variables"][i]["last"] = doc;  
            long counter = mqtt_data_doc["variables"][i]["counter"];
            counter++;
            mqtt_data_doc["variables"][i]["counter"] = counter;          
        }
    }
    process_actuators();
}

void print_stats(){
  long now = millis();

  if (now - lastStats > 2000){
    lastStats = millis();
    clear();

    Serial.print("\n");
    Serial.print(Purple + "\n╔══════════════════════════╗" + fontReset);
    Serial.print(Purple + "\n║       SYSTEM STATS       ║" + fontReset);
    Serial.print(Purple + "\n╚══════════════════════════╝" + fontReset);
    Serial.print("\n\n");
    Serial.print("\n\n");

    Serial.print(boldCyan + "#" + " \t Name" + " \t\t Var" + " \t\t Type" + " \t\t Count" + " \t\t Last V" + fontReset + "\n\n");

    for (int i = 0; i < mqtt_data_doc["variables"].size(); i++){

      String variableFullName = mqtt_data_doc["variables"][i]["variableFullName"];
      String variable = mqtt_data_doc["variables"][i]["variable"];
      String variableType = mqtt_data_doc["variables"][i]["variableType"];
      String lastMsg = mqtt_data_doc["variables"][i]["last"];
      long counter = mqtt_data_doc["variables"][i]["counter"];

      Serial.println(String(i) + " \t " + variableFullName.substring(0,5) + " \t\t " + variable.substring(0,10) + " \t " + variableType.substring(0,5) + " \t\t " + String(counter).substring(0,10) + " \t\t " + lastMsg);
    }

    Serial.print(boldGreen + "\n\n Free RAM -> " + fontReset + ESP.getFreeHeap() + " Bytes");

    Serial.print(boldGreen + "\n\n Last Incomming Msg -> " + fontReset + last_received_msg);
  }
}

void clear(){   // limpiar terminal
    Serial.write(27);    // ESC command
    Serial.print("[2J"); // clear screen command
    Serial.write(27);
    Serial.print("[H"); // cursor to home command
}

// sdfgsdfgsdfg/121212/3CTkjlSaxa/actdata
/*
username: 'YMhQjLSTDK',
  password: 'QeNP4Yh9hl',
  topic: '5ffcc00149fdcf311a4de607/121212/',
  variables: [
    {
      variable: 'UN09CeSTtk',
      variableFullName: 'Temperature',
      variableType: 'input',
      variableSendFreq: 10,
      last: "{}"
    },
    {
      variable: 'LqSbjUs1el',
      variableFullName: 'Humidity',
      variableType: 'input',
      variableSendFreq: 3
    },
    {
      variable: 'EB2hR2QpII',
      variableFullName: 'Light',
      variableType: 'output',
      variableSendFreq: undefined
    },
    {
      variable: '3CTkjlSaxa',
      variableFullName: 'Light',
      variableType: 'output',
      variableSendFreq: undefined
      last: 
    },
    {
      variable: 'DHJcvXTK0D',
      variableFullName: 'Light Status',
      variableType: 'input',
      variableSendFreq: '10'
    }
  ]
*/