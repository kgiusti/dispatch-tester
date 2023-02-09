podman  build -t nginx-perf:1 -f Containerfile .
podman run --name nginx-perf -d -p 8800:80 nginx-perf:1
podman run --name nginx-test -d -p 8800:80 -p 8443:443 nginx-test:1

for debug:
podman run --name nginx-test -p 8800:80 -p 8443:443 -a stdin -a stdout -a stderr nginx-test:1
