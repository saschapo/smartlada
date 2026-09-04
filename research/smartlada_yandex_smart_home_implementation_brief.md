# SmartLada × Яндекс Умный дом — implementation brief

**Статус:** практическая спецификация для реализации на Go. Проверено по официальной документации Яндекса 20.08.2026.  
**Цель MVP:** в «Доме с Алисой» появляются четыре отдельно именованные и независимо диммируемые лампы одного физического фонаря SmartLada: «Поворотник», «Стоп-сигнал», «Габарит», «Задний ход».

> Этот документ намеренно отделяет контракт Яндекса от решений SmartLada. Не считай решения платформенными требованиями.

## 1. Вывод и границы MVP

Модель из исходного плана верна: **не один фонарь с четырьмя brightness, а четыре виртуально отдельные `devices.types.light`**. У одного умения `range` — одна функция/состояние `brightness`; отдельные устройства дают Алисе и приложению предсказуемые независимые элементы управления.

Не включать в MVP: RGB/цветовую температуру, эффекты, «пятое устройство-фонарь», пользовательские свойства и прямое API `api.iot.yandex.net`. Последнее — другой API: он предназначен для *пользовательских приложений*, управляющих уже подключённым домом Яндекса, а не для backend провайдера навыка.

## 2. Подтверждённые требования Яндекса

### Backend и протокол

- Нужен OAuth 2.0 authorization-code grant и provider API, преобразующий запросы Яндекса в команды устройству.
- Выбранный обычный backend корректно реализовать REST; JSON-RPC 2.0 Яндекс поддерживает только в Yandex Cloud Functions.
- Endpoint обязан быть HTTPS с валидной полной цепочкой сертификата (`fullchain`); самоподписанный сертификат не проходит проверку.
- Полный бюджет ответа — **3 секунды**, включая сетевое соединение и доставку; тело ответа не больше 5 000 символов.
- Логировать `X-Request-Id` каждого запроса и возвращать это же значение в JSON-поле `request_id`.
- Все ошибки, относящиеся к конкретному устройству, возвращать как HTTP 200 с `error_code`/`error_message` в объекте устройства (или capability action result). HTTP 401/403/404/500 — для ошибок самого запроса/ресурса.

### Обязательные REST-маршруты

Относительно Backend URL `https://smarthome.saschapo.me`:

| Метод | Маршрут | Требование |
|---|---|---|
| `HEAD` | `/v1.0` | Быстрый `200 OK`; health-check, без тела и без требования bearer-токена. |
| `GET` | `/v1.0/user/devices` | Discovery: список устройств текущего пользователя. |
| `POST` | `/v1.0/user/devices/query` | Состояния только запрошенных устройств. |
| `POST` | `/v1.0/user/devices/action` | Изменение состояний. |
| `POST` | `/v1.0/user/unlink` | Отвязка аккаунтов. Отозвать токен/связку локально; ответ `{"request_id":"…"}`. |

Кроме `HEAD`, проверять `Authorization: Bearer <access_token>` и владение запрошенным устройством; для POST также ожидать `Content-Type: application/json`. Не принимать токен из query string.

### OAuth account linking

- Диалоги передают на authorization endpoint как минимум `state`, `redirect_uri`, `response_type=code`, `client_id`, `scope`.
- После успешного входа вернуть на **строго разрешённый** `redirect_uri` Диалогов `https://social.yandex.net/broker/redirect` параметры `code`, исходный `state`, `client_id` и `scope`.
- Token endpoint обязан поддержать обмен authorization code на access token и refresh token, а также refresh grant. Диалоги сохраняют оба токена.
- Лимиты: access/refresh token до 2048 символов, `expires_in` — целое 1…4 294 967 296, OAuth-ответ до 5000 символов.
- Важная оговорка: авторизация через Яндекс ID допустима для **неофициальных** навыков, но документация прямо запрещает её для официальных. Для SmartLada лучше собственные локальные учётные данные/минимальный account service, если есть шанс на официальный навык.

### Устройства, умения, состояние

- `devices.types.light` — подходящий тип. У устройства должен быть хотя бы один capability или property; несуществующий/невалидный capability приводит к отклонению устройства.
- Для каждого из четырёх каналов нужны `devices.capabilities.on_off` и `devices.capabilities.range` с `instance: "brightness"`.
- Умения должны быть `retrievable: true`, потому что backend знает и должен отдавать фактическое состояние. `reportable: true` означает обязанность отправлять изменения через сервис уведомлений.
- Устройства можно вернуть с `room: "Фара"`, чтобы задать комнату при discovery; это поле — часть описания устройства в примерах Яндекса.
- `properties` для светильника не нужны: это телеметрия/датчики, а не управляемые функции.
- Если устройство офлайн, использовать `DEVICE_UNREACHABLE`; если ID отсутствует или не принадлежит пользователю — `DEVICE_NOT_FOUND`; неверная capability — `INVALID_ACTION`; значение вне объявленного диапазона — `INVALID_VALUE`.

