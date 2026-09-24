#include "te_editor.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define TE_CHUNK_SIZE (64U * 1024U)
#define TE_WORKERS 4
#define TE_MAGIC "TEHF001"
#define TE_MAGIC_SIZE 8

typedef struct huffman_node
{
    unsigned char symbol;
    uint64_t frequency;
    struct huffman_node *left;
    struct huffman_node *right;
} huffman_node_t;

typedef struct
{
    unsigned char *data;
    size_t size;
    size_t bit_count;
} encoded_chunk_t;

typedef struct
{
    const unsigned char *data;
    size_t size;
    size_t chunk_count;
    uint64_t (*local_freq)[256];
    size_t next_chunk;
    size_t completed;
    pthread_mutex_t mutex;
} frequency_pool_t;

typedef struct
{
    const unsigned char *data;
    size_t size;
    size_t chunk_count;
    encoded_chunk_t *chunks;
    char (*codes)[256];
    size_t next_chunk;
    size_t completed;
    pthread_mutex_t mutex;
} encode_pool_t;

typedef struct
{
    const unsigned char *payload;
    size_t payload_bits;
    unsigned char *output;
    size_t output_size;
    size_t chunk_count;
    huffman_node_t *root;
    size_t next_chunk;
    size_t completed;
    pthread_mutex_t mutex;
} decode_pool_t;

typedef struct
{
    int mode;
    unsigned char *data;
    size_t size;
    char *path;
} background_job_t;

static void progress_set(size_t progress, size_t total)
{
    pthread_mutex_lock(&te_background.mutex);
    te_background.progress = progress;
    te_background.total = total;
    pthread_mutex_unlock(&te_background.mutex);
}

static void progress_add(size_t amount)
{
    pthread_mutex_lock(&te_background.mutex);
    te_background.progress += amount;
    pthread_mutex_unlock(&te_background.mutex);
}

static int write_all(int fd, const void *buffer, size_t size)
{
    const unsigned char *bytes = buffer;
    while (size > 0)
    {
        ssize_t written = write(fd, bytes, size);
        if (written < 0)
        {
            if (errno == EINTR)
                continue;
            return -1;
        }
        bytes += written;
        size -= (size_t)written;
    }
    return 0;
}

static int read_all(const char *path, unsigned char **data_out, size_t *size_out)
{
    int fd = -1;
    unsigned char *data = NULL;
    size_t size = 0;
    size_t capacity = 0;
    unsigned char buffer[TE_CHUNK_SIZE];

    fd = open(path, O_RDONLY);
    if (fd < 0)
        return -1;

    for (;;)
    {
        ssize_t count = read(fd, buffer, sizeof(buffer));
        if (count == 0)
            break;
        if (count < 0)
        {
            if (errno == EINTR)
                continue;
            free(data);
            close(fd);
            return -1;
        }
        if (size + (size_t)count > capacity)
        {
            size_t new_capacity = capacity == 0 ? TE_CHUNK_SIZE : capacity * 2;
            while (new_capacity < size + (size_t)count)
                new_capacity *= 2;
            unsigned char *new_data = realloc(data, new_capacity);
            if (new_data == NULL)
            {
                free(data);
                close(fd);
                errno = ENOMEM;
                return -1;
            }
            data = new_data;
            capacity = new_capacity;
        }
        memcpy(data + size, buffer, (size_t)count);
        size += (size_t)count;
    }

    close(fd);
    *data_out = data;
    *size_out = size;
    return 0;
}

static int snapshot_editor(unsigned char **data_out, size_t *size_out)
{
    size_t size = 0;
    unsigned char *data;
    size_t offset = 0;

    if (te_global_state.fd < 0)
    {
        printf("Error: no file open. Use 'editor <archivo>' first.\n");
        return -1;
    }

    for (line_node_t *line = te_global_state.lines; line != NULL; line = line->next)
    {
        for (word_node_t *word = line->words; word != NULL; word = word->next)
            size += word->word == NULL ? 0 : strlen(word->word);
        size++;
    }

    data = size == 0 ? NULL : malloc(size);
    if (size > 0 && data == NULL)
    {
        printf("Error: out of memory creating compression snapshot.\n");
        return -1;
    }

    for (line_node_t *line = te_global_state.lines; line != NULL; line = line->next)
    {
        for (word_node_t *word = line->words; word != NULL; word = word->next)
        {
            size_t word_size = word->word == NULL ? 0 : strlen(word->word);
            memcpy(data + offset, word->word, word_size);
            offset += word_size;
        }
        data[offset++] = '\n';
    }

    *data_out = data;
    *size_out = size;
    return 0;
}

