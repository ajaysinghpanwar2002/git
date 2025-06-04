#include <dirent.h>
#include <errno.h>
#include <openssl/sha.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <zlib.h>

// Helper function to create a blob object and return its SHA-1 hash
char *create_blob_object(const char *file_path) {
  FILE *file = fopen(file_path, "rb");
  if (!file)
    return NULL;

  fseek(file, 0, SEEK_END);
  long file_size = ftell(file);
  fseek(file, 0, SEEK_SET);

  unsigned char *file_content = malloc(file_size);
  fread(file_content, 1, file_size, file);
  fclose(file);

  char header[64];
  int header_len = snprintf(header, sizeof(header), "blob %ld", file_size);

  size_t blob_size = header_len + 1 + file_size;
  unsigned char *blob_data = malloc(blob_size);

  memcpy(blob_data, header, header_len);
  blob_data[header_len] = '\0';
  memcpy(blob_data + header_len + 1, file_content, file_size);

  unsigned char hash[SHA_DIGEST_LENGTH];
  SHA1(blob_data, blob_size, hash);

  char *hash_hex = malloc(41);
  for (int i = 0; i < SHA_DIGEST_LENGTH; i++) {
    sprintf(hash_hex + i * 2, "%02x", hash[i]);
  }
  hash_hex[40] = '\0';

  // Create directory and save blob
  char dir_path[256];
  snprintf(dir_path, sizeof(dir_path), ".git/objects/%.2s", hash_hex);
  mkdir(dir_path, 0755);

  char object_path[256];
  snprintf(object_path, sizeof(object_path), "%s/%s", dir_path, hash_hex + 2);

  uLongf compressed_size = compressBound(blob_size);
  unsigned char *compressed_data = malloc(compressed_size);

  compress(compressed_data, &compressed_size, blob_data, blob_size);

  FILE *object_file = fopen(object_path, "wb");
  fwrite(compressed_data, 1, compressed_size, object_file);
  fclose(object_file);

  free(file_content);
  free(blob_data);
  free(compressed_data);

  return hash_hex;
}

char *create_tree_object(const char *dir_path) {
  DIR *dir = opendir(dir_path);
  if (!dir)
    return NULL;

  // collect enteries
  struct dirent *entry;
  // This will hold Git Mode, filename and SHA-1 hash for each file/directory.
  struct {
    char mode[10]; // eg "100644" for files, "40000" for dirs.
    char name[256];
    unsigned char hash[SHA_DIGEST_LENGTH]; // 20 byte SHA-1 hash of the blob
  } entries[1000];

  int entry_count = 0;
  while ((entry = readdir(dir)) != NULL) {

    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0 ||
        strcmp(entry->d_name, ".git") == 0) {
      continue; // skip special enteries and the .git dir
    }

    char full_path[512];
    snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);

    struct stat st;
    if (stat(full_path, &st) != 0) {
      continue;
    }

    if (S_ISREG(st.st_mode)) {
      // regular file - we need to create a blob object for it.
      strcpy(entries[entry_count].mode, "100644");
      strcpy(entries[entry_count].name, entry->d_name);
      char *blob_hash = create_blob_object(full_path);
      if (blob_hash) {
        // convert the hexadecimal hash string to binary format
        // Git tree objects store hashes as 20 raw bytes, not as hex strings
        // Each hex digit pair (like 'a3') becomes one byte
        for (int i = 0; i < SHA_DIGEST_LENGTH; i++) {
          sscanf(blob_hash + i * 2, "%2hhx", &entries[entry_count].hash[i]);
        }
        free(blob_hash);
        entry_count++;
      }
    } else if (S_ISDIR(st.st_mode)) {
      strcpy(entries[entry_count].mode, "40000");
      strcpy(entries[entry_count].name, entry->d_name);
      char *tree_hash = create_tree_object(full_path);
      if (tree_hash) {
        // converting the hexadecimal hash string to binary format.
        for (int i = 0; i < SHA_DIGEST_LENGTH; i++) {
          sscanf(tree_hash + i * 2, "%2hhx", &entries[entry_count].hash[i]);
        }
        free(tree_hash);
        entry_count++;
      }
    }
  }
  closedir(dir);

  // Tree format: for each entry -> "mode filename\0<20-byte-hash>"
  size_t content_size = 0;
  for (int i = 0; i < entry_count; i++) {
    content_size += strlen(entries[i].mode) + // Length of mode string
                    1 +                       // Space between mode and name
                    strlen(entries[i].name) + // length of filename
                    1 +                       // null byte after filename
                    SHA_DIGEST_LENGTH;        // 20 bytes for SHA-1 hash
  }

  // Git object format: "tree <content_size>\0<content>"
  char header[64];
  int header_len = snprintf(header, sizeof(header), "tree %zu", content_size);

  // calculate total tree object size (header + null byte + content)
  size_t tree_size = header_len + 1 + content_size;
  unsigned char *tree_data = malloc(tree_size);

  // build tree object
  memcpy(tree_data, header, header_len);
  tree_data[header_len] = '\0';

  // build tree content
  unsigned char *ptr =
      tree_data + header_len + 1; // point to start of content area

  for (int i = 0; i < entry_count; i++) {
    // for each entry, write "mode filename\0<20-byte-hash>"
    memcpy(ptr, entries[i].mode, strlen(entries[i].mode));
    ptr += strlen(entries[i].mode);

    *ptr++ = '\0';

    memcpy(ptr, entries[i].hash, SHA_DIGEST_LENGTH);
    ptr += SHA_DIGEST_LENGTH;
  }
  // SHA-1 hash for the complete object
  unsigned char hash[SHA_DIGEST_LENGTH];
  SHA1(tree_data, tree_size, hash);

  // convert binary hash to hexadecimal string for the display and file naming
  char *hash_hex = malloc(41); // 40 hex char + null terminator
  for (int i = 0; i < SHA_DIGEST_LENGTH; i++) {
    sprintf(hash_hex + i * 2, "%02x", hash[i]);
  }
  hash_hex[40] = '\0';

  // save the tree object to .git/objects dir
  char dir_path_obj[256];
  snprintf(dir_path_obj, sizeof(dir_path_obj), ".git/objects/%.2s", hash_hex);
  mkdir(dir_path_obj, 0755);

  char object_path[256];
  snprintf(object_path, sizeof(object_path), "%s/%s", dir_path_obj,
           hash_hex + 2);

  // compress the tree object
  uLongf compressed_size = compressBound(tree_size);
  unsigned char *compressed_data = malloc(compressed_size);

  // compression
  compress(compressed_data, &compressed_size, tree_data, tree_size);

  FILE *object_file = fopen(object_path, "wb");
  fwrite(compressed_data, 1, compressed_size, object_file);
  fclose(object_file);

  free(tree_data);
  free(compressed_data);

  return hash_hex;
}

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
  } else if (strcmp(command, "write-tree") == 0) {
    char *tree_hash = create_tree_object(".");
    if (tree_hash) {
      printf("%s\n", tree_hash);
      free(tree_hash);
    } else {
      fprintf(stderr, "Failed to create a tree object \n");
      return 1;
    }
  } else {
    fprintf(stderr, "Unknown command %s\n", command);
    return 1;
  }

  return 0;
}
