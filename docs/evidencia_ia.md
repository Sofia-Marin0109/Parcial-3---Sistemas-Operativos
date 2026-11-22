# Evidencia del uso de IA

## Consultas estructuradas utilizadas

Las cuatro consultas se ejecutaron durante el diseño, en la etapa 3. El asistente formuló las consultas y las usó para recuperar fuentes. Los textos entre comillas reproducen las consultas históricas completas. La evidencia de esta etapa combina los resultados recuperados, las comprobaciones sobre fuentes primarias y los registros posteriores del proyecto.

### Consulta 1. Arquitectura de mediación

> Contexto: parcial de mmap sobre archivos regulares en Linux v6.12 x86-64. Rol: revisor de arquitectura del kernel. Restricción: usa código fuente y documentación primarios de v6.12. Resultado: contrasta BPF LSM, trazado e intercepción de la tabla de llamadas y localiza mmap_file y file_mprotect.

**Resultado recuperado.** La búsqueda localizó documentación de BPF LSM y ejemplos de conexión a `file_mprotect`. También devolvió páginas de otras versiones.

**Comprobación y decisión.** Se revisaron los ganchos y sus argumentos en el tag v6.12. El despacho nativo x86-64 usa `x64_sys_call()`. El código conserva `sys_call_table` para trazado. La política incorporó un gancho para crear el mapeo y otro para añadir escritura después.

