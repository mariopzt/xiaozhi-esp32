# RobotCabeza ESP32 INMP441

Placa personalizada para `ESP32-WROOM-32` con:

- `INMP441` como microfono I2S
- `MAX98357A` como salida I2S
- boton de arranque en `GPIO0`
- LED simple en `GPIO2`
- sin display

## Cableado

### INMP441

- `SCK` -> `GPIO32`
- `WS` -> `GPIO33`
- `SD` -> `GPIO34`
- `L/R` -> `GND`
- `VDD` -> `3V3`
- `GND` -> `GND`

### MAX98357A

- `BCLK` -> `GPIO26`
- `LRC` -> `GPIO27`
- `DIN` -> `GPIO25`
- `VIN` -> `5V`
- `GND` -> `GND`

### Controles

- boton BOOT -> `GPIO0`
- LED simple -> `GPIO2`

## Compilacion

Desde la raiz del repo oficial:

```bash
python scripts/release.py robotcabeza-esp32-inmp441
```

O en modo manual:

```bash
idf.py set-target esp32
idf.py menuconfig
idf.py build
```

En `menuconfig`, la placa aparece como `RobotCabeza ESP32 + INMP441`.