static void *frequency_worker(void *argument)
{
    frequency_pool_t *pool = argument;
    for (;;)
    {
        size_t chunk;
        size_t start;
        size_t length;
        uint64_t *local;

        pthread_mutex_lock(&pool->mutex);
        chunk = pool->next_chunk++;
        pthread_mutex_unlock(&pool->mutex);
        if (chunk >= pool->chunk_count)
            break;

        start = chunk * TE_CHUNK_SIZE;
        length = pool->size - start;
        if (length > TE_CHUNK_SIZE)
            length = TE_CHUNK_SIZE;
        local = pool->local_freq[chunk];
        for (size_t i = 0; i < length; i++)
            local[pool->data[start + i]]++;

        pthread_mutex_lock(&pool->mutex);
        pool->completed += length;
        pthread_mutex_unlock(&pool->mutex);
        progress_add(length);
    }
    return NULL;
}

static int run_frequency_pool(const unsigned char *data, size_t size, uint64_t freq[256])
{
    frequency_pool_t pool = {0};
    pthread_t workers[TE_WORKERS];
    size_t worker_count;

    memset(freq, 0, sizeof(uint64_t) * 256);
    pool.data = data;
    pool.size = size;
    pool.chunk_count = size == 0 ? 0 : (size + TE_CHUNK_SIZE - 1) / TE_CHUNK_SIZE;
    pool.local_freq = calloc(pool.chunk_count == 0 ? 1 : pool.chunk_count, sizeof(*pool.local_freq));
    if (pool.local_freq == NULL)
        return -1;
    pthread_mutex_init(&pool.mutex, NULL);
    worker_count = pool.chunk_count < TE_WORKERS ? pool.chunk_count : TE_WORKERS;
    for (size_t i = 0; i < worker_count; i++)
        pthread_create(&workers[i], NULL, frequency_worker, &pool);
    for (size_t i = 0; i < worker_count; i++)
        pthread_join(workers[i], NULL);
    for (size_t chunk = 0; chunk < pool.chunk_count; chunk++)
        for (size_t symbol = 0; symbol < 256; symbol++)
            freq[symbol] += pool.local_freq[chunk][symbol];
    pthread_mutex_destroy(&pool.mutex);
    free(pool.local_freq);
    return 0;
}

static void free_tree(huffman_node_t *node)
{
    if (node == NULL)
        return;
    free_tree(node->left);
    free_tree(node->right);
    free(node);
}

static huffman_node_t *build_tree(const uint64_t freq[256])
{
    huffman_node_t *nodes[512];
    size_t count = 0;

    for (size_t symbol = 0; symbol < 256; symbol++)
    {
        if (freq[symbol] == 0)
            continue;
        nodes[count] = calloc(1, sizeof(*nodes[count]));
        if (nodes[count] == NULL)
            goto failure;
        nodes[count]->symbol = (unsigned char)symbol;
        nodes[count]->frequency = freq[symbol];
        count++;
    }
    if (count == 0)
        return NULL;
    while (count > 1)
    {
        size_t first = 0;
        size_t second = 1;
        if (nodes[second]->frequency < nodes[first]->frequency)
        {
            size_t temp = first;
            first = second;
            second = temp;
        }
        for (size_t i = 2; i < count; i++)
        {
            if (nodes[i]->frequency < nodes[first]->frequency)
            {
                second = first;
                first = i;
            }
            else if (nodes[i]->frequency < nodes[second]->frequency)
                second = i;
        }
        huffman_node_t *parent = calloc(1, sizeof(*parent));
        if (parent == NULL)
            goto failure;
        parent->left = nodes[first];
        parent->right = nodes[second];
        parent->frequency = parent->left->frequency + parent->right->frequency;
        if (first > second)
        {
            size_t temp = first;
            first = second;
            second = temp;
        }
        nodes[first] = parent;
        nodes[second] = nodes[count - 1];
        count--;
    }
    return nodes[0];

failure:
    for (size_t i = 0; i < count; i++)
        free_tree(nodes[i]);
    return NULL;
}

