
# build and run the nginx container (port 8080)
# in the nginx subdirectory:

podman  build -t nginx-test:2 -f Containerfile .
podman run --name nginx2 -d -p 8080:80 nginx-test:2


# fire up qdrouterd:

numactl --physcpubind=1,5,2,6 qdrouterd -c qdrouterd.conf &

# use 'hey' to generate traffic to the router's listener port 8000:


numactl --physcpubind=3,7 hey/bin/hey_linux_amd64 -c 2 -z 20s http://127.0.0.1:8000/t1K.html
numactl --physcpubind=3,7 hey/bin/hey_linux_amd64 -c 2 -z 20s http://127.0.0.1:8000/t10K.html
numactl --physcpubind=3,7 hey/bin/hey_linux_amd64 -c 2 -z 20s http://127.0.0.1:8000/t100K.html
numactl --physcpubind=3,7 hey/bin/hey_linux_amd64 -c 2 -z 20s http://127.0.0.1:8000/t1M.html