**Fuentes.** [Definiciones de ganchos v6.12](https://github.com/torvalds/linux/blob/v6.12/include/linux/lsm_hook_defs.h#L196) · [Despacho x86-64 v6.12](https://github.com/torvalds/linux/blob/v6.12/arch/x86/entry/syscall_64.c) · [BPF LSM v6.12](https://docs.kernel.org/6.12/bpf/prog_lsm.html)

### Consulta 2. Interfaces C, CO-RE e identidad

> Contexto: política BPF LSM por archivo y usuario en Linux v6.12. Rol: revisor de interfaces C y CO-RE. Restricción: fundamenta tipos, banderas e identidad en lsm_hook_defs.h, mm.h, mm/mmap.c y kernel/bpf/helpers.c. Resultado: documenta la validación de ABI, permisos e identidad y los requisitos de compilación y carga.

**Resultado recuperado.** La búsqueda localizó documentación que describe BTF y los ganchos BPF LSM. Las banderas y la interpretación de la identidad se verificaron en el código fuente.

**Comprobación y decisión.** `VM_MAYSHARE` identifica el carácter compartido de la VMA. Los bits inferiores de `bpf_get_current_uid_gid()` contienen el UID real traducido en el espacio de usuarios inicial. La política usa ese UID y compara dispositivo e inodo del archivo registrado. Se cotejaron las constantes con encabezados de Debian y se generó `vmlinux.h` con el BTF del kernel de la VM.

**Fuentes.** [Banderas de VMA](https://github.com/torvalds/linux/blob/v6.12/include/linux/mm.h#L275) · [Selección del tipo de mapeo](https://github.com/torvalds/linux/blob/v6.12/mm/mmap.c#L380) · [Helper de UID/GID](https://github.com/torvalds/linux/blob/v6.12/kernel/bpf/helpers.c#L238) · [Compilación registrada](../evidencias/compilacion_vm.log)

### Consulta 3. Pruebas defensivas e integridad

> Contexto: comprobación defensiva de permisos de archivos de prueba con mmap en Linux v6.12. Rol: responsable de pruebas. Restricción: usa solo archivos temporales propios, interfaces documentadas y comprobaciones benignas. Resultado: deriva resultados esperados para MAP_SHARED, MAP_PRIVATE, mprotect, concurrencia e integridad.

**Resultado recuperado.** La búsqueda asistida se orientó a las interfaces de mapeo y sus permisos. Los resultados esperados se derivaron de las rutas de `mmap()` y `mprotect()` y del manual de Linux.

**Comprobación y decisión.** Se incorporaron controles con dos UID, archivos temporales, un descriptor de solo lectura y una vista privada. El script observó `EPERM` de la política y `EACCES` del control POSIX. La réplica volvió a comprobar 128 denegaciones concurrentes y registró los SHA-256 antes y después.

**Fuentes.** [mmap(2)](https://man7.org/linux/man-pages/man2/mmap.2.html) · [Mediación de mprotect v6.12](https://github.com/torvalds/linux/blob/v6.12/mm/mprotect.c#L824) · [Salida de pruebas](../evidencias/smoke_vm.log) · [Traza del script](../evidencias/smoke_traza.log)

### Consulta 4. Medición comparativa

> Contexto: medición comparativa de una política BPF LSM sobre mmap en Linux v6.12. Rol: revisor de metodología experimental. Restricción: separa la latencia de la syscall del primer acceso y usa referencia del kernel y manuales Linux. Resultado: indica relojes, métricas de page faults y controles para comparar el coste de seguridad y auditoría.

**Resultado recuperado.** La consulta se usó para orientar la búsqueda. Las referencias de temporización y contadores se completaron con una búsqueda adicional de los manuales de Linux.

**Comprobación y decisión.** El benchmark usa `CLOCK_MONOTONIC_RAW` alrededor de `mmap()`, mide los primeros accesos en otro intervalo y obtiene los contadores con `getrusage()`. Una comprobación con `perf stat` contó fallos del proceso completo. Se midieron tres condiciones y se guardaron por separado el CSV original y la réplica.

**Fuentes.** [clock_gettime(2)](https://man7.org/linux/man-pages/man2/clock_gettime.2.html) · [getrusage(2)](https://man7.org/linux/man-pages/man2/getrusage.2.html) · [perf stat](https://man7.org/linux/man-pages/man1/perf-stat.1.html) · [Tablas regeneradas](../evidencias/tablas_regeneradas.md)

## Trazabilidad de correcciones

| Origen | Hallazgo | Comprobación y cambio |
| --- | --- | --- |
| Primera propuesta del asistente | Solo cubría el permiso de escritura al crear el mapeo. | El código de `mprotect()` confirmó otro punto de mediación. Se añadió `file_mprotect` y se probó la solicitud posterior. |
| Enunciado | Denomina `file_mmap` al gancho. | `lsm_hook_defs.h` de v6.12 define `mmap_file`. El código y los diagramas usan el nombre de esa versión. |
| Resultados de búsqueda | Se recuperó documentación de v6.14 y v6.15 para una solución fijada en v6.12. | Las firmas y el comportamiento se cotejaron con el tag y la documentación v6.12. |
| Alternativa contemplada en el enunciado | Se consideró interceptar la tabla de llamadas. | La ruta nativa x86-64 usa `x64_sys_call()`. Se seleccionó BPF LSM y se documentó la diferencia con el trazado. |
| Primer intento de compilación | La variable global `config` colisionó con un tipo del encabezado generado desde BTF. | El diagnóstico de Clang llevó a cambiarla por `guard_settings`. El registro de la VM confirma que la compilación estricta pasó con el cambio. |
| Primer intento de compilación | Clang marcó como no usado el parámetro `ctx` que introduce `BPF_PROG`. | Se hizo explícito su no uso y se conservó `-Werror`. La compilación de la VM y la salida del verificador respaldan la corrección. |

Los registros distinguen una omisión de la propuesta inicial, discrepancias del enunciado, diferencias de versión y fallos de compilación. Las fuentes conservadas no acreditan que se hayan generado funciones inexistentes o símbolos no exportados en esta implementación. `Module.symvers` y `EXPORT_SYMBOL` se estudiaron como parte de la comparación con un LKM externo.

La IA asistió en la investigación, el código en C y los casos de prueba. El usuario aprobó las etapas del trabajo. Las comprobaciones de fuentes, compilación, carga y pruebas descritas fueron ejecutadas durante el trabajo asistido. Los resultados declarados se apoyan en esos registros.

**Evidencia.** [Compilación](../evidencias/compilacion_vm.log) · [Verificador](../evidencias/verificador_bpf.log) · [Enlaces activos](../evidencias/carga_bpf.log) · [Verificación de la réplica](../evidencias/resumen_reproduccion.md)