static void make_codes(huffman_node_t *node, char (*codes)[256], char *prefix, size_t length)
{
    if (node->left == NULL && node->right == NULL)
    {
        if (length == 0)
            prefix[length++] = '0';
        prefix[length] = '\0';
        strcpy(codes[node->symbol], prefix);
        return;
    }
    prefix[length] = '0';
    make_codes(node->left, codes, prefix, length + 1);
    prefix[length] = '1';
    make_codes(node->right, codes, prefix, length + 1);
}

static void *encode_worker(void *argument)
{
    encode_pool_t *pool = argument;
    for (;;)
    {
        size_t chunk;
        size_t start;
        size_t length;
        size_t bits = 0;
        unsigned char *encoded = NULL;
        size_t capacity = 0;

        pthread_mutex_lock(&pool->mutex);
        chunk = pool->next_chunk++;
        pthread_mutex_unlock(&pool->mutex);
        if (chunk >= pool->chunk_count)
            break;
        start = chunk * TE_CHUNK_SIZE;
        length = pool->size - start;
        if (length > TE_CHUNK_SIZE)
            length = TE_CHUNK_SIZE;
        for (size_t i = 0; i < length; i++)
        {
            const char *code = pool->codes[pool->data[start + i]];
            for (size_t bit = 0; code[bit] != '\0'; bit++)
            {
                if (bits % 8 == 0)
                {
                    if (bits / 8 == capacity)
                    {
                        capacity = capacity == 0 ? 128 : capacity * 2;
                        encoded = realloc(encoded, capacity);
                        if (encoded == NULL)
                            return NULL;
                    }
                    encoded[bits / 8] = 0;
                }
                if (code[bit] == '1')
                    encoded[bits / 8] |= (unsigned char)(1U << (7U - (bits % 8)));
                bits++;
            }
        }
        pool->chunks[chunk].data = encoded;
        pool->chunks[chunk].size = (bits + 7) / 8;
        pool->chunks[chunk].bit_count = bits;
        pthread_mutex_lock(&pool->mutex);
        pool->completed += length;
        pthread_mutex_unlock(&pool->mutex);
        progress_add(length);
    }
    return NULL;
}

static int encode_chunks(const unsigned char *data, size_t size, char (*codes)[256], encoded_chunk_t **chunks_out, size_t *count_out)
{
    encode_pool_t pool = {0};
    pthread_t workers[TE_WORKERS];
    size_t worker_count;
    pool.data = data;
    pool.size = size;
    pool.chunk_count = size == 0 ? 0 : (size + TE_CHUNK_SIZE - 1) / TE_CHUNK_SIZE;
    pool.codes = codes;
    pool.chunks = calloc(pool.chunk_count == 0 ? 1 : pool.chunk_count, sizeof(*pool.chunks));
    if (pool.chunks == NULL)
        return -1;
    pthread_mutex_init(&pool.mutex, NULL);
    worker_count = pool.chunk_count < TE_WORKERS ? pool.chunk_count : TE_WORKERS;
    for (size_t i = 0; i < worker_count; i++)
        pthread_create(&workers[i], NULL, encode_worker, &pool);
    for (size_t i = 0; i < worker_count; i++)
        pthread_join(workers[i], NULL);
    pthread_mutex_destroy(&pool.mutex);
    *chunks_out = pool.chunks;
    *count_out = pool.chunk_count;
    return 0;
}