### Публикация, тестирование, уведомления

- Для личной установки создать навык «Умный дом», поставить тип доступа **Приватный** и опубликовать: он появится в приложении «Дом с Алисой» только владельцу. Доступ можно раздать по одноразовой ссылке.
- В консоли Диалогов тестировать account linking, обновление discovery, query, action, устройства и группы. Логи консоли показывают запросы и ответы backend.
- Голосовое управление и сервис уведомлений доступны после публикации; приватная публикация подходит для тестирования до публичного релиза.
- Для будущей сертификации обязательны `device_info.manufacturer` и `device_info.model`, самостоятельное подключение устройства пользователем и отправка во внешний сервис уведомлений **каждого** изменения состояния вне Яндекса (кнопка, приложение, reconnect/offline). Не заявлять сертификацию или бейдж «Работает с Алисой» до выполнения этих требований.

## 3. Решения SmartLada (не требования Яндекса)

### Идентичность и доступ

- Один provider-user: `smartlada-owner-001`; таблица `users` оставляет путь к multi-user без переделки протокола.
- Постоянные device IDs: `smartlada-01-turn`, `smartlada-01-brake`, `smartlada-01-tail`, `smartlada-01-reverse`. Никогда не строить их из текущего имени.
- Разрешённый OAuth client и redirect URI — allowlist в конфигурации. Auth code одноразовый, короткоживущий, привязан к client ID, redirect URI и пользователю. Access/refresh токены хранить только как криптографические хэши (refresh — ротировать при обновлении).
- Первое подключение ESP32 регистрируется отдельным безопасным provisioning-потоком, а не автоматически по любому MQTT client ID.

### Представление четырёх устройств

| ID | `name` | Назначение |
|---|---|---|
| `smartlada-01-turn` | `Поворотник SmartLada` | Канал поворотника |
| `smartlada-01-brake` | `Стоп-сигнал SmartLada` | Канал стоп-сигнала |
| `smartlada-01-tail` | `Габарит SmartLada` | Канал габарита |
| `smartlada-01-reverse` | `Задний ход SmartLada` | Канал заднего хода |

Все имеют `room: "Фара"`, `type: "devices.types.light"`, `description` с назначением и `device_info` (`manufacturer: "SmartLada"`, модель/версии из конфигурации или прошивки).

**Brightness policy:** объявлять диапазон `1..100`, `precision: 1`, `unit: "unit.percent"`, `random_access: true`. Ноль не рекламировать как яркость: выключение — `on_off=false`. Это исключает двусмысленное «включён, но 0%». Физический PWM можно калибровать любой гамма-кривой, но backend всегда оперирует процентами API.

### Минимальный discovery-ответ (шаблон одного канала)

```json
{
  "request_id": "<X-Request-Id>",
  "payload": {
    "user_id": "smartlada-owner-001",
    "devices": [{
      "id": "smartlada-01-turn",
      "name": "Поворотник SmartLada",
      "description": "Канал поворотника фонаря SmartLada",
      "room": "Фара",
      "type": "devices.types.light",
      "capabilities": [
        {"type":"devices.capabilities.on_off","retrievable":true,"reportable":true},
        {"type":"devices.capabilities.range","retrievable":true,"reportable":true,
         "parameters":{"instance":"brightness","unit":"unit.percent","random_access":true,
                       "range":{"min":1,"max":100,"precision":1}}}
      ],
      "properties": [],
      "device_info":{"manufacturer":"SmartLada","model":"SmartLada Lantern v1",
                      "hw_version":"1.0","sw_version":"<firmware-version>"}
    }]
  }
}
```

## 4. Контракт backend ↔ ESP32

Это проектное решение; формат не задан Яндексом.

**Broker:** Mosquitto TLS на `8883`, индивидуальные credentials/ACL: ESP32 может читать только свой `cmd` и писать только свой `state`; backend — оба. Не публиковать MQTT на 1883.

**Топики:**

```text
smartlada/smartlada-01/cmd       # backend -> ESP32, non-retained, QoS 1
smartlada/smartlada-01/state     # ESP32 -> backend, retained, QoS 1
```

Один атомарный документ состояния исключает рассинхронизацию каналов:

