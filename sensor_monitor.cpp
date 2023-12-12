#include <iostream>
#include <sstream>
#include <cstdlib>
#include <chrono>
#include <ctime>
#include <thread>
#include <unistd.h>
#include <mutex>
#include <condition_variable>
#include "json.hpp"  // json handling
#include "mqtt/client.h"  // paho mqtt
#include <iomanip>
#include <fstream>

#define QOS 1
#define BROKER_ADDRESS "tcp://localhost:1883"

bool exitRequested = 0;

int cpuFrequency = 1;   // Frequência padrão para o sensor CPU (em segundos)
int memFrequency = 1;   // Frequência padrão para o sensor de memória (em segundos)

std::string machineId;
std::mutex publishMutex;


void publishInitialMessage(mqtt::client &client) {
    nlohmann::json initialMessage;
    initialMessage["machine_id"] = machineId;

    nlohmann::json sensorsArray;

    //Adiciona a informação da cpu
    nlohmann::json cpuSensor;
    cpuSensor["sensor_id"] = "cpu";
    cpuSensor["data_type"] = "float";
    cpuSensor["data_interval"] = cpuFrequency * 1000;  // Convert to milliseconds
    sensorsArray.push_back(cpuSensor);

    // Adiciona a informação da memoria
    nlohmann::json memSensor;
    memSensor["sensor_id"] = "mem";
    memSensor["data_type"] = "float";
    memSensor["data_interval"] = memFrequency * 1000;  // Convert to milliseconds
    sensorsArray.push_back(memSensor);

    initialMessage["sensors"] = sensorsArray;

    //Publicação da mensagem inicial
    std::string topic = "/sensor_monitors";
    mqtt::message msg(topic, initialMessage.dump(), QOS, false);
    client.publish(msg);
    std::clog << "Initial message published - topic: " << topic << " - message: " << initialMessage.dump() << std::endl;
}

float getMemoryUsage() {
    std::ifstream meminfo("/proc/meminfo");
    std::string line;
    float total_mem, free_mem;

    //Leitura da memoria total
    std::getline(meminfo, line);
    std::istringstream total_mem_stream(line.substr(line.find(":") + 1));
    total_mem_stream >> total_mem;

    //Leitura da memoria livre
    std::getline(meminfo, line);
    std::istringstream free_mem_stream(line.substr(line.find(":") + 1));
    free_mem_stream >> free_mem;

    //Calculo da porcentagem de memoria utilizada
    float used_mem = total_mem - free_mem;
    float mem_usage_percentage = (used_mem / total_mem) * 100.0f;

    return mem_usage_percentage;
}

float getCPUUsage() {
    std::ifstream stat("/proc/stat");
    std::string line;
    long user, nice, system, idle, iowait, irq, softirq;

   //Leitura do uso da cpu
    std::getline(stat, line);
    std::istringstream cpu_stream(line.substr(5)); // Skip "cpu"
    cpu_stream >> user >> nice >> system >> idle >> iowait >> irq >> softirq;

    // Calculo do uso total da cpu
    long total_cpu_time = user + nice + system + idle + iowait + irq + softirq;

    //calculo da porcentagem do uso da cpu
    float cpu_usage_percentage = (1.0f - static_cast<float>(idle) / total_cpu_time) * 100.0f;

    return cpu_usage_percentage;
}

float getSensorValue(const std::string &sensorID) {
    if (sensorID == "cpu") {
        return getCPUUsage();
    } else {
        return getMemoryUsage();
    }
}

void publishSensorReading(mqtt::client &client, const std::string &sensorID) {
    // Timestamp no formato ISO 8601 .
    auto now = std::chrono::system_clock::now();
    std::time_t now_c = std::chrono::system_clock::to_time_t(now);
    std::tm *now_tm = std::localtime(&now_c);
    std::stringstream ss;
    ss << std::put_time(now_tm, "%FT%TZ");
    std::string timestamp = ss.str();

    // Construct the JSON message.
    nlohmann::json j;
    j["timestamp"] = timestamp;
    j["value"] = getSensorValue(sensorID);

    std::unique_lock<std::mutex> lock(publishMutex);

    // Publish the JSON message to the appropriate topic.
    std::string topic = "/sensors/" + machineId + "/" + sensorID;
    mqtt::message msg(topic, j.dump(), QOS, false);
    std::clog << "message published - topic: " << topic << " - message: " << j.dump() << std::endl;
    client.publish(msg);
}

void sensorTask(mqtt::client &client, const std::string &sensorID, int frequency) {
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(frequency));
        publishSensorReading(client, sensorID);
    }
}

int main(int argc, char *argv[]) {
    std::string clientId = "sensor-monitor";
    mqtt::client client(BROKER_ADDRESS, clientId);

    // Conectar ao broker MQTT.
    mqtt::connect_options connOpts;
    connOpts.set_keep_alive_interval(20);
    connOpts.set_clean_session(true);

    try {
        client.connect(connOpts);
    } catch (mqtt::exception &e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    std::clog << "connected to the broker" << std::endl;

    // Obter o identificador único da máquina (neste caso, o nome do host).
    char hostname[1024];
    gethostname(hostname, 1024);
    machineId = hostname;

    // Solicita ao usuário as frequências iniciais dos sensores.
    std::cout << "Entre com a frequência do sensor 'cpu' (em segundos): ";
    std::cin >> cpuFrequency;
    std::cout << "Entre com a frequência do sensor 'memória'(em segundos)";
    std::cin >> memFrequency;

    // Solicita ao usuário a frequência de publicação da mensagem inicial.
    int initialMessageFrequency;
    std::cout << "Entre com a frequência da mensagem inicial(em segundos): ";
    std::cin >> initialMessageFrequency;

    // Publica a mensagem inicial
    publishInitialMessage(client);

    // Inicia uma thread para cada sensor com frequências diferentes.
    std::thread cpuThread(sensorTask, std::ref(client), "cpu", cpuFrequency);
    std::thread memThread(sensorTask, std::ref(client), "mem", memFrequency);

    // Periodicamente publicar a mensagem inicial
    while (!exitRequested) {
        std::this_thread::sleep_for(std::chrono::seconds(initialMessageFrequency));
        publishInitialMessage(client);
    }
    // Aguardar até que as threads terminem
    cpuThread.join();
    memThread.join();

    return EXIT_SUCCESS;
}
