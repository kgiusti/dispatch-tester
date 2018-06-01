#!/bin/bash

docker stop Router1-2router
docker rm Router1-2router
docker rmi dispatch-tester/router1:1

docker stop Router2-2router
docker rm Router2-2router
docker rmi dispatch-tester/router2:1
