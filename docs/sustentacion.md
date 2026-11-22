# Guion de sustentación

**Seguridad de archivos mapeados con mmap en Linux**

Duración prevista de 5 a 6 minutos.

| Tiempo | Qué mostrar | Qué explicar |
| --- | --- | --- |
| 0:00-0:45 | Pregunta y propiedad demostrada | Un proceso que obtiene un mapeo compartido escribible puede modificar el archivo. La política decide quién puede solicitar ese permiso para el inodo registrado. |
| 0:45-1:45 | Diagrama del informe | `mmap_file` evalúa la creación del mapeo y `file_mprotect` evalúa cambios de protección. La regla identifica el archivo por dispositivo e inodo, y al solicitante por su UID real. |
| 1:45-3:15 | Prueba funcional y de alcance | Con la política activa, el UID 61002 recibe `EPERM` al pedir un mapeo compartido escribible. El UID 61001 autorizado sí puede escribir. La prueba concurrente verifica 128 denegaciones y compara el SHA-256. La prueba de alcance muestra qué sucede con `write()` y un mapeo creado antes de activar la política. |
| 3:15-4:30 | Tabla de rendimiento | En el archivo protegido, el p50 original de `mmap()` fue 7,47 µs en la base, 8,49 µs con política y 11,99 µs con auditoría. Se observaron 2.560 fallos menores por serie en las tres condiciones. Se midió en TCG con caché caliente. |
| 4:30-5:30 | Evidencia de diseño, compilación y alcance | Mostrar las consultas íntegras del anexo, las correcciones de compilación y los dos enlaces BPF activos. Cerrar con la versión probada, la distinción entre solicitudes nuevas y estados previos, y las vías de E/S que requieren otra política. |

## Demostración

Ejecutar en la VM Debian con el proyecto compilado y sobre un volumen ext4 de pruebas.

```bash
uname -r
cat /sys/kernel/security/lsm
sudo env STRACE_CHECK=1 bash tests/smoke.sh
sudo bash tests/scope.sh
python3 benchmarks/analizar.py benchmarks/resultados_vm_6_12.csv
```

La primera prueba termina con `SMOKE PASS`. Su salida muestra `EPERM`, los 128 rechazos concurrentes y `audit_dropped=0`. La segunda termina con `SCOPE PASS` y muestra los valores del byte testigo. Si la VM no está disponible durante la exposición, usar los registros de la ejecución previa. [Prueba funcional](../evidencias/smoke_vm.log) · [Prueba de alcance](../evidencias/alcance_vm.log) · [Carga y enlaces](../evidencias/carga_bpf.log)

## Diez respuestas para repasar

| Pregunta | Explicación oral |
| --- | --- |
| 1. Primer acceso | `mmap()` establece la VMA. Al acceder por primera vez, si falta la traducción, el kernel resuelve el fallo, instala la PTE y la CPU reintenta la instrucción. |
| 2. Compartido y privado | Un mapeo compartido puede reflejar cambios en el archivo. En una vista privada, Linux separa las escrituras mediante copia al escribir. |
| 3. Copy-on-write | La VMA puede permitir escritura aunque la PTE actual carezca de ella. El fallo de protección permite que el kernel prepare la copia privada. |
| 4. Punto de control | Los ganchos LSM reciben el objeto o la región que se va a controlar y pueden devolver un error. El trazado sirve para observar eventos. |
| 5. Cifrado y caché | El cifrado en disco y la autorización de memoria cubren momentos distintos. Con fscrypt, los datos descifrados pueden residir en la caché de páginas. |
| 6. Persistencia | Las páginas modificadas quedan sucias hasta que Linux las escribe. `msync()` sincroniza un rango mapeado y `fsync()` opera sobre el descriptor. |
| 7. Uso de eBPF | BPF LSM media las solicitudes. Los eventos describen el punto de decisión observado. Los accesos de CPU por PTE ya instaladas siguen la ruta de hardware. |
| 8. Descriptor de solo lectura | `O_RDONLY` admite un mapeo privado escribible porque la escritura recae en la copia privada. El mapeo compartido escribible requiere permiso de escritura. |
| 9. Concurrencia y COW | El kernel sincroniza las modificaciones de las PTE e invalida traducciones cuando toca. La política del proyecto controla solicitudes de mapeo y permisos. |
| 10. Jerarquía y aislamiento | `PGD → P4D → PUD → PMD → PTE` ubica la traducción. Las banderas de acceso, escritura y ejecución limitan lo que permite la MMU. |

## Objeciones frecuentes

- **¿Por qué BPF LSM?** Evalúa la solicitud en la interfaz común del kernel y permite carga dinámica. La disponibilidad del gancho y los privilegios del cargador son requisitos del entorno.
- **¿Cómo se atribuye el rechazo a la política?** Para el mismo UID y descriptor con escritura, el mapeo funciona antes y después de la política. Bajo la política se observa `EPERM`. El control POSIX con `O_RDONLY` da `EACCES`.
- **¿Qué ocurre con `write()` y con una VMA previa?** En la prueba se observaron dos escrituras permitidas. La política decide sobre solicitudes nuevas de mapeo compartido escribible y cambios de protección.
- **¿Qué significa comprobar el UID real?** La decisión usa el UID real traducido al espacio inicial de usuarios. Los casos de prueba usan cuentas ordinarias con UID real y efectivo iguales. Un descriptor heredado se evalúa en una solicitud de mapeo nueva. Un mapeo heredado es una VMA ya existente.
- **¿Se midió la velocidad del manejador de fallos?** Se registraron contadores y la duración del bucle de primeros accesos. El informe separa esas métricas de la latencia de `mmap()` y limita su interpretación al entorno medido.
- **¿En qué kernel se comprobó?** Debian `6.12.107+deb13-amd64`, x86-64, ext4, BPF LSM activo y BTF presente.
