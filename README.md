# Seguridad de archivos mapeados con mmap en Linux

Proyecto del parcial de Sistemas Operativos de la Universidad EAFIT (2026-2).
El objetivo es diseñar y verificar una defensa en el kernel de Linux para
controlar el acceso a archivos mediante `mmap()`.

## Estructura

| Ruta | Contenido previsto |
| --- | --- |
| `docs/` | Respuestas diagnósticas, diseño y evidencia de validación. |
| `src/` | Política eBPF en C, cargador libbpf y tipos compartidos. |
| `tests/` | Pruebas de permisos, integridad, auditoría y concurrencia. |
| `benchmarks/` | Carga de prueba, mediciones y análisis con y sin la defensa. |

## Documentación

[Informe final en PDF](docs/informe_mmap_entrega.pdf)

La revisión del enunciado, del laboratorio de referencia y del entorno está
completa. Las [diez respuestas diagnósticas](docs/respuestas_diagnosticas_mmap.md)
siguen el código fuente de Linux v6.12 en x86-64. El
[diseño de la defensa](docs/diseno_defensa_mmap.md) documenta la política BPF
LSM, el uso de IA y el plan de pruebas. La
[validación de la implementación](docs/validacion_implementacion.md) recoge
la compilación y la demostración en una VM Linux 6.12 sobre ext4. Los
[resultados de validación y rendimiento](docs/resultados_etapa5.md) incluyen
la prueba concurrente y la comparación con y sin auditoría. El
[resumen, la argumentación y la rúbrica](docs/informe_final.md) integran las
conclusiones. El [anexo de IA](docs/evidencia_ia.md) conserva las consultas
completas y la [evidencia técnica](evidencias/resumen_reproduccion.md) enlaza
los registros de la réplica. El [guion de sustentación](docs/sustentacion.md) incluye la
demostración y las preguntas de repaso.

## Compilar y ejecutar

El entorno objetivo es Debian 13 x86-64 con Linux 6.12, BPF LSM activo y BTF
disponible en `/sys/kernel/btf/vmlinux`. La VM debe arrancar con `bpf` en su
lista de LSM activos. Las comprobaciones y la compilación se pueden ejecutar
con una cuenta ordinaria. El cargador y la demostración requieren privilegios
para conectar los ganchos y ejecutar los accesos con dos UID distintos.

```bash
uname -r
cat /sys/kernel/security/lsm
test -r /sys/kernel/btf/vmlinux
sudo apt install build-essential clang bpftool libbpf-dev pkg-config
make clean
make
sudo bash tests/smoke.sh
sudo bash tests/scope.sh
```

La demostración crea un archivo temporal propio, ejecuta un caso de control
con la política desconectada y verifica el rechazo de `mmap()` y `mprotect()`
para un UID distinto. A continuación, verifica el acceso permitido y muestra
los eventos de auditoría y comprueba la integridad del archivo tras 128
solicitudes concurrentes. Para usar ext4, ejecutar con `TMPDIR` apuntando a un
directorio de ese sistema de archivos. Los UID de prueba se pueden elegir con
`ALLOWED_UID` y `DENIED_UID`. La prueba de alcance comprueba la denegación
de un mapeo nuevo y registra el comportamiento de `write()` y del mapeo previo
con la política activa.

El uso directo es `sudo build/mmap_guard ARCHIVO UID_AUTORIZADO`. El cargador
mantiene el archivo registrado mientras se ejecuta y anuncia `READY` tras
activar ambos ganchos. Ctrl+C desactiva la política y desconecta los ganchos.
La opción `--no-audit` deshabilita la emisión de eventos.

## Medir rendimiento

```bash
sudo bash benchmarks/run.sh > mediciones.csv
python3 benchmarks/analizar.py mediciones.csv
```

La medición usa tres condiciones a lo largo de seis rondas.
Para reproducir las comprobaciones instrumentadas, instala `linux-perf` y
`strace` en la VM. Los contadores de `perf stat` salen por la salida de error.

```bash
sudo env STRACE_CHECK=1 bash tests/smoke.sh
sudo env PERF_CHECK=1 bash benchmarks/run.sh > mediciones.csv
```

## Referencia del curso

[SO2026B — USOS/CapaKernel (revisión 63c4cca)](https://github.com/evalenciEAFIT/SO2026B/tree/63c4cca30fd5de9480a1e2a654042834d1e936c0/USOS/CapaKernel)
