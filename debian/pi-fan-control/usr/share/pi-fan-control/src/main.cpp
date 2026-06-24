#include <iostream>
#include <fstream>
#include <string>
#include <chrono>
#include <thread>
#include <cstdlib>
#include <vector>
#include <algorithm>
#include <sstream>
#include <atomic>
#include <mutex>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <lgpio.h>

const std::string PWM_CHIP    = "/sys/class/pwm/pwmchip0";
const std::string CONFIG_FILE = "/etc/pi-fan-control.conf";
const int PERIOD              = 40000;
const int DEFAULT_TACH_PIN    = 24;
const int DEFAULT_PORT        = 7777;

struct TempSpeed {
    float temp;
    int speed;
};

std::vector<TempSpeed> tempTable = {
    { 40.0f,  0  },
    { 50.0f,  25 },
    { 60.0f,  50 },
    { 70.0f,  75 },
    { 80.0f, 100 }
};

std::atomic<int>   manualSpeed(-1);
std::atomic<float> currentTemp(0.0f);
std::atomic<int>   currentSpeed(0);
std::atomic<int>   currentRPM(0);
std::mutex         pwmMutex;

int gpioHandle = -1;
int TACH_PIN   = DEFAULT_TACH_PIN;
int SOCKET_PORT = DEFAULT_PORT;

// ── Config ───────────────────────────────────────────────────────────────────

void loadConfig() {
    std::ifstream file(CONFIG_FILE);
    if (!file.is_open()) {
        std::cout << "No config at " << CONFIG_FILE << ", using defaults." << std::endl;
        return;
    }

    // Clear default table so config fully controls thresholds
    tempTable.clear();

    std::string line;
    while (std::getline(file, line)) {
        // Strip comments and trim whitespace
        auto comment = line.find('#');
        if (comment != std::string::npos)
            line = line.substr(0, comment);
        if (line.empty()) continue;

        std::istringstream iss(line);
        std::string key;
        if (!(iss >> key)) continue;

        if (key == "tach_pin") {
            iss >> TACH_PIN;
            std::cout << "Config: tach_pin=" << TACH_PIN << std::endl;
        } else if (key == "port") {
            iss >> SOCKET_PORT;
            std::cout << "Config: port=" << SOCKET_PORT << std::endl;
        } else if (key == "temp_speed") {
            float temp; int speed;
            iss >> temp >> speed;
            tempTable.push_back({temp, speed});
            std::cout << "Config: temp_speed " << temp << " -> " << speed << "%" << std::endl;
        }
    }
    file.close();
}

// ── PWM helpers ──────────────────────────────────────────────────────────────

void writeToFile(const std::string& path, const std::string& value) {
    std::ofstream file(path);
    if (file.is_open()) {
        file << value;
    } else {
        std::cerr << "Failed to open: " << path << std::endl;
    }
}

void setup() {
    system("pinctrl set 12 a0");
    writeToFile(PWM_CHIP + "/unexport", "0");
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    writeToFile(PWM_CHIP + "/export", "0");
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    writeToFile(PWM_CHIP + "/pwm0/period",     std::to_string(PERIOD));
    writeToFile(PWM_CHIP + "/pwm0/duty_cycle", "0");
    writeToFile(PWM_CHIP + "/pwm0/enable",     "1");

    gpioHandle = lgGpiochipOpen(0);
    if (gpioHandle >= 0)
        lgGpioClaimInput(gpioHandle, LG_SET_PULL_UP, TACH_PIN);
}

void setSpeed(int percent) {
    std::lock_guard<std::mutex> lock(pwmMutex);
    percent = std::max(0, std::min(100, percent));
    int duty = PERIOD * percent / 100;
    writeToFile(PWM_CHIP + "/pwm0/duty_cycle", std::to_string(duty));
    currentSpeed.store(percent);
}

// ── RPM reader ───────────────────────────────────────────────────────────────

