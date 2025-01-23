#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <ctime>
#include <thread>
#include <chrono>
#include <cstring>
#include <mutex>
#include <unistd.h>
#include <arpa/inet.h>
#include <yaml-cpp/yaml.h>

#define ALL_OK 0
#define NO_DEV_IN_FILE 1

bool sendTelnetCommand(const std::string& ip, const std::string& command, const std::string& login, const std::string& password);

std::mutex devicesMutex;

class Device {
protected:
    std::string ip;
    int base_freq;
    int secondary_freq;
    int base_hour;
    int base_minute;
    int secondary_hour;
    int secondary_minute;
    std::string login;
    std::string password;

public:
    Device(const std::string& ip, int base_freq, int secondary_freq, int base_hour, int base_minute,
           int secondary_hour, int secondary_minute, const std::string& login, const std::string& password)
        : ip(ip), base_freq(base_freq), secondary_freq(secondary_freq), base_hour(base_hour),
          base_minute(base_minute), secondary_hour(secondary_hour), secondary_minute(secondary_minute),
          login(login), password(password) {}

    virtual ~Device() = default;

    virtual bool changeFrequency(int freq) = 0;

    int getBaseHour() const { return base_hour; }
    int getBaseMinute() const { return base_minute; }
    int getSecondaryHour() const { return secondary_hour; }
    int getSecondaryMinute() const { return secondary_minute; }
    const std::string& getIp() const { return ip; }
    int getBaseFreq() const { return base_freq; }
    int getSecondaryFreq() const { return secondary_freq; }
};

class NanoStation_m2 : public Device {
public:
    NanoStation_m2(const std::string& ip, int base_freq, int secondary_freq, int base_hour, int base_minute,
                   int secondary_hour, int secondary_minute, const std::string& login, const std::string& password)
        : Device(ip, base_freq, secondary_freq, base_hour, base_minute, secondary_hour, secondary_minute, login, password) {}

	bool changeFrequency(int freq) override {
    std::string command = "iwconfig ath0 freq " + std::to_string(freq) + "M";
    std::cout << "Executing command for NanoStation M2: " << command << "\n";
    return sendTelnetCommand(ip, command, login, password);
	}
};

class NanoBeam_m5 : public Device {
public:
    NanoBeam_m5(const std::string& ip, int base_freq, int secondary_freq, int base_hour, int base_minute,
                int secondary_hour, int secondary_minute, const std::string& login, const std::string& password)
        : Device(ip, base_freq, secondary_freq, base_hour, base_minute, secondary_hour, secondary_minute, login, password) {}

    bool changeFrequency(int freq) override {
        std::string command = "chsw " + std::to_string(freq) + " " + std::to_string(freq) + "\nexit\n";
        std::cout << "Executing command for NanoBeam M5: " << command << "\n";
        return sendTelnetCommand(ip, command, login, password);
    }
};

bool sendTelnetCommand(const std::string& ip, const std::string& command, const std::string& login, const std::string& password) {
    const int port = 23; // Telnet порт
    int sock;
    struct sockaddr_in server_addr;

    try {
        sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) {
            perror("Socket creation failed");
            return false;
        }

        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(port);
        if (inet_pton(AF_INET, ip.c_str(), &server_addr.sin_addr) <= 0) {
            std::cerr << "Invalid address: " << ip << "\n";
            close(sock);
            return false;
        }

        if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
            perror("Connection failed");
            close(sock);
            return false;
        }

        std::cout << "Connected to " << ip << "\n";

        char buffer[1024];
        bool loggedIn = false;

        auto readTelnet = [&](std::string& response) -> int {
            int bytesRead = read(sock, buffer, sizeof(buffer) - 1);
            if (bytesRead > 0) {
                buffer[bytesRead] = '\0';
                response = buffer;

                // Фильтрация управляющих символов
                response.erase(
                    std::remove_if(response.begin(), response.end(),
                                   [](char c) { return !isprint(c) && c != '\n' && c != '\r'; }),
                    response.end());
            }
            return bytesRead;
        };

        while (true) {
            std::string response;
            int bytesRead = readTelnet(response);

            if (bytesRead <= 0) break;
            std::cout << "Response from device: " << response << "\n";

            if (response.find("login:") != std::string::npos) {
                // Отправляем логин
                send(sock, (login + "\n").c_str(), login.length() + 1, 0);
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            } else if (response.find("Password:") != std::string::npos) {
                // Отправляем пароль
                send(sock, (password + "\n").c_str(), password.length() + 1, 0);
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                loggedIn = true;
            } else if (loggedIn && response.find("#") != std::string::npos) {
                // Ожидаем приглашение ввода команды
                send(sock, (command + "\n").c_str(), command.length() + 1, 0);
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                readTelnet(response);

                if (response.find("Error") != std::string::npos || response.find("Invalid") != std::string::npos) {
                    std::cerr << "Command failed: " << response << "\n";
                    close(sock);
                    return false;
                } else {
                    std::cout << "Command executed successfully: " << response << "\n";
                }

                // Закрываем сессию
                send(sock, "exit\n", 5, 0);
                break;
            }
        }

        close(sock);
        return true;
    } catch (const std::exception& ex) {
        std::cerr << "Error while sending Telnet command to " << ip << ": " << ex.what() << "\n";
        if (sock >= 0) close(sock);
        return false;
    }
}