static int write_compressed(const char *path, const unsigned char *data, size_t size)
{
    uint64_t freq[256];
    char codes[256][256] = {{0}};
    char prefix[256];
    encoded_chunk_t *chunks = NULL;
    size_t chunk_count = 0;
    size_t total_bits = 0;
    size_t payload_size;
    unsigned char *payload = NULL;
    size_t bit_offset = 0;
    int fd = -1;
    unsigned char magic[TE_MAGIC_SIZE] = TE_MAGIC;

    progress_set(0, size * 2 + 1);
    if (run_frequency_pool(data, size, freq) < 0)
        return -1;
    huffman_node_t *tree = build_tree(freq);
    if (size > 0 && tree == NULL)
        return -1;
    if (tree != NULL)
    {
        memset(prefix, 0, sizeof(prefix));
        make_codes(tree, codes, prefix, 0);
    }
    if (encode_chunks(data, size, codes, &chunks, &chunk_count) < 0)
    {
        free_tree(tree);
        return -1;
    }
    for (size_t i = 0; i < chunk_count; i++)
        total_bits += chunks[i].bit_count;
    payload_size = (total_bits + 7) / 8;
    payload = payload_size == 0 ? NULL : calloc(payload_size, 1);
    if (payload_size > 0 && payload == NULL)
        goto failure;
    for (size_t chunk = 0; chunk < chunk_count; chunk++)
        for (size_t bit = 0; bit < chunks[chunk].bit_count; bit++)
        {
            if (chunks[chunk].data[bit / 8] & (1U << (7U - (bit % 8))))
                payload[bit_offset / 8] |= (unsigned char)(1U << (7U - (bit_offset % 8)));
            bit_offset++;
        }
    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
        goto failure;
    if (write_all(fd, magic, sizeof(magic)) < 0 ||
        write_all(fd, &size, sizeof(uint64_t)) < 0 ||
        write_all(fd, &total_bits, sizeof(uint64_t)) < 0 ||
        write_all(fd, freq, sizeof(freq)) < 0 ||
        write_all(fd, payload, payload_size) < 0)
        goto failure;
    close(fd);
    for (size_t i = 0; i < chunk_count; i++)
        free(chunks[i].data);
    free(chunks);
    free(payload);
    free_tree(tree);
    progress_set(size * 2 + 1, size * 2 + 1);
    return 0;

failure:
    if (fd >= 0)
        close(fd);
    for (size_t i = 0; i < chunk_count; i++)
        free(chunks[i].data);
    free(chunks);
    free(payload);
    free_tree(tree);
    return -1;
}

static unsigned char get_bit(const unsigned char *data, size_t bit)
{
    return (unsigned char)((data[bit / 8] >> (7U - (bit % 8))) & 1U);
}

static void *decode_worker(void *argument)
{
    decode_pool_t *pool = argument;
    for (;;)
    {
        size_t chunk;
        size_t start;
        size_t length;
        size_t bit = 0;
        huffman_node_t *node;

        pthread_mutex_lock(&pool->mutex);
        chunk = pool->next_chunk++;
        pthread_mutex_unlock(&pool->mutex);
        if (chunk >= pool->chunk_count)
            break;
        start = chunk * TE_CHUNK_SIZE;
        length = pool->output_size - start;
        if (length > TE_CHUNK_SIZE)
            length = TE_CHUNK_SIZE;
        node = pool->root;
        for (size_t output = 0; output < start; output++)
        {
            while (node->left != NULL)
                node = get_bit(pool->payload, bit++) ? node->right : node->left;
            node = pool->root;
        }
        for (size_t output = 0; output < length; output++)
        {
            while (node->left != NULL)
                node = get_bit(pool->payload, bit++) ? node->right : node->left;
            pool->output[start + output] = node->symbol;
            node = pool->root;
        }
        pthread_mutex_lock(&pool->mutex);
        pool->completed += length;
        pthread_mutex_unlock(&pool->mutex);
        progress_add(length);
    }
    return NULL;
}

