#include <iostream>
#include <cstdlib>
#include <chrono>
#include <thread>
#include <unistd.h>
#include "json.hpp" 
#include "mqtt/client.h" 
#include <sstream>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#define UPDATE_INTERVAL 10
#define INACTIVITY_THRESHOLD 10
#define QOS 1
#define BROKER_ADDRESS "tcp://localhost:1883"
#define GRAPHITE_HOST "127.0.0.1"
#define GRAPHITE_PORT 2003

struct SensorSubscription {
    std::string machine_id;
    std::string sensor_id;
    std::chrono::system_clock::time_point subscription_time;
};


std::vector<SensorSubscription> sensor_subscriptions;
bool inative_sensor = false; // Inicializado como falso

void post_metric(const std::string& machine_id, const std::string& sensor_id, const std::string& timestamp_str, const int value, bool is_alarm) {
    std::string metric_path = machine_id + ".";

    if (is_alarm) {
        // Adiciona parte específica para alarmes
        metric_path += "alarms.";
    }

    metric_path += sensor_id;

    // Converte o timestamp para segundos em relação a um referencial
    std::istringstream timestamp_stream(timestamp_str);
    std::tm timestamp_tm = {};
    timestamp_stream >> std::get_time(&timestamp_tm, "%Y-%m-%dT%H:%M:%SZ");
    auto timestamp = std::chrono::system_clock::from_time_t(std::mktime(&timestamp_tm));

    auto reference_time = std::chrono::system_clock::from_time_t(0);
    auto seconds_since_reference = std::chrono::duration_cast<std::chrono::seconds>(timestamp - reference_time).count();
   
    // Constroi a string da métrica no formato correto para o Graphite
    std::ostringstream metric_stream;
    metric_stream << metric_path << " " << std::to_string(value) << " " << seconds_since_reference << std::endl;
    std::string metric_data = metric_stream.str();

    // Cria um socket para se comunicar com o Graphite
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        std::cerr << "Error creating socket" << std::endl;
        return;
    }

    sockaddr_in graphite_addr;
    graphite_addr.sin_family = AF_INET;
    graphite_addr.sin_port = htons(GRAPHITE_PORT);
    inet_pton(AF_INET, GRAPHITE_HOST, &(graphite_addr.sin_addr));

    // Conecta ao servidor Graphite
    if (connect(sock, (struct sockaddr*)&graphite_addr, sizeof(graphite_addr)) == -1) {
        std::cerr << "Error connecting to Graphite" << std::endl;
        close(sock);
        return;
    }

    // Envia dados para o servidor Graphite
    send(sock, metric_data.c_str(), metric_data.length(), 0);

    // Fecha o socket
    close(sock);
}

std::vector<std::string> split(const std::string &str, char delim) {
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream tokenStream(str);
    while (std::getline(tokenStream, token, delim)) {
        tokens.push_back(token);
    }
    return tokens;
}

int main(int argc, char *argv[]) {
    std::string clientId = "clientId";
    mqtt::async_client client(BROKER_ADDRESS, clientId);

    // Create an MQTT callback.
    class callback : public virtual mqtt::callback {
    public:
        void message_arrived(mqtt::const_message_ptr msg) override {
            auto j = nlohmann::json::parse(msg->get_payload());

            std::string topic = msg->get_topic();
            std::cout << topic << std::endl;
            auto topic_parts = split(topic, '/');
            std::string machine_id = topic_parts[2];
            
            std::string sensor_id = topic_parts[3];
            std::cout << sensor_id << std::endl;
            std::string timestamp = j["timestamp"];
            std::cout << timestamp << std::endl;
            int value = j["value"];
            post_metric(machine_id, sensor_id, timestamp, value, inative_sensor);

            // Atualiza o tempo de subscrição para o sensor
            auto it = std::find_if(sensor_subscriptions.begin(), sensor_subscriptions.end(),
                                   [&](const SensorSubscription& s) { return s.sensor_id == sensor_id; });

            if (it != sensor_subscriptions.end()) {
                it->subscription_time = std::chrono::system_clock::now();
            } else {
                // Adiciona nova subscrição
                sensor_subscriptions.push_back({machine_id, sensor_id, std::chrono::system_clock::now()});
            }
        }
    };

    callback cb;
    client.set_callback(cb);

    // Connect to the MQTT broker.
    mqtt::connect_options connOpts;
    connOpts.set_keep_alive_interval(20);
    connOpts.set_clean_session(true);

    try {
        client.connect(connOpts)->wait();
        client.subscribe("/sensors/#", QOS);
        std::cout << "Subscribed\n";
    } catch (mqtt::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    while (true) {
        // Verifica inatividade e gerar alarmes
        auto current_time = std::chrono::system_clock::now();

        for (const auto &subscription : sensor_subscriptions) {
            auto time_difference = std::chrono::duration_cast<std::chrono::seconds>(
                current_time - subscription.subscription_time).count();
          
            
            if (time_difference > INACTIVITY_THRESHOLD) {
                std::cout << "Alarme de Inatividade para sensor " << subscription.sensor_id << std::endl;
                 // Convert current_time to ISO 8601 format in UTC
                 
                auto time_in_utc = std::chrono::system_clock::to_time_t(current_time);
                auto tm_utc = std::gmtime(&time_in_utc);
                std::ostringstream alarm_timestamp;
                alarm_timestamp << std::put_time(tm_utc, "%Y-%m-%dT%H:%M:%SZ");

                std::cout << "Alarme de Inatividade para sensor " << subscription.sensor_id << std::endl;
                post_metric(subscription.machine_id, subscription.sensor_id, alarm_timestamp.str(), 1, true);
    
                
            } else {
                inative_sensor = false; 
            }
        }

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    return EXIT_SUCCESS;
}
