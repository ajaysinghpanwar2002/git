#include <errno.h>
#include <openssl/sha.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <zlib.h>

int main(int argc, char *argv[]) {
  setbuf(stdout, NULL);
  setbuf(stderr, NULL);

  if (argc < 2) {
    fprintf(stderr, "Usage: ./mygit <command> [<args>]\n");
    return 1;
  }

  const char *command = argv[1];

  if (strcmp(command, "init") == 0) {
    fprintf(stderr, "Logs from your program will appear here!\n");

    if (mkdir(".git/objects", 0755) == -1 && errno != EEXIST) {
      fprintf(stderr, "Failed to create .git/objects: %s\n", strerror(errno));
      return 1;
    }
    if (mkdir(".git/refs", 0755) == -1 && errno != EEXIST) {
      fprintf(stderr, "Failed to create .git/refs: %s\n", strerror(errno));
      return 1;
    }

    FILE *headFile = fopen(".git/HEAD", "w");
    if (headFile == NULL) {
      fprintf(stderr, "Failed to create .git/HEAD file: %s\n", strerror(errno));
      return 1;
    }
    fprintf(headFile, "ref: refs/heads/main\n");
    fclose(headFile);

    printf("Initialized git directory\n");
  } else if (strcmp(command, "cat-file") == 0) {
    if (argc < 4 || strcmp(argv[2], "-p") != 0) {
      fprintf(stderr, "Usage: cat-file -p <object_hash>\n");
      return 1;
    }

    const char *hash = argv[3];
    if (strlen(hash) != 40) {
      fprintf(stderr, "Invalid hash length\n");
      return 1;
    }

    // constructing a path: ./git/objects/xx/remaining_38_chars
    char object_path[256];
    snprintf(object_path, sizeof(object_path), ".git/objects/%.2s/%s", hash,
             hash + 2);

    // open and Read the compressed file.
    FILE *file = fopen(object_path, "rb");
    if (!file) {
      fprintf(stderr, "Failed to open object file: %s\n", strerror(errno));
      return 1;
    }

    // get file size
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    // Read compressed data.
    unsigned char *compressed_data = malloc(file_size);
    fread(compressed_data, 1, file_size, file);
    fclose(file);

    // Decompress using zlib
    unsigned char decompressed_data[8192]; // buffer for decompressed_data
    uLongf decompressed_size = sizeof(decompressed_data);

    int result = uncompress(decompressed_data, &decompressed_size,
                            compressed_data, file_size);
    free(compressed_data);

    if (result != Z_OK) {
      fprintf(stderr, "Failed to decompressed object data\n");
      return 1;
    }

    // find the null byte that seprates headed from content
    unsigned char *null_pos =
        memchr(decompressed_data, '\0', decompressed_size);
    if (!null_pos) {
      fprintf(stderr, "Invalid object format\n");
      return 1;
    }

    // content starts after the null byte
    unsigned char *content = null_pos + 1;
    size_t content_size = decompressed_size - (content - decompressed_data);

    // print content without trailing newline
    fwrite(content, 1, content_size, stdout);
  } else if (strcmp(command, "hash-object") == 0) {
    if (argc < 4 || strcmp(argv[2], "-w") != 0) {
      fprintf(stderr, "Usage: hash-object -w <file>\n");
      return 1;
    }

    const char *filename = argv[3];

    // Read the file
    FILE *file = fopen(filename, "rb");
    if (!file) {
      fprintf(stderr, "Failed to open file: %s\n", strerror(errno));
      return 1;
    }

    // Getting file size
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    // Read file contents
    unsigned char *file_content = malloc(file_size);
    fread(file_content, 1, file_size, file);
    fclose(file);

    // create blob header: "blob <size>\0"
    char header[64];
    int header_len = snprintf(header, sizeof(header), "blob %ld", file_size);

    // calculate total blob size (header + null byte + content)
    size_t blob_size = header_len + 1 + file_size;
    unsigned char *blob_data = malloc(blob_size);

    // Assemble blob: header + null byte + content
    memcpy(blob_data, header, header_len);
    blob_data[header_len] = '\0';
    memcpy(blob_data + header_len + 1, file_content, file_size);

    // calculate SHA-1 hash
    unsigned char hash[SHA_DIGEST_LENGTH];
    SHA1(blob_data, blob_size, hash);

    // convert hash to hex string
    char hash_hex[41];
    for (int i = 0; i < SHA_DIGEST_LENGTH; i++) {
      sprintf(hash_hex + i * 2, "%02x", hash[i]);
    }
    hash_hex[40] = '\0';

    // Create a directory path: ./git/objects/xx/
    char dir_path[256];
    snprintf(dir_path, sizeof(dir_path), ".git/objects/%.2s", hash_hex);

    if (mkdir(dir_path, 0755) == -1 && errno != EEXIST) {
      fprintf(stderr, "Failed to create directory: %s\n", strerror(errno));
      free(file_content);
      free(blob_data);
      return 1;
    }

    // create full object path: ./git/objects/xx/remaining_38_chars
    char object_path[256];
    snprintf(object_path, sizeof(object_path), "%s/%s", dir_path, hash_hex + 2);

    // compress the blob data
    uLongf compressed_size = compressBound(blob_size);
    unsigned char *compressed_data = malloc(compressed_size);

    int result =
        compress(compressed_data, &compressed_size, blob_data, blob_size);
    if (result != Z_OK) {
      fprintf(stderr, "Failed to compress blob data\n");
      free(file_content);
      free(blob_data);
      free(compressed_data);
      return 1;
    }

    // Write compressed data to object file.
    FILE *object_file = fopen(object_path, "wb");
    if (!object_file) {
      fprintf(stderr, "Failed to create object file: %s\n", strerror(errno));
      free(file_content);
      free(blob_data);
      free(compressed_data);
      return 1;
    }
    fwrite(compressed_data, 1, compressed_size, object_file);
    fclose(object_file);

    // clean up
    free(file_content);
    free(blob_data);
    free(compressed_data);

    printf("%s\n", hash_hex);
  } else if (strcmp(command, "ls-tree") == 0) {
    if (argc < 4 || strcmp(argv[2], "--name-only")) {
      fprintf(stderr, "Usage: ls-tree --name-only <tree_sha>\n");
      return 1;
    }

    const char *hash = argv[3];
    if (strlen(hash) != 40) {
      fprintf(stderr, "Invalid hash length \n");
      return 1;
    }

    // Construct object path: .git/objects/xx/remaining_38_chars
    char object_path[256];
    snprintf(object_path, sizeof(object_path), ".git/objects/%.2s/%s", hash,
             hash + 2);

    // open an read compressed file.
    FILE *file = fopen(object_path, "rb");
    if (!file) {
      fprintf(stderr, "Failed to open object file: %s\n", strerror(errno));
      return 1;
    }

    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    // Read compressed data
    unsigned char *compressed_data = malloc(file_size);
    fread(compressed_data, 1, file_size, file);
    fclose(file);

    // Decompress using zlib
    unsigned char decompressed_data[8192];
    uLongf decompressed_size = sizeof(decompressed_data);

    int result = uncompress(decompressed_data, &decompressed_size,
                            compressed_data, file_size);
    free(compressed_data);

    if (result != Z_OK) {
      fprintf(stderr, "Failed to decompress object data\n");
      return 1;
    }

    // find the null byte that seprates header from content.
    unsigned char *null_pos =
        memchr(decompressed_data, '\0', decompressed_size);
    if (!null_pos) {
      fprintf(stderr, "Invalid tree object format \n");
      return 1;
    }

    // content start after the null byte
    unsigned char *content = null_pos + 1;
    size_t content_size = decompressed_size - (content - decompressed_data);

    // parse tree enteries
    unsigned char *ptr = content;
    unsigned char *end = content + content_size;

    while (ptr < end) {
      // Read mode (as string until space)
      unsigned char *mode_start = ptr;
      while (ptr < end && *ptr != ' ')
        ptr++;
      if (ptr >= end)
        break;

      // Skip space
      ptr++;

      // Read name (until null byte)
      unsigned char *name_start = ptr;
      while (ptr < end && *ptr != '\0')
        ptr++;
      if (ptr >= end)
        break;

      // Print the name
      printf("%.*s\n", (int)(ptr - name_start), name_start);

      // Skip null byte
      ptr++;

      // Skip 20-byte SHA hash
      ptr += 20;
    }
  } else {
    fprintf(stderr, "Unknown command %s\n", command);
    return 1;
  }

  return 0;
}
