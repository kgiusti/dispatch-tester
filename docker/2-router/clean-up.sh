#!/bin/bash

# set -x
sudo docker stop Router1-2router
sudo docker rm Router1-2router
sudo docker rmi dispatch-tester/router1:1

sudo docker stop Router2-2router
sudo docker rm Router2-2router
sudo docker rmi dispatch-tester/router2:1
