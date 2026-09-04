# RevCMqttTls — Фаза 2.1: ESP32-C6 фонарь по MQTT/mTLS

Устройство-половина облачного пути: коннектится к mosquitto (`mqtt.saschapo.me:8883`) по
**клиентскому сертификату (mTLS)**, рулит 4 каналами из `cmd`, публикует применённое состояние
(retained) в `state`. Без Zigbee — чистый Wi-Fi + TLS.

Гейт 2.1: ESP32 подключается по cert, получает `cmd`, двигает канал, публикует `state`.
(Go-backend тут ещё не нужен — «бэкенд» имитируем `mosquitto_pub/sub` с сервера.)

Стенд: RevA плата + nanoESP32-C6, пины каналов Rev C `{1,0,2,3}`, NeoPixel GPIO8.
Контракт топиков — [`../../smarthome_backend/deploy/mqtt/README.md`](../../smarthome_backend/deploy/mqtt/README.md).

## 1. secrets.h (не коммитится)
```bash
cd "/Users/saschapo/Documents/Claude/Projects/VAZ smart light/sketches/RevCMqttTls" && cp secrets.example.h secrets.h
```
Открой `secrets.h`, впиши Wi-Fi SSID/пароль и вставь три PEM из `smarthome_backend/deploy/mqtt/out/`:
- `CA_CERT` ← `out/ca.crt`
- `CLIENT_CERT` ← `out/esp32-smartlada-01.crt`
- `CLIENT_KEY` ← `out/esp32-smartlada-01.key`

Распечатать содержимое для копипаста:
```bash
for f in ca.crt esp32-smartlada-01.crt esp32-smartlada-01.key; do echo "===== $f ====="; cat "../../smarthome_backend/deploy/mqtt/out/$f"; done
```
Сохраняй строки `-----BEGIN...`/`-----END...` целиком (raw-литерал `R"EOF(...)EOF"` держит переводы строк).

## 2. Сборка и заливка
```bash
arduino-cli compile -b "esp32:esp32:esp32c6:CDCOnBoot=cdc,FlashSize=16M,PartitionScheme=huge_app" sketches/RevCMqttTls
```
```bash
arduino-cli upload -b "esp32:esp32:esp32c6:CDCOnBoot=cdc,FlashSize=16M,PartitionScheme=huge_app" -p /dev/cu.usbmodem2101 sketches/RevCMqttTls
```
`huge_app` — с запасом по флешу (TLS+WiFi+MQTT тяжёлые; на дефолтном разделе ~84%). Монитор:
```bash
arduino-cli monitor -p /dev/cu.usbmodem2101 -c baudrate=115200
```

## 3. Проверка (гейт 2.1) — «бэкенд» имитируем вручную

После заливки в мониторе увидишь: `WiFi ok` → `MQTT connected` → `pub state seq=1` (NeoPixel зелёный).

**A. Смотрим публикуемое состояние** (как backend, на сервере; подставь пароль backend):
```bash
ssh -t root@178.217.99.181 "mosquitto_sub -h 127.0.0.1 -p 1883 -u backend -P 'ПАРОЛЬ_BACKEND' -t smartlada/smartlada-01/state -v"
```
Должно сразу прийти retained-состояние (ESP32 публикует его при подключении).

**B. Шлём команду** (как backend), лампа turn -> ON 50%:
```bash
ssh -t root@178.217.99.181 "mosquitto_pub -h 127.0.0.1 -p 1883 -u backend -P 'ПАРОЛЬ_BACKEND' -t smartlada/smartlada-01/cmd -m '{\"command_id\":\"t1\",\"set\":{\"turn\":{\"on\":true,\"brightness\":50}}}'"
```
✅ В мониторе ESP32: `cmd on ...` → `ch0 turn ON bri=50 -> duty=...` → `pub state ... command_id t1`.
В окне A прилетит новое состояние с `"command_id":"t1"`. Если к OUT0 подключена лампа — загорится ~50%.

**C. Выключение / другой канал:**
```bash
ssh -t root@178.217.99.181 "mosquitto_pub -h 127.0.0.1 -p 1883 -u backend -P 'ПАРОЛЬ_BACKEND' -t smartlada/smartlada-01/cmd -m '{\"command_id\":\"t2\",\"set\":{\"brake\":{\"on\":true,\"brightness\":100},\"turn\":{\"on\":false,\"brightness\":50}}}'"
```
Проверь, что оба канала отработали и в state оба обновились.

**D. Тест офлайна (LWT):** выдерни питание ESP32 → через ~keepalive backend-подписка в окне A
получит retained `{"online":false}` (LWT). Это то, по чему Фаза 2.2 отдаст Яндексу `DEVICE_UNREACHABLE`.

## Что дальше (Фаза 2.2)
Go-backend: `store.go` mock -> MQTT-клиент. `action` шлёт `cmd` с `command_id`, ждёт совпадающий
`state` (<=1.5с) -> `DONE`/`DEVICE_UNREACHABLE`; `query` читает кэш последнего `state`; LWT
`online:false` -> `DEVICE_UNREACHABLE`. Один mutex/seq на фонарь против гонок при групповых командах.
