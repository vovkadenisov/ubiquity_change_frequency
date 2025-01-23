# ubiquity_change_frequency
программа для ручной смены смены частот по времени, на антеннах ubiquity m2, m5. по сути достаточно добавить класс для любой антенны.
инструмент подключения по telnet.


например класс для mikrotik w60g
```
class Mikrotik_lhg60 : public Device {
public:
    Mikrotik_lhg60(const std::string& ip, int base_freq, int secondary_freq, int base_hour, int base_minute,
                   int secondary_hour, int secondary_minute, const std::string& login, const std::string& password)
        : Device(ip, base_freq, secondary_freq, base_hour, base_minute, secondary_hour, secondary_minute, login, password) {}

	bool changeFrequency(int freq) override {
    std::string command = "interface w60g set frequency=" + std::to_string(freq) + "\nquit\n";
    std::cout << "Executing command for Mikrotik_lhg60: " << command << "\n";
    return sendTelnetCommand(ip, command, login, password);
	}
};
```

и далее задать параметры в файл devices.yaml

```
- ip: 10.x.x.x
  base_freq: 62640
  secondary_freq: 60480
  base_hour: 10
  base_minute: 15
  secondary_hour: 6
  secondary_minute: 0
  type: Mikrotik_lhg60
  login: admin
  password: admin
```
