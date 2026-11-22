# Seguridad de archivos mapeados con mmap en Linux

**Parte I · Diez preguntas diagnósticas**

Universidad EAFIT · Sistemas Operativos · 2026-2

El análisis sigue el código fuente de **Linux v6.12 en x86-64**. Para el flujo normal se toma como ejemplo un archivo regular de ext4, con caché de páginas y sin DAX, páginas gigantes ni precarga explícita. La única observación experimental de esta sección se hizo en Linux 6.18.57 y se identifica como tal.

## 1. Del mapeo virtual al primer fallo de página

1. **Entrada al kernel.** La llamada nativa `mmap()` entra por la interfaz de llamadas al sistema de x86-64. El envoltorio comprueba que el desplazamiento esté alineado a página y llama a `ksys_mmap_pgoff()` con el desplazamiento expresado en páginas. Para un archivo, esta función resuelve el descriptor y obtiene el `struct file`.
2. **Creación de la región virtual.** `vm_mmap_pgoff()` consulta `security_mmap_file()`. Si se autoriza la operación, toma el bloqueo de escritura de los mapeos del proceso y llama a `do_mmap()`. Esta función comprueba argumentos y permisos, escoge el intervalo virtual y delega en `mmap_region()`. El kernel crea o combina un área virtual (VMA) que describe el intervalo, el archivo, su desplazamiento y los permisos. En una VMA nueva de ext4, la operación del archivo configura `vm_ops`, que incluye el manejador de fallos. La llamada retorna la dirección virtual inicial. Con paginación por demanda, esa dirección ya es válida como parte de la VMA aunque todavía falten traducciones para sus páginas.
3. **Excepción de la CPU.** En el primer acceso a una página sin traducción presente, la MMU consulta la TLB y, si hace falta, recorre las tablas de páginas. La CPU genera `#PF` si la página está ausente o la traducción presente impide el acceso. En x86, `exc_page_fault()` recibe la dirección del fallo, normalmente de `CR2`, y el código de error. `do_user_addr_fault()` localiza y estabiliza la VMA, verifica los permisos y llama a `handle_mm_fault()`.
4. **Resolución en memoria virtual.** `handle_mm_fault()` llega a `handle_pte_fault()` en la ruta de páginas normales. Si falta la PTE de un archivo ordinario, `do_fault()` distingue lectura, escritura privada y escritura compartida.

   | Primer acceso | Ruta y resultado |
   | --- | --- |
   | Lectura | `do_read_fault()` puede instalar traducciones de páginas cercanas que ya están en caché mediante `map_pages`. Si necesita el manejador del archivo, ext4 utiliza `filemap_fault()` para obtener la página y, si corresponde, iniciar su lectura. |
   | Escritura privada | `do_cow_fault()` obtiene los datos del archivo, crea una página anónima para el mapeo privado e instala la traducción correspondiente. Si una lectura previa dejó instalada una PTE de solo lectura, la escritura posterior entra por `do_wp_page()`. |
   | Escritura compartida autorizada | `do_shared_fault()` obtiene la página del archivo y llama a `page_mkwrite` si el sistema de archivos proporciona esa operación. La traducción permite modificar la página compartida tras las comprobaciones correspondientes. |

5. **Retorno.** Una vez instalada o actualizada la traducción, la CPU reintenta la instrucción. Una dirección sin VMA o un acceso incompatible con los permisos puede dar `SIGSEGV`. Un acceso a una página completa situada más allá del final del archivo puede dar `SIGBUS`. Con `MAP_POPULATE`, el kernel puede preparar traducciones durante la llamada, por lo que el primer acceso de la aplicación podría encontrar ya instalada la suya.

