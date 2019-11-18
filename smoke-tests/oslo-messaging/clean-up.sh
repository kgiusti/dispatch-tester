#!/bin/bash

# set -x
podman stop Router1-2router
podman rm Router1-2router
podman rmi dispatch-tester/router1:1

podman stop Router2-2router
podman rm Router2-2router
podman rmi dispatch-tester/router2:1