static int write_decompressed(const char *path, const unsigned char *data, size_t size)
{
    const size_t header_size = TE_MAGIC_SIZE + sizeof(uint64_t) * 2 + sizeof(uint64_t) * 256;
    uint64_t original_size;
    uint64_t total_bits;
    uint64_t freq[256];
    huffman_node_t *tree = NULL;
    unsigned char *output = NULL;
    decode_pool_t pool = {0};
    pthread_t workers[TE_WORKERS];
    size_t worker_count;
    int fd = -1;

    if (size < header_size || memcmp(data, TE_MAGIC, strlen(TE_MAGIC)) != 0)
    {
        printf("Error: invalid Huffman file.\n");
        return -1;
    }
    memcpy(&original_size, data + TE_MAGIC_SIZE, sizeof(original_size));
    memcpy(&total_bits, data + TE_MAGIC_SIZE + sizeof(original_size), sizeof(total_bits));
    memcpy(freq, data + TE_MAGIC_SIZE + sizeof(original_size) + sizeof(total_bits), sizeof(freq));
    if (original_size > SIZE_MAX || total_bits > (size - header_size) * 8ULL)
        return -1;
    if (original_size == 0)
    {
        fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0)
            return -1;
        close(fd);
        progress_set(1, 1);
        return 0;
    }
    tree = build_tree(freq);
    if (tree == NULL)
        return -1;
    output = malloc((size_t)original_size);
    if (output == NULL)
        goto failure;
    pool.payload = data + header_size;
    pool.payload_bits = (size_t)total_bits;
    pool.output = output;
    pool.output_size = (size_t)original_size;
    pool.root = tree;
    pool.chunk_count = (pool.output_size + TE_CHUNK_SIZE - 1) / TE_CHUNK_SIZE;
    pthread_mutex_init(&pool.mutex, NULL);
    progress_set(0, pool.output_size);
    worker_count = pool.chunk_count < TE_WORKERS ? pool.chunk_count : TE_WORKERS;
    for (size_t i = 0; i < worker_count; i++)
        pthread_create(&workers[i], NULL, decode_worker, &pool);
    for (size_t i = 0; i < worker_count; i++)
        pthread_join(workers[i], NULL);
    pthread_mutex_destroy(&pool.mutex);
    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0 || write_all(fd, output, (size_t)original_size) < 0)
        goto failure;
    close(fd);
    free(output);
    free_tree(tree);
    progress_set((size_t)original_size, (size_t)original_size);
    return 0;

failure:
    if (fd >= 0)
        close(fd);
    free(output);
    free_tree(tree);
    return -1;
}

static void *background_worker(void *argument)
{
    background_job_t *job = argument;
    int result;
    if (job->mode == 1)
        result = write_compressed(job->path, job->data, job->size);
    else
    {
        unsigned char *encoded = NULL;
        size_t encoded_size = 0;
        result = read_all(job->path, &encoded, &encoded_size);
        if (result == 0)
            result = write_decompressed(job->data == NULL ? "" : (char *)job->data, encoded, encoded_size);
        free(encoded);
    }
    pthread_mutex_lock(&te_background.mutex);
    te_background.result = result;
    te_background.active = 0;
    pthread_mutex_unlock(&te_background.mutex);
    free(job->data);
    free(job->path);
    free(job);
    return NULL;
}

static void reload_after_decompression(const char *operation, int result)
{
    if (result == 0 && strcmp(operation, "decompressing") == 0 && te_global_state.fd >= 0)
    {
        te_free_line_list(te_global_state.lines);
        te_global_state.lines = NULL;
        te_global_state.line_count = 0;
        if (te_load_file_into_nodes(&te_global_state) < 0)
            printf("Error: decompressed file was written but could not be reloaded.\n");
    }
}

static int start_job(int mode, unsigned char *data, size_t size, const char *input, const char *output)
{
    background_job_t *job;
    const char *path = mode == 1 ? output : input;
    pthread_attr_t attributes;

    if (te_background_busy())
        return -1;
    te_background_join();
    job = calloc(1, sizeof(*job));
    if (job == NULL)
        return -1;
    job->mode = mode;
    job->data = data;
    job->size = size;
    job->path = strdup(path);
    if (job->path == NULL)
    {
        free(job->data);
        free(job);
        return -1;
    }
    pthread_mutex_lock(&te_background.mutex);
    te_background.active = 1;
    te_background.result = 0;
    te_background.progress = 0;
    te_background.total = size == 0 ? 1 : size * 2 + 1;
    strcpy(te_background.operation, mode == 1 ? "compressing" : "decompressing");
    pthread_mutex_unlock(&te_background.mutex);
    pthread_attr_init(&attributes);
    pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_JOINABLE);
    if (pthread_create(&te_background.thread, &attributes, background_worker, job) != 0)
    {
        pthread_attr_destroy(&attributes);
        pthread_mutex_lock(&te_background.mutex);
        te_background.active = 0;
        pthread_mutex_unlock(&te_background.mutex);
        free(job->data);
        free(job->path);
        free(job);
        return -1;
    }
    pthread_attr_destroy(&attributes);
    te_background.thread_started = 1;
    return 0;
}

