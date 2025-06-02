CC = clang
CFLAGS = -Wall -Wextra -std=c17 -g
PKG_CONFIG_CFLAGS = $(shell pkg-config --cflags openssl 2>/dev/null || echo "")
PKG_CONFIG_LDFLAGS = $(shell pkg-config --libs openssl 2>/dev/null || echo "")

LDFLAGS = $(PKG_CONFIG_LDFLAGS) -lz

TARGET = mygit
SRC = src/main.c

DOCKER_IMAGE_NAME = mygit-app
DOCKER_CONTAINER_NAME = mygit-container

all: docker-build

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(PKG_CONFIG_CFLAGS) -o $(TARGET) $(SRC) $(LDFLAGS)

docker-build:
	docker build -t $(DOCKER_IMAGE_NAME) .

run: docker-run

docker-run: docker-build
	@echo "Running in Docker. Output from your program will appear below."
	@echo "To pass arguments, use: make run ARGS=\"<your_args>\""
	docker run --rm -it \
	  -v "$(shell pwd)/.git_docker_data:/app/.git" \
	  --name $(DOCKER_CONTAINER_NAME) \
	  $(DOCKER_IMAGE_NAME) $(ARGS)

clean:
	rm -f $(TARGET)
	rm -rf .git_docker_data 
	docker rmi $(DOCKER_IMAGE_NAME) || true 
	docker rm $(DOCKER_CONTAINER_NAME) || true 

.PHONY: all clean run docker-build docker-run

