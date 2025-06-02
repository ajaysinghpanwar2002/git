# MyGit

MyGit is a Git-like command-line tool written in C.

### Build the Docker Image

To build the Docker image:

```sh
make docker-build
```
Alternatively, running `make all` or simply `make` will also build the image.

### Run the Application

To run `mygit` commands inside a Docker container:

```sh
make run ARGS="<command> [<args>]"
```

For example, to initialize a new repository:

```sh
make run ARGS="init"
```

To hash a file named `example.txt` and write it to the object store:

```sh
touch example.txt
echo "hello world" > example.txt
make run ARGS="hash-object -w example.txt"
```

To view the content of an object (replace `<object_hash>` with the actual hash):

```sh
make run ARGS="cat-file -p <object_hash>"
```

The `.git` directory used by the application inside the Docker container is mapped to `.git_docker_data` on your host machine. This allows you to inspect the Git data created by `mygit`.

## Makefile Targets

- `all` (default): Builds the Docker image. Equivalent to `make docker-build`.
- `docker-build`: Builds the Docker image for the application.
- `run`: Runs the `mygit` application inside a Docker container.
  - Use `ARGS` to pass commands and arguments to `mygit`. Example: `make run ARGS="init"`
- `docker-run`: Alias for `run`, also builds the image if it doesn't exist.
- `clean`: Removes the compiled binary (`mygit`), the local `.git` and `.git_docker_data` directories, and the Docker image and container.

## Cleaning Up

To remove build artifacts, Docker images, and containers:

```sh
make clean
```

## Project Structure

```
.
├── Dockerfile        # Defines the Docker build process
├── Makefile          # Defines build, run, and clean tasks
├── README.md         # This file
└── src/
    └── main.c        # Main C source code for mygit
```
