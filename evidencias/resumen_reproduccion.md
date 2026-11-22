# Evidencia técnica de la réplica

La réplica del **15 de noviembre de 2026** compiló y ejecutó el proyecto dentro de una VM QEMU sin red, con emulación TCG, dos CPU virtuales, Linux Debian `6.12.107+deb13-amd64` y un volumen ext4 dedicado. La compilación empleó el BTF del kernel en ejecución y libbpf 1.5.0. La salida de cada comprobación se separó a partir de la consola de la VM, con finales de línea normalizados.

| Registro | Qué permite comprobar |
| --- | --- |
| [entorno_vm.txt](entorno_vm.txt) y [estados_vm.txt](estados_vm.txt) | Kernel, configuración BPF, disponibilidad de BTF y versiones. Cada comprobación ejecutada y la salida de la VM finalizaron con estado 0. |
| [compilacion_vm.log](compilacion_vm.log) | `make clean` seguido de compilación con `-Wall -Wextra -Werror`. El objeto ELF contiene las secciones `lsm/mmap_file`, `lsm/file_mprotect`, `.BTF` y `.BTF.ext`. |
| [verificador_bpf.log](verificador_bpf.log) | Carga de diagnóstico con `bpftool -d prog loadall` y trazas del verificador para ambos programas. Se retiraron los objetos anclados tras la comprobación. |
| [carga_bpf.log](carga_bpf.log) | El cargador emitió `READY` y se observaron los dos enlaces LSM. Antes del inicio y después de `SIGTERM`, la lista de enlaces estaba vacía. |
| [fallo_conexion_bpf.log](fallo_conexion_bpf.log) | Fallo deliberado: `strace` inyectó `EPERM` en la segunda llamada `BPF_LINK_CREATE`. La primera conexión tuvo éxito. El cargador salió con estado 1 sin emitir `READY` y la lista de enlaces volvió a quedar vacía. El estado 0 del registro de control indica que se observaron los resultados esperados de esa prueba. |
| [smoke_vm.log](smoke_vm.log) y [smoke_traza.log](smoke_traza.log) | Salida completa del script y traza de ejecución separada. Las 128 solicitudes concurrentes fueron denegadas, el SHA-256 coincidió y se observaron 130 denegaciones `mmap` en total, contando los dos casos individuales. `strace` confirmó `EPERM` en `mprotect()`. El script obtuvo `audit_dropped=0` y `SMOKE PASS`. |
| [rendimiento_replica_6_12.csv](rendimiento_replica_6_12.csv), [rendimiento_perf.log](rendimiento_perf.log) y [tablas_regeneradas.md](tablas_regeneradas.md) | 36 filas nuevas de seis rondas, contadores auxiliares de `perf` y tablas generadas con el mismo analizador para la referencia y para la réplica. |

Una ejecución posterior de cierre repitió la compilación, la prueba funcional y el benchmark, con estado 0. También comprobó el resultado del [script de alcance](../tests/scope.sh) y obtuvo `SCOPE PASS`. Su [registro](alcance_vm.log) muestra el rechazo de un nuevo mapeo y la escritura permitida mediante `write()` y una VMA previa.

El SHA-256 que coincidió antes y después de las solicitudes concurrentes fue `9aab2ec00da30c5da4edea7c834d83c0d3bd094cb276a6b98693d9dfc3e8a524`.

## Origen de las mediciones

El [CSV original de la etapa 5](../benchmarks/resultados_vm_6_12.csv) se conservó byte por byte. Su SHA-256 es `7231fac996a0cfdbc1b4f778c079a17edd3a2386116e5dae6d2f9d232f3e353c`. Se volvió a ejecutar el analizador y las seis filas numéricas coincidieron con la tabla del PDF entregado.

Se recuperaron la [prueba de la etapa 4](smoke_etapa4_recuperado.log) y la [verificación posterior del ZIP](verificacion_entrega_recuperada.log). Se distinguen de la medición original de la etapa 5 y de la réplica del 15 de noviembre. El archivo de consola llamado `validation-vm.log` en el entorno coincidía con el registro de la verificación posterior del ZIP. Por ello, se conserva una sola copia de ese registro.

La réplica obtuvo para el archivo protegido p50 de 8,03 µs con ganchos desconectados, 9,25 µs con política sin auditoría y 15,80 µs con auditoría. Como en la medición original, cada serie de primeros accesos registró 2.560 fallos menores y cero mayores. Las diferencias de latencia entre ejecuciones son parte de la variabilidad observada en TCG. La tasa registrada corresponde al intervalo de la carga de prueba. Aislar el tiempo del manejador de fallos requeriría instrumentación adicional.

## Comandos de reproducción

Desde la raíz del repositorio en una VM Debian 13 con BPF LSM activo, BTF y un volumen ext4 de pruebas:

```bash
uname -a
cat /sys/kernel/security/lsm
test -r /sys/kernel/btf/vmlinux
make clean && make
readelf -SW build/mmap_guard.bpf.o | grep -E '(lsm/|BTF)'
sudo env STRACE_CHECK=1 bash tests/smoke.sh
sudo env PERF_CHECK=1 bash benchmarks/run.sh > rendimiento_replica.csv 2> rendimiento_perf.log
python3 benchmarks/analizar.py benchmarks/resultados_vm_6_12.csv
python3 benchmarks/analizar.py rendimiento_replica.csv
```

Para observar los enlaces se usó `bpftool link show` durante la ejecución del cargador y `bpftool -j link show` antes y después. Para obtener el registro del verificador se usó `bpftool -d prog loadall` sobre un directorio nuevo del montaje BPF en la VM. La prueba de limpieza ante fallo dedujo de una traza de arranque normal cuál era la posición de la segunda llamada de conexión. Esa posición depende del entorno y conviene determinarla de nuevo al reproducir la prueba.

La primera preparación de la VM encontró una diferencia del entorno mínimo: faltaba el alias `cc`. La compilación verificada eligió `CC=gcc` de forma explícita. En esta revisión se conservaron sin cambios el código de la política y las cifras de la medición original.
