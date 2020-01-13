#!/bin/bash
#
set -e
usage="Usage: $0 [-p <proton-branch>] [-d <dispatch-branch>] [-u <git-url>]"
usage+=" Use -P <proton-branch> -D <dispatch-branch> or"
usage+=" -U <git-url> for cross version testing"

while getopts "p:d:P:D:u:U:" opt; do
    case $opt in
        p)
            proton_branch=$OPTARG
            ;;
        d)
            dispatch_branch=$OPTARG
            ;;
        P)
            old_proton=$OPTARG
            ;;
        D)
            old_dispatch=$OPTARG
            ;;
        u)
            dispatch_url=$OPTARG
            ;;
        U)
            old_dispatch_url=$OPTARG
            ;;
        ?)
            echo $usage
            return 1
            ;;
    esac
done

proton_branch=${proton_branch:-"master"}
old_proton=${old_proton:-$proton_branch}

dispatch_branch=${dispatch_branch:-"master"}
dispatch_url=${dispatch_url:-"https://github.com/apache/qpid-dispatch/archive"}
old_dispatch=${old_dispatch:-$dispatch_branch}
old_dispatch_url=${old_dispatch_url:-$dispatch_url}

set -x
podman build --tag dispatch-tester/router1:1 --file Dockerfile-router1 \
       --build-arg proton_branch=$proton_branch \
       --build-arg dispatch_branch=$dispatch_branch \
       --build-arg dispatch_url=$dispatch_url .

podman build --tag dispatch-tester/router2:1 --file Dockerfile-router2 \
       --build-arg proton_branch=$old_proton \
       --build-arg dispatch_branch=$old_dispatch \
       --build-arg dispatch_url=$old_dispatch_url .

podman run -d --name Router1-2router --net=host dispatch-tester/router1:1
podman run -d --name Router2-2router --net=host dispatch-tester/router2:1 
set +x

