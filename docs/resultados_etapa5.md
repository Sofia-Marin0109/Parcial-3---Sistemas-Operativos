# Validación y rendimiento de la defensa mmap

**Etapa 5 · Resultados observados en la VM**

Se usó Linux Debian `6.12.107+deb13-amd64` con BPF LSM activo, sobre ext4 en una VM QEMU x86-64 con TCG y dos CPU virtuales. El proyecto, incluido el programa de medición, compiló desde cero con `-Wall -Wextra -Werror`. La VM ejecutó las pruebas y las mediciones en la misma sesión y terminó con estado 0.

## Validación funcional

El archivo protegido y el de control fueron archivos temporales propios. Se usaron dos UID ordinarios, `61001` autorizado y `61002` sin autorización de la política. Ambos tenían permisos POSIX de escritura. El script comprueba explícitamente los códigos de error y el contenido de los archivos.

| Caso | Resultado observado |
| --- | --- |
| UID 61002, política desconectada, mapeo compartido escribible | Éxito. El primer byte cambió de `A` a `B`. |
| UID 61002, política activa, `MAP_SHARED` o `MAP_SHARED_VALIDATE` escribible | Ambos devolvieron `EPERM`. |
| UID 61002, política activa, mapeo compartido de solo lectura seguido de `mprotect()` con escritura | El mapeo inicial tuvo éxito y el cambio devolvió `EPERM`. `strace` observó ese rechazo de la llamada al sistema. |
| UID 61002, mapeo privado escribible del archivo protegido | Éxito. La escritura se hizo en la vista privada y el archivo conservó su primer byte. |
| UID 61002, mapeo compartido escribible del archivo de control | Éxito. El primer byte del archivo de control cambió a `B`. |
| UID 61001, descriptor `O_RDONLY`, mapeo compartido escribible | `EACCES` por el permiso del descriptor. El gancho dejó continuar la evaluación de Linux. |
| UID 61002, ocho trabajadores con 16 solicitudes cada uno | **128 denegaciones**. El SHA-256 del archivo protegido coincidió antes y después. |
| UID 61001, política activa, mapeo compartido escribible | Éxito. Se verificó la escritura tras `msync()`. |
| UID 61002, después de desconectar la política | El mapeo compartido escribible volvió a tener éxito. |

El script comprobó los 130 eventos `mmap` denegados del UID 61002, que corresponden a 128 solicitudes concurrentes y dos casos individuales de mapeo. La prueba de `mprotect()` se comprobó por separado. Se obtuvo `audit_dropped=0` y `SMOKE PASS`. La prueba concurrente demuestra la integridad del archivo para las solicitudes y la carga ensayadas.

## Prueba del alcance de la política

Una prueba adicional comprobó tres operaciones con el UID 61002, bajo la misma política activa y sobre el mismo archivo temporal de ext4. El descriptor tenía permiso POSIX de escritura.

| Operación | Esperado | Observado |
| --- | --- | --- |
| Solicitud nueva de mapeo compartido escribible | `EPERM` | `EPERM`. El byte testigo siguió en `A` y la auditoría registró la denegación. |
| Escritura mediante `write()` | La ruta ordinaria permite escribir | Éxito. El byte cambió a `D`. |
| Escritura con un mapeo creado antes de activar la política | La VMA conserva la escritura | Éxito. Después de `msync()` el byte fue `E`. |

La prueba comprobó un evento de auditoría, cero pérdidas y la salida `SCOPE PASS`. Los resultados delimitan la propiedad demostrada: el control media la creación de mapeos y los cambios de protección. [Registro de la VM](../evidencias/alcance_vm.log) · [Script de prueba](../tests/scope.sh)

## Método de medición

- Se compararon los ganchos **desconectados**, la **política activa sin auditoría** y la **política activa con auditoría**. Cada condición se aplicó al mismo proceso y patrón de acceso, sobre un archivo protegido y uno de control de 4 MiB. El proceso de la medición tenía el UID autorizado.
- Se ejecutaron seis rondas, una por cada permutación de las tres condiciones. Se alternó el orden de los archivos. Por ronda, archivo y condición se midieron 2.000 llamadas a `mmap()` después de 200 repeticiones de calentamiento. El cronómetro `CLOCK_MONOTONIC_RAW` rodea solo la llamada y sus lecturas del reloj. Se excluyen la apertura, el desmapeo y el primer acceso.
- En una fase separada se tocaron por primera vez las 1.024 páginas de 4 KiB de cada mapeo, durante 40 mapeos nuevos, tras dos mapeos de calentamiento. Los archivos estaban calientes en la caché de páginas. Se sumó el tiempo de los bucles de acceso y se tomaron los contadores de fallos del proceso con `getrusage()` antes y después de la fase.
- La tabla usa la **mediana de los seis estadísticos por ronda**. La columna de cambio compara el p50 de cada variante con el p50 de la base del mismo archivo. La tasa observada se obtuvo dividiendo los fallos de cada ronda por los segundos acumulados de los primeros toques. El intervalo medido incluye el coste del bucle y de acceder a memoria.