```json
{
  "seq": 42,
  "online": true,
  "channels": {
    "turn":    {"on": true,  "brightness": 40},
    "brake":   {"on": false, "brightness": 100},
    "tail":    {"on": true,  "brightness": 15},
    "reverse": {"on": false, "brightness": 100}
  },
  "ts": "2026-08-20T12:34:56Z"
}
```

Команда включает `command_id`, полный желаемый patch и `request_id` Яндекса. ESP32 применяет команду, затем публикует фактически применённый state с тем же `command_id`. LWT публикует retained state с `online:false`.

**Критическое правило action:** не отвечать `DONE` только потому, что команда встала в MQTT-очередь. Ждать короткое подтверждение ESP32 в пределах общего лимита 3 секунд. При отсутствии подтверждения вернуть capability-level `ERROR` / `DEVICE_UNREACHABLE`; при подтверждении — `DONE`. Backend обновляет свой state cache только подтверждённым состоянием.

## 5. Логика provider API

### Query

1. Валидировать bearer-токен и IDs.
2. Читать последний **подтверждённый** state cache, а не делать синхронный MQTT round-trip.
3. Для каждого запрошенного канала вернуть `on_off` (`instance:"on", value:boolean`) и brightness (`instance:"brightness", value:1..100`) только если канал online.
4. При `online:false`, устаревшем state (порог — конфигурация, например 90 секунд) или неизвестном ответе — объект устройства с `error_code:"DEVICE_UNREACHABLE"`.

### Action

1. Обрабатывать все устройства/capabilities в одном входящем запросе, не только первое.
2. Валидация до отправки: device ID, право пользователя, тип `on_off` или `range/brightness`, абсолютное/относительное brightness, диапазон. Неподдерживаемое/некорректное действие возвращать точно на уровне capability.
3. Сериализовать patch по физическому фонарю: одновременные команды группе могут прийти параллельно. Mutex/очередь на `smartlada-01` и version/`seq` не дают последней команде потерять изменение другого канала.
4. Отправить MQTT command QoS 1, дождаться совпадающего `command_id`, ответить action result `DONE` или `ERROR` для каждого capability.
5. Не скрывать частичный успех: один канал может быть `DONE`, другой — `ERROR`.

### Unlink, offline и события

- `unlink`: отозвать access/refresh tokens, удалить связывание Yandex-account ↔ provider-user, оставить физическое устройство и локальную учётную запись.
- ESP32 state/LWT обновляет cache. При `reportable:true` backend посылает notification на `POST https://dialogs.yandex.net/api/v1/skills/{skill_id}/callback/state` с авторизационным токеном владельца навыка, `ts`, `user_id`, channel ID, `status: online|offline` и изменившимися capabilities.
- Нужны retry с ограничением, idempotency/dedup по `(device_id, seq)` и метрики. Не отправлять устаревшее состояние после нового.

## 6. Обязательные тесты и порядок работ

1. **Инфраструктура:** отдельный nginx server block/cert для `smarthome.saschapo.me`, Go только на `127.0.0.1`, Mosquitto TLS, systemd service, секреты в файле с правами доступа. Не изменять существующие `video.saschapo.me` и PocketBase.
2. **Protocol-first mock:** реализовать OAuth и пять REST маршрутов с четырьмя in-memory lights; создать приватный навык в Диалогах, настроить Backend URL + OAuth URLs; пройти account linking и Refresh devices.
3. **Contract tests:** fixtures discovery/query/action/unlink; тесты на request ID echo, отсутствующий/просроченный token, незнакомый ID, offline, invalid brightness, относительную яркость, несколько устройств и параллельные action.
4. **MQTT bridge:** проверить retained state, LWT, command acknowledgement, рестарт backend и ESP32; тестировать при отключённом Wi‑Fi и в момент одновременной команды группе.
5. **Консоль и голос:** в Диалогах проверить names, room, ползунок, on/off, состояние после перезапуска, group actions и голосовые фразы на реальном устройстве. Проверять raw log Диалогов при каждой неудаче.
6. **Уведомления:** после приватной публикации проверить внешнее изменение (физическая кнопка/перезагрузка/обрыв) и состояние offline/online в приложении.

### Definition of done MVP

- Все 4 канала появляются после обновления списка, имеют нужные русские имена, комнату и отдельные on/off + brightness controls.
- `query` всегда возвращает подтверждённый ESP32 state или явный `DEVICE_UNREACHABLE`; нет выдуманного успеха.
- `action` корректно обрабатывает multi-device запросы и подтверждается железом не дольше 3 секунд.
- OAuth linking и refresh работают после перезапуска Go-сервиса; unlink отключает доступ.
- По `X-Request-Id` можно найти полную цепочку: входящий запрос → MQTT command ID → ack/state → ответ/notification.

