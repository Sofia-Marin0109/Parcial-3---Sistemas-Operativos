# Seguridad de archivos mapeados con mmap en Linux

Universidad EAFIT · Escuela de Ciencias Aplicadas e Ingeniería
Departamento de Informática y Analítica · Sistemas Operativos · 2026-2

## Resumen

Se implementó una política BPF LSM que controla la escritura compartida sobre un archivo regular de ext4. La regla identifica el archivo por dispositivo e inodo y autoriza el mapeo compartido escribible de acuerdo con el UID real. El kernel comprueba la regla al crear el mapeo y cuando se solicita agregar escritura. La auditoría registra las decisiones observadas sobre el archivo protegido.

En una VM Debian con Linux `6.12.107+deb13-amd64`, las solicitudes del UID sin autorización devolvieron `EPERM`. En la prueba concurrente se bloquearon 128 solicitudes y el SHA-256 del archivo quedó intacto. En la carga medida, el p50 de `mmap()` subió de 7,47 µs a 8,49 µs con la política sin auditoría y a 11,99 µs con auditoría. El recuento de fallos de página de la fase medida se mantuvo en 2.560 menores y cero mayores por serie, con los datos calientes en la caché.

## Argumentación y trabajo futuro

### Por qué el control se aplica en el kernel

Una biblioteca en espacio de usuario puede observar las llamadas que pasan por ella. La política requiere cubrir también procesos que invocan directamente la interfaz del sistema. BPF LSM sitúa la decisión en los puntos de mediación comunes del kernel, donde el objeto `struct file` y la VMA ya se pueden examinar. Esto permite usar la misma identidad para decidir al establecer un mapeo y al cambiar su protección. Frente a un LSM compilado en el kernel, la solución se pudo cargar y desconectar en la VM con su kernel de distribución. Frente a la instrumentación por fallo de página, los ganchos elegidos tienen un alcance adecuado para esta regla de autorización y mantienen el trabajo de la política fuera del primer acceso a cada página.

**Referencias.** [Mediación de mmap en Linux v6.12](https://github.com/torvalds/linux/blob/v6.12/mm/util.c#L575) · [Mediación de mprotect en Linux v6.12](https://github.com/torvalds/linux/blob/v6.12/mm/mprotect.c#L824) · [BPF LSM](https://docs.kernel.org/6.12/bpf/prog_lsm.html)

### Límites de seguridad y de las mediciones

La política decide sobre nuevos mapeos y cambios de protección. Los mapeos escribibles existentes o heredados conservan sus permisos mientras sigan vigentes. Operaciones como `write()` o `truncate()` siguen gobernadas por los permisos y las demás políticas del sistema. La prueba de alcance comprobó dos casos con la política activa en ext4: una escritura directa con `write()` y una escritura mediante un mapeo previo. La auditoría registra las decisiones del gancho. Contar las escrituras de CPU o seguir cada llamada hasta su resultado final requiere instrumentación adicional. La prueba de concurrencia verifica las solicitudes ensayadas mediante el SHA-256 de un archivo temporal.

La decisión usa el UID real en el espacio inicial de usuarios. Las pruebas emplearon procesos con UID real y efectivo iguales. Si esas credenciales difieren, la identidad seleccionada por la regla sigue siendo el UID real. Una solicitud nueva se evalúa aunque el descriptor se haya heredado. Un mapeo heredado puede seguir escribible si ya existía. La protección sigue el dispositivo y el inodo registrados ante un cambio de nombre. Un archivo creado en sustitución del original tiene otra identidad y requiere registrarse por separado. [Prueba de alcance](resultados_etapa5.md) · [Registro de la VM](../evidencias/alcance_vm.log)

Las latencias describen el huésped emulado, su planificador y los archivos calientes en caché. Una evaluación en hardware físico y con distintos estados de caché permitiría comprobar cuánto se mantienen las diferencias observadas y estudiar los fallos mayores. La validación funcional se realizó en la versión de kernel indicada. Cualquier afirmación de compatibilidad adicional debe comprobarse en su entorno.

### Mitigaciones adicionales

Una política de integridad de archivos que abarque todas las vías de modificación debería extender el control a otros puntos de autorización del sistema de archivos. Para aislar cargas entre máquinas virtuales, el hipervisor puede usar paginación de segunda etapa, como Intel EPT o AMD NPT, para controlar la memoria física del huésped. Esa traducción opera sobre páginas, mientras que la asociación entre UID, descriptor e inodo pertenece al sistema operativo invitado. Una política combinada debería conservar la mediación dentro del huésped y reservar el hipervisor para la frontera entre máquinas virtuales. Si se quiere vigilar escrituras por página desde el hipervisor, habría que evaluar por separado cómo identificar las páginas correspondientes y cuánto cuesta atender las salidas de la VM.

**Referencia.** [MMU de KVM en x86 y traducción de segunda etapa](https://docs.kernel.org/6.12/virt/kvm/x86/mmu.html)

## Correspondencia con la rúbrica

| Rubro | Peso | Evidencia |
| --- | ---: | --- |
| Preguntas diagnósticas | 30 % | Diez respuestas con referencias a Linux v6.12, estructuras internas y rutas verificadas en el código fuente. |
| Diseño y metodología con IA | 30 % | Flujo de trabajo, cuatro consultas íntegras, revisión crítica, diagrama, matriz de alternativas y registros de validación. |
| Implementación y argumentación | 40 % | Código C y Makefile, demostración, prueba concurrente y prueba de alcance en una VM, trazado con strace, contadores con perf, resultados CSV y discusión del alcance. |

La evidencia para el apartado de PoC es una demostración defensiva sobre archivos temporales. Su aceptación como evidencia de ese apartado corresponde al docente. El código, los scripts y los datos de medición acompañan el informe. El [anexo de IA](evidencia_ia.md) conserva las cuatro consultas íntegras. La [evidencia de reproducción](../evidencias/resumen_reproduccion.md) documenta compilación, carga, pruebas y mediciones.
