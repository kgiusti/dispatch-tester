Prometheus daemon for testing.

$ podman build -t prometheus:kag -f ./Containerfile .
$ podman run -d --rm --name prometheus --net host localhost/prometheus:kag

// point browser to http://localhost:9090, do things.

$ podman stop prometheus
