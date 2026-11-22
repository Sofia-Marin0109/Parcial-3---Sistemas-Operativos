# Evidencia de implementación y compilación

**Etapa 4 · Compilación limpia y demostración de los ganchos**

## Entorno verificado

| Componente | Versión o configuración |
| --- | --- |
| VM | QEMU 10.0.13, x86-64, emulación TCG, 2 CPU virtuales, sin red |
| Kernel | Debian `6.12.107+deb13-amd64` |
| Sistema de archivos de prueba | ext4 en un disco virtual dedicado |
| Configuración | `CONFIG_BPF_LSM=y`, `CONFIG_DEBUG_INFO_BTF=y` y `bpf` en la lista de LSM activos |
| Herramientas de compilación | GCC 14.2.0, Clang 19.1.7, bpftool 7.5.0 y libbpf 1.5.0 |

En la primera validación, el `Makefile` generó el objeto eBPF, el cargador y el programa de pruebas desde cero con `-Wall -Wextra -Werror`. La compilación usó los tipos BTF extraídos de la imagen del kernel que arrancó la VM. Se cotejaron las constantes UAPI y las banderas internas con los encabezados Debian de esa misma versión. Se comprobaron las secciones `lsm/mmap_file`, `lsm/file_mprotect`, `.BTF` y `.BTF.ext` en el objeto generado.

### Correcciones detectadas al compilar con BTF real

- El generador produjo un tipo llamado `config` que chocaba con el nombre inicial de la variable global del programa. Se cambió el nombre de la variable a `guard_settings`.
- La macro `BPF_PROG` introduce el parámetro implícito `ctx`. La configuración estricta de Clang lo marcó como no usado. Se indicó explícitamente en cada gancho y se mantuvo la compilación con advertencias tratadas como errores.

## Demostración en ext4

El script `tests/smoke.sh` creó su propio archivo temporal y usó los UID de prueba `61001` (autorizado) y `61002` (distinto del autorizado). El archivo podía abrirse para escritura conforme a los permisos ordinarios de Linux. Por eso, el mismo UID que luego recibió una denegación pudo escribir antes de activar la política y después de desconectarla.

| Estado de la política | Solicitud | UID | Resultado observado |
| --- | --- | --- | --- |
| Desconectada | Mapeo compartido escribible | 61002 | Éxito. El byte inicial cambió de `A` a `B`. |
| Activa | Mapeo compartido escribible | 61002 | `EPERM`. La prueba verificó que el byte inicial siguiera siendo `A`. |
| Activa | Mapeo compartido de solo lectura y solicitud de escritura mediante `mprotect()` | 61002 | Éxito inicial y luego `EPERM`. El byte siguió siendo `A`. |
| Activa | Mapeo compartido escribible | 61001 | Éxito. El byte inicial cambió a `B` después de sincronizarlo. |
| Desconectada de nuevo | Mapeo compartido escribible | 61002 | Éxito. Se restauró el comportamiento de control. |

El cargador anunció `READY` después de conectar ambos ganchos. La auditoría registró dos denegaciones y dos decisiones de continuar la evaluación de Linux. El contador fue `audit_dropped=0` durante la demostración. El script terminó con `SMOKE PASS` y la VM devolvió estado 0.

Esta comprobación cubre los dos ganchos, el estado del archivo de prueba y el ciclo básico de activación y desconexión en esa versión de Linux. La matriz ampliada y las mediciones figuran en los [resultados de la etapa 5](resultados_etapa5.md). La reproducción en Linux 5.x sigue sin verificarse.

## Réplica de compilación y carga

El 15 de noviembre de 2026 se repitió la compilación dentro de la VM con el BTF de su kernel en ejecución. El verificador aceptó los dos programas. El cargador anunció `READY`, se observaron ambos enlaces y, tras `SIGTERM`, se confirmó la lista vacía. En una prueba separada se simuló `EPERM` en la segunda conexión: la primera se había establecido, el cargador salió con estado 1 sin anunciar `READY` y el kernel quedó sin enlaces de la prueba.

**Evidencia.** [Entorno](../evidencias/entorno_vm.txt) · [Compilación](../evidencias/compilacion_vm.log) · [Verificador](../evidencias/verificador_bpf.log) · [Enlaces](../evidencias/carga_bpf.log) · [Fallo simulado](../evidencias/fallo_conexion_bpf.log)