int te_start_compress(const char *output_path)
{
    unsigned char *snapshot = NULL;
    size_t size = 0;
    if (output_path == NULL || *output_path == '\0')
    {
        printf("Error: usage: c <archivo_salida>\n");
        return -1;
    }
    if (te_background_busy())
    {
        te_editor_busy_error();
        return -1;
    }
    if (snapshot_editor(&snapshot, &size) < 0)
        return -1;
    if (start_job(1, snapshot, size, NULL, output_path) < 0)
    {
        printf("Error: could not start compression.\n");
        return -1;
    }
    printf("Compression started in background: %s\n", output_path);
    return 0;
}

int te_start_decompress(const char *input_path)
{
    char *output_path;
    if (input_path == NULL || *input_path == '\0')
    {
        printf("Error: usage: u <archivo_comprimido>\n");
        return -1;
    }
    if (te_global_state.fd < 0)
    {
        printf("Error: open a destination file with 'editor <archivo>' first.\n");
        return -1;
    }
    if (te_background_busy())
    {
        te_editor_busy_error();
        return -1;
    }
    output_path = strdup(te_global_state.path);
    if (output_path == NULL)
        return -1;
    if (start_job(2, (unsigned char *)output_path, 0, input_path, output_path) < 0)
    {
        printf("Error: could not start decompression.\n");
        return -1;
    }
    printf("Decompression started in background; destination: %s\n", te_global_state.path);
    return 0;
}

int te_background_busy(void)
{
    int active;
    pthread_mutex_lock(&te_background.mutex);
    active = te_background.active;
    pthread_mutex_unlock(&te_background.mutex);
    return active;
}

int te_editor_busy_error(void)
{
    if (!te_background_busy())
        return 0;
    printf("Error: editor is busy with a background compression task. Use 'status' and wait.\n");
    return 1;
}

int te_background_join(void)
{
    pthread_t thread;
    int should_join;
    pthread_mutex_lock(&te_background.mutex);
    should_join = te_background.thread_started && !te_background.active;
    thread = te_background.thread;
    pthread_mutex_unlock(&te_background.mutex);
    if (should_join)
    {
        char operation[sizeof(te_background.operation)];
        int result;
        pthread_join(thread, NULL);
        pthread_mutex_lock(&te_background.mutex);
        te_background.thread_started = 0;
        result = te_background.result;
        strcpy(operation, te_background.operation);
        pthread_mutex_unlock(&te_background.mutex);
        reload_after_decompression(operation, result);
    }
    return 0;
}

int te_background_wait(void)
{
    pthread_t thread;
    int should_join;
    int result;
    char operation[sizeof(te_background.operation)];

    pthread_mutex_lock(&te_background.mutex);
    should_join = te_background.thread_started;
    thread = te_background.thread;
    pthread_mutex_unlock(&te_background.mutex);
    if (should_join)
    {
        pthread_join(thread, NULL);
        pthread_mutex_lock(&te_background.mutex);
        te_background.thread_started = 0;
        result = te_background.result;
        strcpy(operation, te_background.operation);
        pthread_mutex_unlock(&te_background.mutex);
        reload_after_decompression(operation, result);
        printf("Background %s finished: %s\n", operation, result == 0 ? "success" : "error");
    }
    return 0;
}

int te_background_status(void)
{
    size_t progress;
    size_t total;
    int active;
    int result;
    char operation[sizeof(te_background.operation)];

    pthread_mutex_lock(&te_background.mutex);
    progress = te_background.progress;
    total = te_background.total;
    active = te_background.active;
    result = te_background.result;
    strcpy(operation, te_background.operation);
    pthread_mutex_unlock(&te_background.mutex);
    if (active)
        printf("Background %s: %zu%% (%zu/%zu bytes)\n", operation, total == 0 ? 0 : progress * 100 / total, progress, total);
    else if (te_background.thread_started)
    {
        te_background_join();
        printf("Background %s finished: %s\n", operation, result == 0 ? "success" : "error");
    }
    else
        printf("Background task: idle\n");
    return result;
}