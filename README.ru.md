# esphome-ld2410-ble

*[Read in English](README.md)*

Внешний компонент ESPHome для датчика присутствия HiLink LD2410, который умеет общаться
с датчиком по встроенному **BLE**-радио, по проводному **UART**, либо **сразу по обоим**
каналам с автоматическим переключением между ними.

## Зачем

Штатный компонент ESPHome [`ld2410`](https://esphome.io/components/ld2410) поддерживает
только проводной UART. В некоторых установках UART-линии нет вовсе (или она нужна лишь
как резерв), а LD2410 при этом всё равно даёт доступ к тому же самому протоколу
конфигурации/данных через собственное BLE-радио — этот компонент говорит с датчиком по
BLE напрямую через `ble_client`, и опционально может параллельно говорить по UART,
используя тот канал, который сейчас жив.

## Возможности

- Бинарные сенсоры присутствия/движения, поканальные сенсоры энергии, сенсоры дистанции,
  текстовые сенсоры прошивки/MAC, переключатели engineering-mode и Bluetooth, числовые
  параметры порогов по воротам/таймаута, селекторы разрешения дистанции/управления
  подсветкой, кнопки factory reset/restart/query — тот же набор сущностей, что и у
  нативного `ld2410`.
- **Failover UART + BLE**: при указании и `uart_id`, и `ble_client_id` компонент держит
  оба канала открытыми одновременно. Показания сенсоров публикуются от того транспорта,
  откуда пришёл валидный кадр — поэтому presence-данные остаются непрерывными при обрыве
  любого из каналов. Исходящие команды уходят через UART, если по нему за последние 2
  секунды был валидный кадр, иначе — через BLE. Диагностическая текстовая сущность
  `active_transport` показывает, какой канал сейчас в приоритете.
- Работает и в режиме только BLE, и в режиме только UART — достаточно указать один из
  идентификаторов.

## Установка

```yaml
external_components:
  - source:
      type: local
      path: components  # либо источник на github — см. документацию ESPHome по external_components
    components: [ld2410_ble]
```

## Пример конфигурации (два транспорта)

```yaml
esp32_ble_tracker:

uart:
  - id: my_ld2410_uart
    tx_pin: GPIO17
    rx_pin: GPIO16
    baud_rate: 256000
    parity: NONE
    stop_bits: 1

ble_client:
  - mac_address: AA:BB:CC:DD:EE:FF
    id: my_ld2410_ble_client

ld2410_ble:
  - id: my_ld2410
    uart_id: my_ld2410_uart        # опционально
    ble_client_id: my_ld2410_ble_client  # опционально — нужен хотя бы один из двух
    password: "HiLink"             # пароль BLE-доступа, по умолчанию "HiLink"

binary_sensor:
  - platform: ld2410_ble
    ld2410_id: my_ld2410
    has_moving_target:
      name: Moving Target
    has_still_target:
      name: Still Target

text_sensor:
  - platform: ld2410_ble
    ld2410_id: my_ld2410
    active_transport:
      name: LD2410 Active Transport
```

Смотрите [`ld2410-ble-template.yaml`](ld2410-ble-template.yaml) — переиспользуемый
packages-шаблон только для BLE, и [`ld2410-uart-ble-template.yaml`](ld2410-uart-ble-template.yaml)
— вариант с двумя транспортами. Оба принимают подстановки
`place`/`mac_address`/`password`/`disabled` (а dual-transport ещё и `tx`/`rx`) и
открывают полный набор сущностей. [`ld2410-ble-test.yaml`](ld2410-ble-test.yaml) /
[`ld2410-uart-ble-test.yaml`](ld2410-uart-ble-test.yaml) — запускаемые примеры конфигов
устройств поверх этих шаблонов.

## Статус

Собирается без ошибок под ESP32-фреймворками `esp-idf` и `arduino`. BLE-режим уже
использовался на реальном железе; UART-транспорт и сам механизм failover UART/BLE пока
проверены только на уровне компиляции, но не на реальном устройстве (например, обрыв
UART-провода с проверкой плавного переключения на BLE ещё не тестировался).

## Благодарности

Протокол и структура сущностей портированы и смоделированы по образцу штатного
компонента ESPHome [`ld2410`](https://github.com/esphome/esphome/tree/dev/esphome/components/ld2410)
(лицензия Apache-2.0).
