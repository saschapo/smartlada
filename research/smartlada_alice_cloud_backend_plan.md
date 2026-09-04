# SmartLada × Алиса — облачный backend (Yandex Smart Home API) на saschapo.me

Дата: 2026-08-20. Решение: путь **B (облако Wi-Fi)** выбран, т.к. только он даёт
**программные имена ламп, имена пресетов и группировку** (см.
[`smartlada_alice_wifi_matter_vs_zigbee.md`](smartlada_alice_wifi_matter_vs_zigbee.md)).
Стек backend: **Go**. Сервер уже есть.

## Сервер (готово, предпосылки Яндекса выполнены)

- vdsina VPS, **IP 178.217.99.181**, Ubuntu, **1 ядро / 1 ГБ RAM / 10 ГБ**.
- **HTTPS (Let's Encrypt) на saschapo.me** — Яндексу нужен HTTPS Endpoint. ✅
- nginx отдаёт портфолио + **`video.saschapo.me` (cineink) — НЕ ТРОГАТЬ**. PocketBase (127.0.0.1:8090) — планировался, не трогаем.
- Открыты только 80/443. MQTT-брокера нет. Node не стоит (и не нужен — Go = один бинарь).

## Ограничение платформы (напоминание)

`range/brightness` — один на устройство; «4 диммера в 1 карточке» невозможны. Отдаём **4
отдельных light-устройства**. Эффекты — `color_setting/scene` (16 фикс-сцен: party, night,
ocean, movie…), произвольные фразы — через сценарии Алисы.

## Архитектура

```
ESP32-C6 (Wi-Fi, MQTT/TLS client)
        │  publish: state    subscribe: cmd
        ▼
   mosquitto  (VPS :8883, TLS + login/pass, retained state + LWT)
        ▲
        │ Go service (systemd, слушает 127.0.0.1:PORT)
        │  - MQTT-мост (кэширует last-state для <=3c query)
        │  - OAuth2-провайдер (/authorize, /token)
        │  - Yandex provider REST (/v1.0/...)
        ▼  HTTPS
   nginx: smarthome.saschapo.me (свой server-блок + отдельный certbot-cert)
        ▲
        │ HTTPS
   Яндекс Smart Home API  ◀──▶  «Дом с Алисой» / Станция Midi
```

## Что реализует Go-сервис

**Yandex provider REST** (все требуют `Authorization: Bearer <наш_токен>`; SLA ответа ≤3 c):
- `HEAD /v1.0` — health-check.
- `GET  /v1.0/user/devices` — список устройств (4 лампы: id, name RU, room «Фара», type
  `devices.types.light`, capabilities).
- `POST /v1.0/user/devices/query` — текущее состояние (берём из MQTT-кэша).
- `POST /v1.0/user/devices/action` — выполнить действие → трансляция в MQTT-команду.
- `POST /v1.0/user/unlink` — отвязка аккаунта.
> Точные пути/схемы сверить с актуальной докой в Фазе 1 (структура URL Яндекса менялась).

**OAuth2-провайдер** (самое муторное; для личного одно-пользовательского — минимально):
- `GET /authorize` (Яндекс редиректит пользователя) → выдать auth code.
- `POST /token` → обмен code/refresh на access token.
- Хранить один аккаунт (ты), токены — в SQLite/файле.

**Модель устройств** (4 лампы):
- capability `on_off`
- capability `range` instance `brightness` (0..100, unit percent)
- (опц.) capability `color_setting` instance `scene` — эффекты; либо 5-е «устройство-фара»
  со сценами на всю фару.

## MQTT-контракт (черновик)

- Топики: `smartlada/<devId>/cmd` (backend→ESP32), `smartlada/<devId>/state` (ESP32→backend, retained).
- Payload cmd: `{"on":true,"bri":30,"scene":"party"}`; state — то же, что применено.
- LWT: `smartlada/<id>/state` → `{"online":false}` при обрыве → backend отдаёт Яндексу «недоступно».
- ESP32: Wi-Fi STA + MQTT/TLS клиент, reconnect, publish retained state, mapping bri%→duty (gamma/soft-start как в channels).

## Безопасность и изоляция (сервер боевой)

- Всё изолированно: отдельный сабдомен/сертификат/systemd-юзер; существующие блоки nginx,
  PocketBase, cineink — не трогаем.
- MQTT только **8883 TLS + логин/пароль**, не голый 1883. Фаервол: открыть 8883 адресно.
- Go-сервис слушает **только 127.0.0.1**, наружу — через nginx.
- Ресурсы: mosquitto + один Go-бинарь + nginx влезают в 1 ГБ; мониторить RAM.

## Фазовый план (test-first, как с Zigbee)

**Фаза 0 — инфра на VPS + MQTT round-trip.** Поставить mosquitto (TLS+auth), открыть 8883,
завести сабдомен `smarthome.saschapo.me` + certbot, systemd-скелет. Тест-скетч ESP32-C6
(Wi-Fi+MQTT), проверить публикацию/подписку через брокер. **Гейт: ESP32 ↔ mosquitto работает.**

**Фаза 1 — Go backend с MOCK-устройствами + OAuth + навык.** Реализовать provider-эндпоинты
с 4 захардкоженными лампами (без ESP32), OAuth2, создать навык «Умный дом» в Яндекс.Диалогах,
account linking. **Гейт (главный по нашей цели): 4 лампы появились в «Доме с Алисой» с русскими
именами и в комнате «Фара», программно.** Это решает боль с именами — то, ради чего облако.

**Фаза 2 — связать MQTT.** action→MQTT→ESP32 драйвит каналы; ESP32 публикует state→кэш для
query. On/off + яркость end-to-end.

**Фаза 3 — эффекты.** Добавить `color_setting/scene`, маппинг сцен→режимы ESP32, проверить
голос «режим вечеринки». Опц. 5-е устройство-фара.

**Фаза 4 — харденинг.** TLS везде, reconnect/LWT, бэкап токенов, финальная изоляция.

## Спецификация протокола = бриф (проверено 2026-08-20)

Детальный контракт Яндекса — в [`smartlada_yandex_smart_home_implementation_brief.md`](smartlada_yandex_smart_home_implementation_brief.md)
(собран с ChatGPT по офиц. докам). Я **сверил 3 самых дорогих факта** — все подтверждены докой:
- OAuth `redirect_uri` Диалогов = `https://social.yandex.net/broker/redirect`;
- сервис уведомлений = `POST https://dialogs.yandex.net/api/v1/skills/{skill_id}/callback/state`
  (Authorization: OAuth токен владельца; тело `ts`+`payload{user_id,devices[]}`; status online/offline);
- поле `room` в discovery СУЩЕСТВУЕТ (optional) — авто-раскладка по комнате в UI не гарантирована
  докой, проверяем эмпирически в Фазе 1 (это наша авто-«Фара»).

Из брифа берём как корректность: action НЕ отвечает DONE на постановку в очередь (ждать ack ESP32
в 3с иначе DEVICE_UNREACHABLE); один атомарный state-документ на фонарь + `seq`/mutex; постоянные
device ID; `brightness 1..100`, выкл=`on_off=false`; эхо `X-Request-Id`→`request_id`; `HEAD /v1.0`
без bearer; НЕ использовать scopes `iot:view/iot:control` (это другой, пользовательский API).

Мои инженерные поправки (больше контекста проекта):
1. 3с-ack — операционный риск (лампа на домашнем Wi-Fi через интернет): жёсткий таймаут ~1.5с.
2. `reportable`: в облачном MVP (рулит только Алиса) = `false`; в продукте (есть OLED-кнопки) = `true`
   + notification-сервис обязателен.
3. MQTT наружу: не открывать 8883 всему интернету. mTLS или WireGuard-туннель ESP32<->VPS.

Деплой-модель: **A — «я пишу, ты деплоишь»** (ssh/rsync с Mac). Из sandbox VPS достижим (443/22),
но root-доступ агенту к боевому боксу не даём. Код в `smarthome_backend/` (отд. от портфолио).

## Открытые вопросы

- Точные URL/схемы provider-протокола Яндекса — сверить в Фазе 1.
- Минимальный OAuth2: одно-пользовательский или полноценный.
- Эффекты: `scene` на каждую лампу или одно «устройство-фара» со сценами.
- Где физически лампа и как ESP32 достаёт до брокера (тот же интернет, TLS).
