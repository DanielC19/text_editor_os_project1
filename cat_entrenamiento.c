#include "shell.h"
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>      // Para constantes como O_CREAT, O_RDONLY, O_WRONLY
#include <unistd.h>     // Para llamadas POSIX como read, write, close, unlink, rmdir
#include <string.h>     // Para manejo de cadenas como strcmp, strlen
#include <sys/stat.h>   // Para stat, mkdir, chmod, y constantes de permisos y tipos de archivo
#include <sys/types.h>  // Tipos de datos del sistema como mode_t, ssize_t

int cmd_clonar(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, COLOR_ERROR "Uso: clonar <archivo>\n" COLOR_RESET);
        return 1;
    }

    char *file_to_clone = argv[1];

    int fd_src = open(file_to_clone, O_RDONLY);
    if (fd_src == -1) {
        int fd = open(file_to_clone, O_CREAT | O_WRONLY | O_TRUNC, 0644);
        if (fd == -1) {
            perror("Error al crear el archivo");
            return 1;
        }
        printf("Archivo '%s' creado exitosamente (fd: %d).\n", file_to_clone, fd);
        // Siempre se debe cerrar el descriptor de archivo para liberar recursos del kernel.
        close(fd);

        return 0;
    }

    const char *dest = strcat(file_to_clone, ".clone");

    // Creamos/abrimos destino para escritura, si no existe lo crea (O_CREAT), si existe lo vacía (O_TRUNC)
    int fd_dest = open(dest, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd_dest == -1) {
        perror("Error al abrir/crear el archivo destino");
        close(fd_src);
        return 1;
    }

    char buffer[4096]; // Buffer generoso (4KB, tamaño de bloque común en discos) para eficiencia
    ssize_t bytes_read, bytes_written;

    // Bucle clásico de copia: leemos en bloques y escribimos lo leído.
    while ((bytes_read = read(fd_src, buffer, sizeof(buffer))) > 0) {
        bytes_written = write(fd_dest, buffer, bytes_read);
        if (bytes_written != bytes_read) {
            perror("Error de escritura al copiar");
            close(fd_src); close(fd_dest);
            return 1;
        }
    }
    if (bytes_read == -1) {
        perror("Error de lectura al copiar");
    } else {
        printf("Archivo '%s' copiado a '%s' exitosamente.\n", file_to_clone, dest);
    }
    close(fd_src);
    close(fd_dest);

    return 0;
}