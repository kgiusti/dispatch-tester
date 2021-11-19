podman  build -t nginx-smoke-image-1:1 -f Containerfile .
podman run --name nginx-smoke-1 -d -p 8080:80 nginx-smoke-image-1:1
podman  build -t nginx-smoke-image-2:1 -f Containerfile .
podman run --name nginx-smoke-2 -d -p 8888:80 nginx-smoke-image-2:1