**Fuentes.** [Entrada x86-64](https://github.com/torvalds/linux/blob/v6.12/arch/x86/kernel/sys_x86_64.c#L79) · [vm_mmap_pgoff](https://github.com/torvalds/linux/blob/v6.12/mm/util.c#L575) · [do_mmap](https://github.com/torvalds/linux/blob/v6.12/mm/mmap.c#L283) · [Manejador x86](https://github.com/torvalds/linux/blob/v6.12/arch/x86/mm/fault.c) · [Fallos de archivo](https://github.com/torvalds/linux/blob/v6.12/mm/memory.c#L5277) · [VM de ext4](https://github.com/torvalds/linux/blob/v6.12/fs/ext4/file.c#L776) · [mmap(2)](https://man7.org/linux/man-pages/man2/mmap.2.html)

## 2. MAP_SHARED y MAP_PRIVATE en la VMA

`struct vm_area_struct` describe un intervalo virtual en un `mm_struct`. Para un mapeo de archivo importan `vm_start`, `vm_end`, `vm_file`, `vm_pgoff`, `vm_flags`, `vm_page_prot` y `vm_ops`. Las banderas VMA describen la política de la región. Las entradas de las tablas de páginas materializan los permisos para cada traducción instalada.

| Aspecto | `MAP_SHARED` | `MAP_PRIVATE` |
| --- | --- | --- |
| Banderas internas | En la ruta ordinaria, `do_mmap()` activa `VM_SHARED` y `VM_MAYSHARE`. Para un descriptor sin escritura elimina `VM_MAYWRITE` y `VM_SHARED`, y conserva `VM_MAYSHARE`. | La rama de archivo privado carece de esas marcas de carácter compartido. Puede admitir escritura privada aunque el descriptor sea de solo lectura. |
| Visibilidad | Una escritura autorizada modifica la página del archivo que pueden observar otros mapeos compartidos del mismo contenido. Los procesos deben sincronizarse si necesitan consistencia en estructuras de varios campos. | Las lecturas iniciales pueden compartir páginas físicas de la caché. Al escribir se obtiene una página privada mediante COW. Los cambios propios quedan en el mapeo privado. |
| Persistencia | La escritura ensucia la caché y el sistema de archivos puede persistirla mediante *writeback*. La visibilidad en memoria y la durabilidad en almacenamiento ocurren en momentos distintos. | Las escrituras privadas quedan fuera del *writeback* del archivo. La interfaz deja sin especificar la visibilidad de cambios hechos al archivo después de crear el mapeo privado. |
| Seguridad | Importa controlar quién puede crear o adquirir un mapeo compartido escribible. | COW aísla las escrituras. El acceso de lectura sigue revelando al proceso autorizado los datos originales que mapeó. |

**Fuentes.** [vm_area_struct](https://github.com/torvalds/linux/blob/v6.12/include/linux/mm_types.h) · [Banderas y permisos en do_mmap](https://github.com/torvalds/linux/blob/v6.12/mm/mmap.c#L380) · [mmap(2)](https://man7.org/linux/man-pages/man2/mmap.2.html)

## 3. Ciclo de Copy-on-Write y consecuencias de una falla

`PROT_READ` y `PROT_WRITE` son los permisos solicitados por la aplicación. En una VMA, `VM_READ` y `VM_WRITE` describen los permisos actuales. `VM_MAYWRITE` representa el límite de escritura que puede admitir la región, relevante al cambiar protecciones. La MMU consulta el permiso de la traducción activa. Por ello, una VMA privada puede permitir escritura con `VM_WRITE` mientras su PTE sigue temporalmente protegida para provocar COW.

1. Una lectura de un mapeo privado de archivo puede usar la página de la caché con una traducción de solo lectura.
2. Si el proceso tiene permiso de escritura sobre la VMA, la primera escritura sobre esa traducción provoca un fallo de protección. `do_wp_page()` y, cuando corresponde, `wp_page_copy()` preparan la página anónima. Si la escritura es el primer acceso y falta la PTE, la ruta empieza en `do_cow_fault()`.
3. El kernel sustituye la traducción bajo el bloqueo correspondiente, comprueba que la entrada observada siga siendo válida e invalida las traducciones antiguas de la TLB cuando corresponde. La CPU reintenta la escritura sobre la copia privada. El contenido archivado sigue respaldado por su página original.

El invariante de seguridad es que una escritura privada autorizada afecta únicamente a la página privada. Si el kernel atribuye indebidamente permisos de escritura al marco del archivo o valida una decisión de COW frente a un estado que ya cambió, una escritura podría alterar la página compartida. Si un componente privilegiado consume luego ese contenido, la corrupción podría convertirse en una escalada. Dirty COW (CVE-2016-5195) fue una falla histórica en la gestión de COW ante concurrencia.

**Fuentes.** [wp_page_copy y do_wp_page](https://github.com/torvalds/linux/blob/v6.12/mm/memory.c#L3331) · [do_cow_fault](https://github.com/torvalds/linux/blob/v6.12/mm/memory.c#L5309) · [Cambios de protección](https://github.com/torvalds/linux/blob/v6.12/mm/mprotect.c#L709) · [NVD, CVE-2016-5195](https://nvd.nist.gov/vuln/detail/cve-2016-5195)

## 4. Intercepción en la tabla de llamadas frente a LSM

El gancho de seguridad se llama **`mmap_file`** en Linux v6.12. `security_mmap_file()` lo invoca con el archivo, la protección solicitada, la protección efectiva calculada y las banderas. El gancho **`file_mprotect`** permite evaluar cambios posteriores de protección de una VMA.

| Aspecto | LKM que intenta interceptar la tabla de llamadas | LSM |
| --- | --- | --- |
| Punto de observación | Entrada de la llamada al sistema. En arquitecturas o versiones donde el despacho usa la tabla, ofrece visibilidad temprana de los argumentos de esa ABI. | Punto de autorización de la operación. Trabaja con un `struct file` ya resuelto y, en `file_mprotect`, con la VMA existente. |
| Viabilidad en x86-64 v6.12 | El despacho nativo usa el `switch` de `x64_sys_call()`. La tabla `sys_call_table` se conserva para el trazado. Alterar esa tabla carece de efecto sobre ese despacho y dependería de detalles internos del kernel. | Un LSM integrado usa la infraestructura de seguridad del kernel. BPF LSM puede cargar una política en ejecución si está habilitado y el cargador tiene los privilegios correspondientes. |
| Cobertura y coste | Una intercepción ligada a la ABI debe considerar rutas y variantes adicionales. Su mantenimiento depende de la versión y la arquitectura. | Ofrece un punto para permitir, denegar y auditar. Los ganchos etiquetan bien la operación, pero su coste depende de la política. El gancho inicial tampoco observa cada acceso de CPU a la región ya mapeada. |

Para la defensa planteada, el punto de decisión adecuado se sitúa en la creación del mapeo y en cualquier cambio de protección que habilite escritura después. La elección concreta del mecanismo queda para el diseño.

**Fuentes.** [Despacho x86-64](https://github.com/torvalds/linux/blob/v6.12/arch/x86/entry/syscall_64.c) · [Definición de ganchos LSM](https://github.com/torvalds/linux/blob/v6.12/include/linux/lsm_hook_defs.h#L196) · [Implementación de la mediación](https://github.com/torvalds/linux/blob/v6.12/security/security.c#L2976) · [BPF LSM](https://docs.kernel.org/6.12/bpf/prog_lsm.html)

## 5. Cifrado transparente y caché de páginas

En un sistema de archivos compatible con `fscrypt`, los bloques de datos permanecen cifrados en almacenamiento. Con la clave disponible, el sistema de archivos obtiene el texto claro durante la lectura y lo deja en la caché de páginas. El fallo de página del proceso autorizado puede instalar una PTE que apunta a esa página legible. Para persistir datos modificados, la ruta de escritura aplica el cifrado antes de que los datos queden en almacenamiento. La transformación puede hacerse en software o con cifrado en línea, según el sistema y el dispositivo.

La caché se administra por archivo y posición dentro del archivo, y puede compartirse entre mapeos autorizados. El control de acceso decide qué procesos pueden obtener un descriptor y mapear el objeto. Las tablas de páginas y la MMU limitan qué procesos pueden acceder a cada marco mediante sus direcciones virtuales. Por ello, tener la página descifrada en memoria física no concede por sí solo acceso a otro proceso.

El modelo de amenaza de `fscrypt` cubre principalmente el acceso al almacenamiento sin las claves. El texto claro existe en RAM mientras la página está en uso o en caché. Un proceso con acceso autorizado al archivo puede leerlo y copiarlo. La separación también depende de los controles sobre depuración, volcados y acceso a memoria, así como de la integridad del kernel.

**Fuentes.** [Modelo de amenaza, mmap e implementación de fscrypt](https://docs.kernel.org/6.12/filesystems/fscrypt.html) · [Tablas de páginas](https://docs.kernel.org/6.12/mm/page_tables.html)

## 6. Páginas sucias, msync, fsync e integridad

En un mapeo compartido, el primer fallo de escritura puede requerir `page_mkwrite` para preparar el sistema de archivos y marcar los datos para escritura. El estado sucio de una PTE y el estado sucio del *folio* de la caché son conceptos distintos. La infraestructura de memoria coordina ambos. Durante el *writeback*, mecanismos como `folio_clear_dirty_for_io()` y `folio_mkclean()` coordinan la limpieza del estado y la protección de las traducciones compartidas. Si la página vuelve a modificarse, el sistema puede detectar que debe escribirse otra vez. El sistema de archivos aplica las reglas de ordenamiento, escritura de metadatos y tratamiento de errores que correspondan.

- **`msync(MS_SYNC)`** sincroniza el intervalo solicitado del mapeo compartido respaldado por un archivo. En v6.12, la ruta llama a `vfs_fsync_range()` para la parte correspondiente del archivo y espera el resultado. Los cambios propios de un mapeo privado quedan en sus páginas anónimas.
- **`fsync(fd)`** solicita la sincronización de los datos modificados de ese archivo y sus metadatos. Para asegurar la entrada de directorio de un archivo recién creado también puede requerirse `fsync()` sobre el directorio. La aplicación debe verificar los códigos de error y sincronizar a sus escritores si necesita coherencia lógica entre varios cambios.

Un filtro que observa exclusivamente `write()` carece de visibilidad sobre las escrituras de CPU que usan una PTE válida y escribible. Un filtro en `msync()` también tiene cobertura parcial porque existe *writeback* en segundo plano y otros puntos de sincronización. Un control preventivo puede decidir si se permite el mapeo escribible. Si se necesita verificar el contenido que llega al almacenamiento, hace falta una política integrada en una ruta que cubra la persistencia efectiva. La sincronización por sí sola no valida la autorización ni convierte varias escrituras en una transacción.

**Fuentes.** [msync de Linux](https://github.com/torvalds/linux/blob/v6.12/mm/msync.c#L32) · [vfs_fsync_range](https://github.com/torvalds/linux/blob/v6.12/fs/sync.c#L180) · [Gestión del estado sucio](https://github.com/torvalds/linux/blob/v6.12/mm/page-writeback.c#L2987) · [msync(2)](https://man7.org/linux/man-pages/man2/msync.2.html) · [fsync(2)](https://man7.org/linux/man-pages/man2/fsync.2.html)

## 7. Auditoría y control con eBPF

La instrumentación puede cargarse sin recompilar si el kernel ya incluye las capacidades necesarias y el cargador tiene permisos. La compatibilidad se verifica en el kernel objetivo. `fexit` depende de BTF y de que la función admita ese tipo de conexión. Los *kprobes* dependen de que la función sea instrumentable.

| Mecanismo | Papel en la defensa |
| --- | --- |
| *Tracepoints* de llamadas al sistema, cuando están disponibles | Correlacionar entradas y resultados de `mmap()` y `mprotect()` para medir frecuencia, intentos fallidos y cambios de protección. Permiten observar también rechazos anteriores al gancho LSM. |
| `kprobe/kretprobe` o `fexit` | Observar funciones de la ruta de memoria virtual. Por ejemplo, permiten muestrear llamadas y resultados de `handle_mm_fault()` cuando el entorno lo soporta. Son instrumentos de diagnóstico. Un fallo de página aislado es compatible con un acceso normal. |
| BPF LSM en `mmap_file` y `file_mprotect` | Inspeccionar el objeto de archivo y la protección pertinente. Puede dejar constancia de la decisión y denegar una operación que incumpla la política. Un programa de la cadena debe propagar una denegación previa. |

Para auditar una política, se pueden registrar el instante, el identificador del proceso, el usuario, la identidad del archivo, las protecciones, las banderas y la decisión. El análisis puede distinguir intentos rechazados de fallos normales y agregar eventos por proceso o archivo. Los accesos que ya tienen una traducción válida se ejecutan en hardware sin pasar por esos ganchos. La auditoría cubre operaciones observables, como la creación del mapeo, cambios de protección y fallos instrumentados.

**Fuentes.** [Secciones y tipos de programa de libbpf](https://docs.kernel.org/6.12/bpf/libbpf/program_types.html) · [Kprobes](https://docs.kernel.org/6.12/trace/kprobes.html) · [BPF LSM](https://docs.kernel.org/6.12/bpf/prog_lsm.html)

## 8. O_RDONLY con MAP_PRIVATE y protección de lectura y escritura

**Sí.** Para un archivo regular que admite `mmap()`, Linux puede aceptar `O_RDONLY` con `PROT_READ | PROT_WRITE` y `MAP_PRIVATE`, sujeto a las restricciones del sistema de archivos y a la política de seguridad. En `do_mmap()`, el caso privado comprueba `FMODE_READ`. En cambio, una solicitud de `MAP_SHARED` con `PROT_WRITE` requiere que el descriptor tenga `FMODE_WRITE`. El permiso del descriptor y la protección de la copia privada cumplen funciones distintas.

El proceso puede modificar los bytes que ve en su vista privada. Otro componente que consuma ese búfer dentro del mismo proceso podría recibir la versión modificada. Los datos originales accesibles ya se revelan al abrir y mapear el archivo para lectura. Si se necesita mantener su confidencialidad frente a un proceso, la política debe controlar también la capacidad de leerlos. COW preserva el contenido del archivo frente a las escrituras privadas ordinarias.

**Comprobación en Linux 6.18.57 x86-64.** Con un archivo temporal propio cuyo primer byte era `A`, una vista privada con `O_RDONLY` y protección RW permitió cambiar ese byte a `B`. La lectura directa del descriptor devolvió `A` y, al cerrar el mapeo, el archivo conservó sus bytes originales. Con el mismo descriptor, un mapeo compartido RW devolvió `EACCES`.

**Fuentes.** [Permisos de do_mmap](https://github.com/torvalds/linux/blob/v6.12/mm/mmap.c#L380) · [mmap(2)](https://man7.org/linux/man-pages/man2/mmap.2.html)

## 9. Concurrencia entre MADV_DONTNEED y escrituras privadas

`MADV_DONTNEED` permite descartar traducciones del intervalo de direcciones. En Linux v6.12, `madvise_dontneed_single_vma()` delega en `zap_page_range_single()`. Para un mapeo privado de archivo, un acceso posterior puede repoblar la región desde el archivo. Por eso es esperable perder cambios privados descartados cuando el programa no coordina el descarte con sus escritores.

La prevención del fallo de seguridad depende de tres invariantes:

1. **Estabilidad de la región y de la traducción.** La infraestructura usa bloqueos de la VMA o del mapa del proceso según la ruta. La eliminación y actualización de PTE se coordinan con el bloqueo de la tabla correspondiente. Si la ruta COW preparó una copia y la PTE original cambió mientras tanto, `wp_page_copy()` revalida la entrada antes de instalarla.
2. **Coherencia entre CPUs.** Al retirar o sustituir traducciones, el kernel invalida las entradas de TLB que corresponden. Coordina esa invalidación con la liberación de recursos para evitar que otro núcleo siga usando una traducción obsoleta.
3. **Verificación de la página efectiva.** Cuando una ruta interna de `get_user_pages()` solicita una página para escritura, debe verificar la traducción y el respaldo actuales. En la ruta ordinaria de v6.12, `can_follow_write_pte()` comprueba los permisos y, en el caso privado especial que admite sin una PTE escribible, exige una página anónima exclusiva. La página de caché del archivo no satisface esa condición.

El objetivo del kernel es preservar el aislamiento y la integridad del archivo aunque coincidan ambas operaciones. El orden lógico de las operaciones de la aplicación requiere sincronización propia si la aplicación necesita conservar sus cambios privados.

**Fuentes.** [MADV_DONTNEED en v6.12](https://github.com/torvalds/linux/blob/v6.12/mm/madvise.c#L843) · [Retiro de traducciones](https://github.com/torvalds/linux/blob/v6.12/mm/memory.c#L1917) · [Revalidación de COW](https://github.com/torvalds/linux/blob/v6.12/mm/memory.c#L3390) · [Validación de acceso a la página](https://github.com/torvalds/linux/blob/v6.12/mm/gup.c#L793) · [Caché y TLB](https://docs.kernel.org/6.12/core-api/cachetlb.html) · [madvise(2)](https://man7.org/linux/man-pages/man2/madvise.2.html)

## 10. Descriptores, tablas de páginas y bits de protección

| Elemento | Función y efecto de seguridad |
| --- | --- |
| `struct page` y *folio* | Describen memoria física desde el kernel. Registran o permiten gestionar referencias, respaldo y estado de las páginas o grupos de páginas. Ayudan a gestionar la vida de la página y a distinguir memoria anónima de memoria respaldada por un archivo. La MMU aplica los permisos a partir de las tablas de páginas. |
| `PGD → P4D → PUD → PMD → PTE` | Jerarquía lógica de Linux que traduce direcciones virtuales a marcos físicos. En x86-64 de cuatro niveles, Linux pliega `P4D`. Una página grande puede tener una entrada hoja antes del nivel PTE. Cada espacio de direcciones de usuario tiene sus propias traducciones, aunque dos espacios puedan mapear un mismo marco con distintos permisos. |
| **NX** (*No Execute*) | Impide que la CPU obtenga instrucciones de una traducción marcada como no ejecutable. Los datos pueden seguir siendo accesibles según los demás permisos. |
| **R/W** | Controla la capacidad de escritura. Una traducción presente de solo lectura permite, cuando corresponde, capturar el intento de escritura para resolver COW. |
| **U/S** (*User/Supervisor*) | Separa páginas accesibles por código de usuario de páginas reservadas a supervisor. Un proceso en modo usuario carece de acceso a una traducción efectiva de supervisor. |

El modo útil de combinar estas protecciones es una política **W^X**, según la cual una región se mantiene escribible o ejecutable, con controles sobre los cambios de protección. Así, un búfer de datos puede ser RW y NX, y una región de código puede ser RX. La política debe considerar también mapeos con permisos diferentes sobre un mismo respaldo si necesita impedir que una vista escribible altere código que otra vista pueda ejecutar. NX limita la ejecución de datos inyectados. La autorización sobre archivos mapeados y la validación del contenido se controlan en capas adicionales.

**Fuentes.** [Descriptores de memoria](https://github.com/torvalds/linux/blob/v6.12/include/linux/mm_types.h) · [Tablas de páginas](https://docs.kernel.org/6.12/mm/page_tables.html) · [Paginación x86-64 de cinco niveles](https://docs.kernel.org/6.12/arch/x86/x86_64/5level-paging.html) · [Bits de página de x86](https://github.com/torvalds/linux/blob/v6.12/arch/x86/include/asm/pgtable_types.h)
