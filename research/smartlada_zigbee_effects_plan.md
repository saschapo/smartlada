# SmartLada — финальный план: Zigbee + 5-е устройство для эффектов

Дата решения: 2026-08-25. Яндекс на feature request не ответил → **откат на Zigbee**.
Облачный backend (`smarthome_backend/`) запаркован (не удаляем — задокументированный путь и
доказательство потолка платформы). Сервер откатываем до исходного состояния (см. ниже).

## Потолок платформы (подтверждено, не обходится)
Яндекс: `range/brightness` — одна на устройство; нет составных/многоканальных устройств
(device-types) и механизма под-каналов (capability-types). «Одна Фара с 4 диммерами внутри»
невозможна ни в Zigbee, ни в облаке. Достижимо: N отдельных устройств + группа/мастер + отдельное
устройство под эффекты. См. [`smartlada_alice_wifi_matter_vs_zigbee.md`](smartlada_alice_wifi_matter_vs_zigbee.md).

## Выбранная архитектура (Zigbee)

База — рабочий `sketches/RevCZigbee4EP` (4× DimmableLight EP10..13, вариант A подтверждён).
Добавляем **5-е устройство** — `ZigbeeColorDimmableLight` (EP14), «Фара»:

- `on/off` — питание всей фары;
- `brightness` (level) — **мастер**, масштабирует все 4 канала;
- `hue` (цвет, `onLightChangeHsv`) — **селектор эффекта** (таблица hue→effect);
- **Yandex-группу НЕ делаем** — мастер живёт на 5-м устройстве (иначе два мастера конфликтуют).

### Правило приоритета каналов (гвоздями)
```
master = level(EP14) / 254
если эффект активен (hue != белый/выкл):
    каналы = кадр_анимации[i] * master
иначе:
    каналы[i] = индивидуальная_лампа[i] * master
```
hue=белый/off → эффекта нет, лампы работают индивидуально (EP10..13 + мастер).

### Движок эффектов
Анимацию крутит ESP32 — портируем `fx::` из прошивки RevC (`SmartLadaRevC/src/fx`). Старт:
2-3 эффекта (напр. бегущий поворотник / chase / fade-breathe). Маппинг цвет→эффект уточнить.

## Открытые решения
- [ ] Стартовый набор эффектов (2-3) и грубый маппинг hue→effect.
- [ ] Скорость/яркость эффекта: level(EP14) как master, или отдельный параметр.

## Риски — проверять на стенде (эмпирика)
- **Гетерогенный узел:** 4× DimmableLight (0x0101) + 1× ColorDimmableLight (0x0102) на одном
  node — Станция может придираться к смешанным типам endpoint (T3 из чеклиста).
- Доходит ли hue в `onLightChangeHsv`; нужен `setLightColorCapabilities` (по умолч. XY).
- Порядок pairing/имена после rejoin.

## Следующий шаг
Расширить `RevCZigbee4EP`: 5-й Color-endpoint (master+hue) + правило приоритета + 2-3 эффекта из
`fx::`, сохранить raw-лог. Залить на стенд (RevA + nanoESP32-C6), проверить 5 устройств +
цвет→эффект + мастер. Результаты — в [`zigbee_alice_test_results.md`](zigbee_alice_test_results.md).

## Откат сервера до исходного (сделать)
Убрать: сабдомены smarthome/mqtt (DNS vdsina), systemd `smarthome` + `/opt/smarthome`, nginx-сайт
`smarthome` + certbot-cert, mosquitto (purge) + `/etc/mosquitto/smartlada` + conf.d + AppArmor
override + ufw 8883. НЕ трогать: портфолио `saschapo.me`, `video.saschapo.me` (cineink), PocketBase.
Команды — в сессии/этом плане; облачный код в репо остаётся.
