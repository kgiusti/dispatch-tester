Simply instantiates a single router in a container.  The configuration
includes the default address prefixes used by Openstack.

To deploy the router:

$ sudo docker build --tag dispatch-tester/router1:1 --file Dockerfile-standalone \
       --build-arg proton_branch=$proton_branch \
       --build-arg dispatch_branch=$dispatch_branch .
$ sudo docker run -d --name Router1 --net=host dispatch-tester/router1:1

The --build-arg options are optional but can be used to select a
particular branch, tag or SHA1 to use for proton and/or dispatch.

To teardown the router:

$ sudo docker stop Router1
$ sudo docker rm Router1
$ sudo docker rmi dispatch-tester/router1:1

