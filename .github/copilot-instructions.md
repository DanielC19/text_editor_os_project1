# Architectonical Design Principles

The program will allow reading, modifying, and saving plain text files using POSIX system calls. The system architecture must be prepared to scale in functionality.

Strategic Integration with the Shell: As a fundamental requirement, the developed text editor must be functionally integrated with the existing Shell within a new category called `cat_text_editor.c`.

Critical I/O Restriction: The use of high-level functions from the C standard library (such as fopen, fread, fwrite, or fclose) for text file manipulation is prohibited. All disk access must be performed through open, read, write, lseek, ftruncate, and close, etc. Only standard I/O (printf, scanf, fgets) is allowed for reading commands from STDIN and printing to the console on STDOUT.

# CLI Commands

| Command | Description | Recommended System Calls |
| :--- | :--- | :--- |
| `o [file]` | Open a file for editing. If it does not exist, create it with appropriate permissions. | `open()` with `O_RDWR | O_CREAT`, `mode_t` |
| `p [n]` | Print line `n`. Without parameters, print the entire file by reading bytes until `\n`. | `read()`, `lseek()`, `write()` (FD 1) |
| `a [text]` | Append the provided text as a new line at the end of the file. | `lseek()` (`SEEK_END`), `write()` |
| `d [n]` | Delete line `n` from the file (shift subsequent bytes and truncate). | `read()`, `write()`, `lseek()`, `ftruncate()` |
| `q` | Close the file descriptor and exit the application without memory leaks. | `close()`, `exit()` |
| `i [n] [text]` | Insert `text` at line `n`, shifting the remainder of the file forward. | `lseek()`, `read()`, `write()`, `ftruncate()`; in-memory buffers via `malloc()`/`free()` |
| `s [word]` | Search for `word` in the open file and report matching line numbers. | `read()`, `lseek()` |
| `m` | Print file metadata: size, permissions, inode number, and last modification time. | `fstat()` (on the open FD) |
| `y [n]` | Copy line `n` into a local sequential clipboard (in-memory). | `lseek()`, `read()`, `malloc()`/`free()` |
| `x [n]` | Paste the most recently copied line at line `n` (or current position as defined). | `lseek()`, `read()`, `write()`, `ftruncate()`, `malloc()`/`free()` |

Notes:

- Implement advanced `lseek()` usage and dynamic buffer management when shifting bytes to avoid data loss — use `malloc()`/`free()` for temporary buffers and ensure consistent error handling.
- Use `fstat()` to obtain inode metadata for the `m` command; format permissions and timestamps for human-readable output.
- Maintain a sequential in-memory clipboard for `y`/`x` operations; ensure clipboard state is preserved only while the editor runs (or persist if explicitly required).