void logToFile(const std::string& message) {
    std::ofstream logFile("telnet.log", std::ios::app);
    if (logFile.is_open()) {
        logFile << "[" << std::time(nullptr) << "] " << message << "\n";
        logFile.close();
    }
}


std::vector<Device*> parseYaml(const std::string& filename) {
    std::vector<Device*> devices;
    try {
        YAML::Node config = YAML::LoadFile(filename);

        for (const auto& node : config) {
            std::string ip = node["ip"].as<std::string>();
            int base_freq = node["base_freq"].as<int>();
            int secondary_freq = node["secondary_freq"].as<int>();
            int base_hour = node["base_hour"].as<int>();
            int base_minute = node["base_minute"].as<int>();
            int secondary_hour = node["secondary_hour"].as<int>();
            int secondary_minute = node["secondary_minute"].as<int>();
            std::string type = node["type"].as<std::string>();
            std::string login = node["login"].as<std::string>();
            std::string password = node["password"].as<std::string>();

            if (type == "NanoStation_m2") {
                devices.push_back(new NanoStation_m2(ip, base_freq, secondary_freq, base_hour, base_minute,
                                                     secondary_hour, secondary_minute, login, password));
            } else if (type == "NanoBeam_m5") {
                devices.push_back(new NanoBeam_m5(ip, base_freq, secondary_freq, base_hour, base_minute,
                                                  secondary_hour, secondary_minute, login, password));
            }
        }
    } catch (const std::exception& ex) {
        std::cerr << "Error parsing YAML: " << ex.what() << "\n";
    }
    return devices;
}

void processDevices(std::vector<Device*>& devices) {
    while (true) {
        std::time_t now = std::time(nullptr);
        std::tm* localTime = std::localtime(&now);
        int currentHour = localTime->tm_hour;
        int currentMinute = localTime->tm_min;

        std::lock_guard<std::mutex> lock(devicesMutex); // Блокировка списка устройств
        for (auto* device : devices) {
            if (currentHour == device->getBaseHour() && currentMinute == device->getBaseMinute()) {
                device->changeFrequency(device->getBaseFreq());
            } else if (currentHour == device->getSecondaryHour() && currentMinute == device->getSecondaryMinute()) {
                device->changeFrequency(device->getSecondaryFreq());
            }
        }

        std::this_thread::sleep_for(std::chrono::seconds(30));
    }
}

void updateDevices(const std::string& filename, std::vector<Device*>& devices) {
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(60)); // Проверяем конфигурацию каждые 60 секунд
        std::vector<Device*> newDevices = parseYaml(filename);

        if (!newDevices.empty()) {
            std::lock_guard<std::mutex> lock(devicesMutex); // Блокировка списка устройств
            for (auto* device : devices) {
                delete device;
            }
            devices = std::move(newDevices);
            std::cout << "Device list updated from " << filename << "\n";
        }
    }
}

int main() {
    const std::string devicesFile = "devices.yaml";
    std::vector<Device*> devices = parseYaml(devicesFile);

    if (devices.empty()) {
        std::cerr << "No devices found in devices.yaml.\n";
        return NO_DEV_IN_FILE;
    }

    std::thread updateThread(updateDevices, devicesFile, std::ref(devices)); // Поток для обновления устройств
    processDevices(devices); // Основной поток для обработки
    updateThread.join();

    return ALL_OK;
}