## 7. Что сознательно отложено / гипотезы

| Тема | Статус и решение |
|---|---|
| Эффекты | **Отложить.** Если появятся реальные эффекты, добавлять `devices.capabilities.color_setting`: описание использует `parameters.color_scene.scenes`, а состояние/action — `instance:"scene"` с одним из 16 фиксированных IDs Яндекса. Нельзя объявлять произвольные названия пресетов. |
| «Вся фара» как пятое устройство | **Гипотеза.** Технически возможное виртуальное `light`, но оно создаст конфликт состояния с 4 каналами. Лучший первый вариант — Яндекс-сценарий/группа из четырёх каналов; добавлять агрегат только с правилами приоритета и синхронизацией. |
| Однопользовательский OAuth | **Допустимое ограничение MVP, не упрощение протокола.** По-прежнему нужны authorization code, refresh, проверка state/client/redirect и отзыв токенов. |
| Сертификация/публичный каталог | **Вне MVP.** Понадобятся SKU на Маркете, публичный навык, тестируемое автономно подключаемое устройство, `device_info` и полный поток уведомлений. |
| Открытие MQTT порта | **Риск, который надо отдельно проверить.** Адресный firewall и TLS уменьшают риск, но домашний ESP32 может иметь динамический IP. Альтернатива — VPN/туннель или брокер с mTLS; не решать это случайным открытием 8883 всему интернету. |

## 8. Поправки к исходному плану

1. Убрать формулировку «все REST требуют Bearer»: `HEAD /v1.0` — исключение.
2. Зафиксировать `HEAD /v1.0` (без завершающего `/`) и остальные пути как в таблице; не оставлять это открытым вопросом.
3. Добавить notification service и `reportable:true` в архитектуру, иначе план не готов к реальным внешним изменениям и сертификации.
4. Не называть сцены `color_setting/scene` в discovery: правильное описание — `color_setting` + `parameters.color_scene`; `scene` — instance текущего состояния/action.
5. Заменить «action → MQTT → сразу успех» на action → MQTT → подтверждённый state/ошибка в пределах 3 с.
6. Не использовать scopes `iot:view`/`iot:control` в OAuth provider навыка: это scopes другого, пользовательского API Яндекса.
7. Добавить тестирование одновременных команд группе: документация отдельно предупреждает, что такие команды могут приходить параллельно.

## Официальные источники

- [Порядок действий / quick start](https://yandex.ru/dev/dialogs/smart-home/doc/ru/concepts/quick-start)
- [REST-протокол provider API](https://yandex.ru/dev/dialogs/smart-home/doc/ru/reference/resources)
- [Discovery: информация об устройствах](https://yandex.ru/dev/dialogs/smart-home/doc/ru/reference/get-devices)
- [Query: состояния устройств](https://yandex.ru/dev/dialogs/smart-home/doc/ru/reference/post-devices-query)
- [Action: изменение состояния](https://yandex.ru/dev/dialogs/smart-home/doc/ru/reference/post-action)
- [OAuth account linking](https://yandex.ru/dev/dialogs/smart-home/doc/ru/auth/how-it-works)
- [Тип `devices.types.light`](https://yandex.ru/dev/dialogs/smart-home/doc/ru/concepts/device-type-light), [on/off](https://yandex.ru/dev/dialogs/smart-home/doc/ru/concepts/on_off), [brightness/range](https://yandex.ru/dev/dialogs/smart-home/doc/ru/concepts/range), [color scenes](https://yandex.ru/dev/dialogs/smart-home/doc/ru/concepts/color_setting)
- [Коды ответов и device errors](https://yandex.ru/dev/dialogs/smart-home/doc/ru/concepts/response-codes)
- [Уведомление об изменении состояния](https://yandex.ru/dev/dialogs/smart-home/doc/ru/reference-alerts/post-skill_id-callback-state)
- [Создание и настройки: HTTPS, лимиты](https://yandex.ru/dev/dialogs/smart-home/doc/ru/start), [тестирование](https://yandex.ru/dev/dialogs/smart-home/doc/ru/testing), [приватный доступ](https://yandex.ru/dev/dialogs/smart-home/doc/ru/access)
- [Модерация и будущая сертификация](https://yandex.ru/dev/dialogs/smart-home/doc/ru/publishing), [требования сертификации](https://yandex.ru/dev/dialogs/smart-home/doc/ru/certification)

