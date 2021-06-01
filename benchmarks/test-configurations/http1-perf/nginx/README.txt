podman  build -t nginx-perf:1 -f Containerfile .
podman run --name nginx-perf -d -p 8080:80 nginx-perf:1
