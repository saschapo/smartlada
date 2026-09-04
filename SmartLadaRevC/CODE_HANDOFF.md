# SmartLada Rev C — Firmware Code Handoff (superseded)

> **Этот документ устарел (был предзигбишным). Актуальные документы:**
> - [`README.md`](README.md) — что это, архитектура, **pin-map (ground truth)**, модель управления, сборка/прошивка.
> - [`SESSION_HANDOFF.md`](SESSION_HANDOFF.md) — текущее состояние, открытые хвосты, следующий шаг.
> - [`DEV_PLAN.md`](DEV_PLAN.md) — план по фазам (Zigbee + WiFi/веб).

Прошивка контроллера ламп фонаря ВАЗ на **ESP32-C6-WROOM-1** (плата revC): тонкий `.ino` +
слои `src/*`, OLED-меню (4 кнопки, J4), 4 PWM-канала ламп, эффекты, **Zigbee** (5 эндпоинтов).
Форк от `../SmartLadaRevB`. Всё содержательное перенесено в файлы выше.
