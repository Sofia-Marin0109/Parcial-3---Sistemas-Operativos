# Tablas del analizador

## Medición original de la etapa 5

| Archivo | Condición | p50 mmap (ns) | p95 mmap (ns) | Cambio p50 | Primeros toques (ms) | Menores / mayores | Fallos/s |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| protected | baseline | 7466.2 | 20416.0 | +0.0 % | 38.97 | 2560 / 0 | 65743 |
| protected | policy | 8492.2 | 21072.5 | +13.7 % | 41.17 | 2560 / 0 | 62194 |
| protected | audit | 11994.8 | 51357.5 | +60.7 % | 43.13 | 2560 / 0 | 59359 |
| control | baseline | 7430.5 | 19174.5 | +0.0 % | 41.04 | 2560 / 0 | 62392 |
| control | policy | 8234.8 | 21535.5 | +10.8 % | 38.85 | 2560 / 0 | 65896 |
| control | audit | 8366.8 | 21422.0 | +12.6 % | 40.06 | 2560 / 0 | 63930 |

## Réplica de verificación

| Archivo | Condición | p50 mmap (ns) | p95 mmap (ns) | Cambio p50 | Primeros toques (ms) | Menores / mayores | Fallos/s |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| protected | baseline | 8032.0 | 18256.5 | +0.0 % | 40.05 | 2560 / 0 | 63921 |
| protected | policy | 9253.5 | 22193.0 | +15.2 % | 39.23 | 2560 / 0 | 65256 |
| protected | audit | 15803.2 | 70604.0 | +96.8 % | 40.73 | 2560 / 0 | 62886 |
| control | baseline | 8036.8 | 20434.5 | +0.0 % | 39.61 | 2560 / 0 | 64637 |
| control | policy | 8928.0 | 20810.5 | +11.1 % | 40.63 | 2560 / 0 | 63018 |
| control | audit | 8908.0 | 21797.0 | +10.8 % | 39.54 | 2560 / 0 | 64762 |