void rpmLoop() {
    while (true) {
        if (gpioHandle < 0) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }

        int pulseCount = 0;
        int last = 1;
        auto start = std::chrono::steady_clock::now();

        while (std::chrono::steady_clock::now() - start < std::chrono::seconds(5)) {
            int val = lgGpioRead(gpioHandle, TACH_PIN);
            if (val == 0 && last == 1)
                pulseCount++;
            last = val;
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }

        int rpm = (pulseCount / 2) * 12;
        currentRPM.store(rpm);
    }
}

// ── Temperature ──────────────────────────────────────────────────────────────

float getCpuTemp() {
    FILE* pipe = popen("vcgencmd measure_temp", "r");
    if (!pipe) return 0.0f;
    char buffer[64];
    fgets(buffer, sizeof(buffer), pipe);
    pclose(pipe);
    float temp = 0.0f;
    sscanf(buffer, "temp=%f", &temp);
    return temp;
}

int tempToSpeed(float temp) {
    std::sort(tempTable.begin(), tempTable.end(),
              [](const TempSpeed& a, const TempSpeed& b){ return a.temp < b.temp; });
    if (temp < tempTable.front().temp) return 0;
    if (temp >= tempTable.back().temp) return tempTable.back().speed;
    for (size_t i = 0; i < tempTable.size() - 1; i++) {
        if (temp >= tempTable[i].temp && temp < tempTable[i+1].temp)
            return tempTable[i].speed;
    }
    return 100;
}

// ── Fan control loop ─────────────────────────────────────────────────────────

void fanLoop() {
    while (true) {
        float temp = getCpuTemp();
        currentTemp.store(temp);
        int manual = manualSpeed.load();
        int speed  = (manual >= 0) ? manual : tempToSpeed(temp);
        setSpeed(speed);
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }
}

// ── Socket server ────────────────────────────────────────────────────────────

void handleClient(int clientFd) {
    char buf[256] = {};

    auto statusStr = [&]() {
        std::ostringstream s;
        s << "temp="  << currentTemp.load()
          << " speed=" << currentSpeed.load()
          << " rpm="   << currentRPM.load()
          << " mode="  << (manualSpeed.load() >= 0 ? "manual" : "auto")
          << "\n";
        return s.str();
    };

    int n = recv(clientFd, buf, sizeof(buf) - 1, 0);
    if (n <= 0) { close(clientFd); return; }

    std::string cmd(buf);
    cmd.erase(std::remove_if(cmd.begin(), cmd.end(), [](char c){ return c == '\n' || c == '\r'; }), cmd.end());

    std::string response;

    if (cmd == "status") {
        response = statusStr();

    } else if (cmd == "auto") {
        manualSpeed.store(-1);
        response = "OK mode=auto\n";

    } else if (cmd.rfind("speed=", 0) == 0) {
        try {
            int val = std::stoi(cmd.substr(6));
            val = std::max(0, std::min(100, val));
            manualSpeed.store(val);
            setSpeed(val);
            response = "OK speed=" + std::to_string(val) + "\n";
        } catch (...) {
            response = "ERR invalid speed value\n";
        }

    } else {
        response = "ERR unknown command. Use: status | speed=0-100 | auto\n";
    }

    send(clientFd, response.c_str(), response.size(), 0);
    close(clientFd);
}

void socketServer() {
    int serverFd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(serverFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(SOCKET_PORT);

    bind(serverFd, (sockaddr*)&addr, sizeof(addr));
    listen(serverFd, 5);

    std::cout << "Fan control listening on port " << SOCKET_PORT << std::endl;

    while (true) {
        int clientFd = accept(serverFd, nullptr, nullptr);
        if (clientFd >= 0)
            std::thread(handleClient, clientFd).detach();
    }
}

// ── Main ─────────────────────────────────────────────────────────────────────

int main() {
    loadConfig();
    setup();

    std::cout << "Pi fan control running on port " << SOCKET_PORT << std::endl;

    std::thread fan(fanLoop);
    std::thread rpm(rpmLoop);
    std::thread sock(socketServer);

    fan.join();
    rpm.join();
    sock.join();

    return 0;
}