**Fuentes de las interfaces.** [clock_gettime(2)](https://man7.org/linux/man-pages/man2/clock_gettime.2.html) · [getrusage(2)](https://man7.org/linux/man-pages/man2/getrusage.2.html) · [perf stat(1)](https://man7.org/linux/man-pages/man1/perf-stat.1.html)

## Resultados

| Archivo | Condición | p50 mmap (ns) | p95 mmap (ns) | Cambio p50 | Primeros toques (ms) | Menores / mayores | Fallos/s |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Protegido | Desconectada | 7466,2 | 20416,0 | Base | 38,97 | 2560 / 0 | 65743 |
| Protegido | Política sin auditoría | 8492,2 | 21072,5 | +13,7 % | 41,17 | 2560 / 0 | 62194 |
| Protegido | Política con auditoría | 11994,8 | 51357,5 | +60,7 % | 43,13 | 2560 / 0 | 59359 |
| Control | Desconectada | 7430,5 | 19174,5 | Base | 41,04 | 2560 / 0 | 62392 |
| Control | Política sin auditoría | 8234,8 | 21535,5 | +10,8 % | 38,85 | 2560 / 0 | 65896 |
| Control | Política con auditoría | 8366,8 | 21422,0 | +12,6 % | 40,06 | 2560 / 0 | 63930 |

- **Latencia.** Para el archivo protegido, el p50 subió unos 1,03 µs con la política sin auditoría y unos 4,53 µs con auditoría, ambos frente a la base. En el archivo de control también hubo un incremento, compatible con el coste de entrar al gancho y descartar la operación después de consultar la identidad. Las medianas por ronda del archivo protegido con auditoría estuvieron entre 11,01 y 28,58 µs. El coste incluye la emisión y el consumo de eventos bajo la planificación de la VM. No se registraron pérdidas de auditoría en estas rondas.
- **Fallos de página.** Las 36 ejecuciones registraron 2.560 fallos menores y cero mayores en la fase de primer acceso. El recuento equivale a 64 fallos menores por mapeo de 1.024 páginas, consistente con la instalación anticipada de traducciones cercanas (*fault-around*) en regiones respaldadas por la caché. La tasa varió con el tiempo del bucle. Estos datos no aíslan la latencia interna de cada invocación del manejador.
- **Contraste con perf.** Dos corridas auxiliares con `perf stat` sobre el archivo protegido contaron 2.758 fallos menores con los ganchos desconectados y 2.759 con la política sin auditoría, con cero mayores en ambas. Esos contadores abarcan todo el proceso, incluida la preparación de la carga, y por eso superan los contadores de la fase medida con `getrusage()`.

**Referencia para fault-around.** [mm/memory.c, Linux v6.12](https://github.com/torvalds/linux/blob/v6.12/mm/memory.c#L5171)

## Reproducción y alcance

```bash
make clean
make
sudo env STRACE_CHECK=1 bash tests/smoke.sh
sudo env PERF_CHECK=1 bash benchmarks/run.sh > mediciones.csv
python3 benchmarks/analizar.py mediciones.csv
```

Las verificaciones instrumentadas requieren `strace` y `perf` dentro de la VM. Se guardaron las 36 filas medidas en [resultados_vm_6_12.csv](../benchmarks/resultados_vm_6_12.csv). Las mediciones describen una VM emulada y archivos calientes en la caché. Quedan por comprobar el rendimiento en hardware físico, los fallos mayores con caché fría y la compatibilidad con otras versiones del kernel. La cobertura de la política se limita a las solicitudes de mapeo y cambios de protección descritos en el diseño. El resultado concurrente se refiere a archivos temporales y a la carga especificada.

## Réplica de verificación

Se conservaron el [CSV original](../benchmarks/resultados_vm_6_12.csv) y sus cifras. La réplica usó otra sesión de la VM con el mismo kernel y ext4. Volvió a observar 128 denegaciones concurrentes y el mismo SHA-256 antes y después de esas solicitudes.

La [réplica de 36 series](../evidencias/rendimiento_replica_6_12.csv) registró un p50 de 8,03 µs para el archivo protegido con ganchos desconectados, 9,25 µs con política sin auditoría y 15,80 µs con auditoría. En ambas ejecuciones, las series de primeros accesos registraron 2.560 fallos menores y cero mayores. Los p50 de la réplica se presentan por separado para conservar la trazabilidad y mostrar la variabilidad entre ejecuciones en TCG. El recuento y la tasa de fallos describen la fase de acceso medida. La latencia interna del manejador no se aisló.

**Evidencia.** [Salida de las pruebas](../evidencias/smoke_vm.log) · [Contadores perf de la réplica](../evidencias/rendimiento_perf.log) · [Tablas regeneradas](../evidencias/tablas_regeneradas.md) · [Resumen de reproducibilidad](../evidencias/resumen_reproduccion.md)
