#include <iostream>
#include <sstream>
#include <cstdlib>
#include <chrono>
#include <ctime>
#include <thread>
#include <unistd.h>
#include "json.hpp" // json handling
#include "mqtt/client.h" // paho mqtt
#include <iomanip>
#include <fstream>

#define QOS 1
#define BROKER_ADDRESS "tcp://localhost:1883"



std::string getSensorID() {
    // Seed para geração de números aleatórios
    std::srand(static_cast<unsigned int>(std::time(nullptr)));

    // Criar um vetor com as opções
    std::vector<std::string> options = {"cpu", "disk", "mem"};

    // Embaralhar o vetor
    std::random_shuffle(options.begin(), options.end());

    // Retornar o primeiro elemento do vetor (agora embaralhado)
    return options[0];
}


float getMemoryUsage() {
    std::ifstream meminfo("/proc/meminfo");
    std::string line;
    float total_mem, free_mem;

    // Read total memory
    std::getline(meminfo, line);
    std::istringstream total_mem_stream(line.substr(line.find(":") + 1));
    total_mem_stream >> total_mem;

    // Read free memory
    std::getline(meminfo, line);
    std::istringstream free_mem_stream(line.substr(line.find(":") + 1));
    free_mem_stream >> free_mem;

    // Calculate memory usage percentage
    float used_mem = total_mem - free_mem;
    float mem_usage_percentage = (used_mem / total_mem) * 100.0f;

    return mem_usage_percentage;
}

float getCPUUsage() {
    std::ifstream stat("/proc/stat");
    std::string line;
    long user, nice, system, idle, iowait, irq, softirq;

    // Read CPU usage information
    std::getline(stat, line);
    std::istringstream cpu_stream(line.substr(5)); // Skip "cpu"
    cpu_stream >> user >> nice >> system >> idle >> iowait >> irq >> softirq;

    // Calculate total CPU time
    long total_cpu_time = user + nice + system + idle + iowait + irq + softirq;

    // Calculate CPU usage percentage
    float cpu_usage_percentage = (1.0f - static_cast<float>(idle) / total_cpu_time) * 100.0f;

    return cpu_usage_percentage;
}

float getSensorValue(const std::string& sensorID) {
    if(sensorID == "cpu"){
        return getCPUUsage();
    }
    else{
        return getMemoryUsage();
    }
}


int main(int argc, char* argv[]) {
    
    std::string clientId = "sensor-monitor";
    mqtt::client client(BROKER_ADDRESS, clientId);
    
    // Connect to the MQTT broker.
    mqtt::connect_options connOpts;
    connOpts.set_keep_alive_interval(20);
    connOpts.set_clean_session(true);

    try {
        client.connect(connOpts);
    } catch (mqtt::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    
    std::clog << "connected to the broker" << std::endl;

    // Get the unique machine identifier, in this case, the hostname.
    char hostname[1024];
    gethostname(hostname, 1024);
    std::string machineId(hostname);

    while (true) {
        std::string sensorID = getSensorID();        
       // Get the current time in ISO 8601 format.
        auto now = std::chrono::system_clock::now();
        std::time_t now_c = std::chrono::system_clock::to_time_t(now);
        std::tm* now_tm = std::localtime(&now_c);
        std::stringstream ss;
        ss << std::put_time(now_tm, "%FT%TZ");
        std::string timestamp = ss.str();

        // Generate a random value.
        int value = rand();

        // Construct the JSON message.
        nlohmann::json j;
        j["timestamp"] = timestamp;
        j["value"] = getSensorValue(sensorID);

        // Publish the JSON message to the appropriate topic.
        std::string topic = "/sensors/" + machineId + "/" + sensorID;
        mqtt::message msg(topic, j.dump(), QOS, false);
        std::clog << "message published - topic: " << topic << " - message: " << j.dump() << std::endl;
        client.publish(msg);

        // Sleep for some time.
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    return EXIT_SUCCESS;
}